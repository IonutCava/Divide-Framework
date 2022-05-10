#include "stdafx.h"

#include "Headers/RenderPassManager.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Editor/Headers/Editor.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/RenderPass/Headers/RenderQueue.h"

namespace Divide {
namespace {
    constexpr bool g_multiThreadedCommandGeneration = true;
}

RenderPassManager::RenderPassManager(Kernel& parent, GFXDevice& context)
    : KernelComponent(parent),
      _context(context),
      _renderPassTimer(&Time::ADD_TIMER("Render Passes")),
      _buildCommandBufferTimer(&Time::ADD_TIMER("Build Command Buffers")),
      _processGUITimer(&Time::ADD_TIMER("Process GUI")),
      _flushCommandBufferTimer(&Time::ADD_TIMER("Flush Command Buffers")),
      _postFxRenderTimer(&Time::ADD_TIMER("PostFX Timer")),
      _blitToDisplayTimer(&Time::ADD_TIMER("Flush To Display Timer"))
{
    _flushCommandBufferTimer->addChildTimer(*_buildCommandBufferTimer);
    for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i) {
        const string timerName = Util::StringFormat("Process Command Buffers [ %s ]", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
        _processCommandBufferTimer[i] = &Time::ADD_TIMER(timerName.c_str());
        _flushCommandBufferTimer->addChildTimer(*_processCommandBufferTimer[i]);
    }
    _flushCommandBufferTimer->addChildTimer(*_processGUITimer);
    _flushCommandBufferTimer->addChildTimer(*_blitToDisplayTimer);
    _drawCallCount.fill(0);
}

RenderPassManager::~RenderPassManager()
{
    for (auto& data : _renderPassData) {
        if (data._cmdBuffer != nullptr) {
            DeallocateCommandBuffer(data._cmdBuffer);
        }
        MemoryManager::SAFE_DELETE(data._pass);
    }
    if (_postFXCmdBuffer != nullptr) {
        DeallocateCommandBuffer(_postFXCmdBuffer);
    }
    if (_postRenderBuffer != nullptr) {
        DeallocateCommandBuffer(_postRenderBuffer);
    } 
    if (_skyLightRenderBuffer != nullptr) {
        DeallocateCommandBuffer(_skyLightRenderBuffer);
    }
}

void RenderPassManager::postInit() {
    ShaderModuleDescriptor vertModule{ ShaderType::VERTEX, "baseVertexShaders.glsl", "FullScreenQuad" };

    {
        ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "OITComposition.glsl" };

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);
        shaderDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        ResourceDescriptor shaderResDesc("OITComposition");
        shaderResDesc.propertyDescriptor(shaderDescriptor);
        _OITCompositionShader = CreateResource<ShaderProgram>(parent().resourceCache(), shaderResDesc);

        shaderDescriptor._modules.back()._defines.emplace_back("USE_MSAA_TARGET");
        ResourceDescriptor shaderResMSDesc("OITCompositionMS");
        shaderResMSDesc.propertyDescriptor(shaderDescriptor);
        _OITCompositionShaderMS = CreateResource<ShaderProgram>(parent().resourceCache(), shaderResMSDesc);
    }
    {
        const Configuration& config = _parent.platformContext().config();

        ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "display.glsl", "ResolveGBuffer"};
        fragModule._defines.emplace_back(Util::StringFormat("NUM_SAMPLES %d", config.rendering.MSAASamples));

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);
        shaderDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        ResourceDescriptor shaderResolveDesc("GBufferResolveShader");
        shaderResolveDesc.propertyDescriptor(shaderDescriptor);
        _gbufferResolveShader = CreateResource<ShaderProgram>(parent().resourceCache(), shaderResolveDesc);
    }

    for (auto& executor : _executors) {
        if (executor != nullptr) {
            executor->postInit( _OITCompositionShader, _OITCompositionShaderMS, _gbufferResolveShader );
        }
    }

    _postFXCmdBuffer = GFX::AllocateCommandBuffer();
    _postRenderBuffer = GFX::AllocateCommandBuffer();
    _skyLightRenderBuffer = GFX::AllocateCommandBuffer();
}

void RenderPassManager::startRenderTasks(const RenderParams& params, TaskPool& pool, const CameraSnapshot& cameraSnapshot) {
    OPTICK_EVENT();

    const auto GetTaskPriority = [](const I8 renderStageIdx) noexcept {
        if (renderStageIdx == 0) {
            return TaskPriority::REALTIME;
        }

        return g_multiThreadedCommandGeneration ? TaskPriority::DONT_CARE : TaskPriority::REALTIME;
    }; 

    { //PostFX should be pretty fast
        PostFX& postFX = _context.getRenderer().postFX();
        _postFXWorkTask = CreateTask(nullptr,
                                     [player = params._playerPass,
                                      buf = _postFXCmdBuffer,
                                      sceneManager = parent().sceneManager(),
                                      &postFX,
                                      cameraSnapshot,
                                      timer = _postFxRenderTimer](const Task& /*parentTask*/)
                                      {
                                          OPTICK_EVENT("PostFX: BuildCommandBuffer");
                                 
                                          buf->clear(false);
                                 
                                          Time::ScopedTimer time(*timer);
                                          postFX.apply(player, cameraSnapshot, *buf);
                                 
                                          buf->batch();
                                     },
                                     false);
        Start(*_postFXWorkTask, pool, GetTaskPriority(to_I8(RenderStage::COUNT)));
    }

    for (I8 i = to_I8(RenderStage::COUNT) - 1; i >= 0; i--)
    { //All of our render passes should run in parallel
        RenderPassData& passData = _renderPassData[i];
        passData._workTask = CreateTask(nullptr,
                                        [i, player = params._playerPass, &passData, sceneRenderState = params._sceneRenderState](const Task& parentTask) {
                                           OPTICK_EVENT("RenderPass: BuildCommandBuffer");
                                           OPTICK_TAG("Pass IDX", i);
                                           passData._cmdBuffer->clear(false);
                                           passData._memCmd = {};
                                           passData._pass->render(player, parentTask, *sceneRenderState, *passData._cmdBuffer, passData._memCmd);
                                           passData._cmdBuffer->batch();
                                       },
                                       false);

        Start(*passData._workTask, pool, GetTaskPriority(i));
    }
}

void RenderPassManager::render(const RenderParams& params) {
    OPTICK_EVENT();

    if (params._parentTimer != nullptr && !params._parentTimer->hasChildTimer(*_renderPassTimer)) {
        params._parentTimer->addChildTimer(*_renderPassTimer);
        params._parentTimer->addChildTimer(*_postFxRenderTimer);
        params._parentTimer->addChildTimer(*_flushCommandBufferTimer);
    }

    GFXDevice& gfx = _context;
    PlatformContext& context = parent().platformContext();
    SceneManager* sceneManager = parent().sceneManager();

    const Camera* cam = Attorney::SceneManagerRenderPass::playerCamera(sceneManager);

    LightPool& activeLightPool = Attorney::SceneManagerRenderPass::lightPool(sceneManager);

    const CameraSnapshot& prevSnapshot = _context.getCameraSnapshot(params._playerPass);
    _context.setPreviousViewProjectionMatrix(prevSnapshot._viewMatrix, prevSnapshot._projectionMatrix);

    activeLightPool.preRenderAllPasses(cam);

    {
       Time::ScopedTimer timeCommandsBuild(*_buildCommandBufferTimer);
       GFX::MemoryBarrierCommand sceneBufferLocks{};
       {
           OPTICK_EVENT("RenderPassManager::update sky light");
            _skyLightRenderBuffer->clear(false);
            sceneBufferLocks = Attorney::SceneManagerRenderPass::bindSceneDescriptorSet(sceneManager, *_skyLightRenderBuffer);
            SceneEnvironmentProbePool::UpdateSkyLight(gfx, *_skyLightRenderBuffer, sceneBufferLocks);
       }
       GFX::CommandBuffer& buf = *_postRenderBuffer;
       buf.clear(false);

       GFX::EnqueueCommand(buf, sceneBufferLocks);

       if (params._editorRunning) {
           GFX::BeginRenderPassCommand beginRenderPassCmd{};
           beginRenderPassCmd._target = context.editor().getRenderTargetHandle()._targetID;
           beginRenderPassCmd._name = "BLIT_TO_RENDER_TARGET";
           EnqueueCommand(buf, beginRenderPassCmd);
       }

       GFX::EnqueueCommand(buf, GFX::BeginDebugScopeCommand{ "Flush Display" });

       RenderTarget* resolvedScreenTarget = gfx.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN);
       const auto& screenAtt = resolvedScreenTarget->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
       const TextureData texData = screenAtt.texture()->data();
       const Rect<I32>& targetViewport = params._targetViewport;
       // Apply gamma correction here as PostFX requires everything in linear space
       gfx.drawTextureInViewport(texData, screenAtt.samplerHash(), targetViewport, true, false, false, buf);

       {
           Time::ScopedTimer timeGUIBuffer(*_processGUITimer);
           Attorney::SceneManagerRenderPass::drawCustomUI(sceneManager, targetViewport, buf);
           if_constexpr(Config::Build::ENABLE_EDITOR) {
               context.editor().drawScreenOverlay(cam, targetViewport, buf);
           }
           context.gui().draw(gfx, targetViewport, buf);
           sceneManager->getEnvProbes()->prepareDebugData();
           gfx.renderDebugUI(targetViewport, buf);

           GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(buf);
           if (params._editorRunning) {
               EnqueueCommand(buf, GFX::EndRenderPassCommand{});
           }
       }
    }
    TaskPool& pool = context.taskPool(TaskPoolType::HIGH_PRIORITY);
    {
        Time::ScopedTimer timeAll(*_renderPassTimer);
        startRenderTasks(params, pool, cam->snapshot());
    }
    {
        OPTICK_EVENT("RenderPassManager::FlushCommandBuffers");
        Time::ScopedTimer timeCommands(*_flushCommandBufferTimer);

        gfx.flushCommandBuffer(*_skyLightRenderBuffer);
        static eastl::array<bool, MAX_RENDER_PASSES> s_completedPasses;

        s_completedPasses.fill(false);

        // std::any_of(begin, begin + stage_count, false);
        const auto stillWorking = [](const eastl::array<bool, MAX_RENDER_PASSES>& passes) noexcept {
            for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i) {
                if (!passes[i]) {
                    return true;
                }
            }

            return false;
        };

        {
            OPTICK_EVENT("FLUSH_PASSES_WHEN_READY");
            U8 idleCount = 0u;

            while (stillWorking(s_completedPasses)) {

                // For every render pass
                bool finished = true;
                for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i) {
                    if (s_completedPasses[i] || !Finished(*_renderPassData[i]._workTask)) {
                        _parent.platformContext().idle(PlatformContext::SystemComponentType::COUNT);
                        continue;
                    }

                    // Grab the list of dependencies
                    const auto& dependencies = _renderPassData[i]._pass->dependencies();

                    bool dependenciesRunning = false;
                    // For every dependency in the list try and see if it's running
                    for (U8 j = 0u; j < to_base(RenderStage::COUNT) && !dependenciesRunning; ++j) {
                        // If it is running, we can't render yet
                        if (j != i && !s_completedPasses[j]) {
                            for (const RenderStage dep : dependencies) {
                                if (_renderPassData[j]._pass->stageFlag() == dep) {
                                    dependenciesRunning = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (!dependenciesRunning) {
                        OPTICK_TAG("Buffer ID: ", i);
                        Time::ScopedTimer timeGPUFlush(*_processCommandBufferTimer[i]);

                        //Start(*whileRendering);
                        // No running dependency so we can flush the command buffer and add the pass to the skip list
                        _drawCallCount[i] = _context.frameDrawCalls();
                        _context.flushCommandBuffer(*_renderPassData[i]._cmdBuffer, false);
                        GFX::EnqueueCommand(*_postRenderBuffer, _renderPassData[i]._memCmd);
                        _drawCallCount[i] = _context.frameDrawCalls() - _drawCallCount[i];

                        s_completedPasses[i] = true;
                        //Wait(*whileRendering, pool);

                    } else {
                        finished = false;
                    }
                }

                if (!finished) {
                    OPTICK_EVENT("IDLING");
                    if (idleCount++ % 2 == 0) {
                        parent().idle(idleCount > 3);
                    } else {
                        pool.threadWaiting(true);
                    }
                }
            }
        }
    }

    // Flush the postFX stack
    Wait(*_postFXWorkTask, pool);
    _context.flushCommandBuffer(*_postFXCmdBuffer, false);

    activeLightPool.postRenderAllPasses();

    Time::ScopedTimer time(*_blitToDisplayTimer);
    gfx.flushCommandBuffer(*_postRenderBuffer);

    _context.setCameraSnapshot(params._playerPass, cam->snapshot());

    {
        OPTICK_EVENT("Executor post-render");
        for (auto& executor : _executors) {
            if (executor != nullptr) {
                executor->postRender();
            }
        }
    }
}

RenderPass& RenderPassManager::setRenderPass(const RenderStage renderStage, const vector<RenderStage>& dependencies)
{
    DIVIDE_ASSERT(Runtime::isMainThread());

    RenderPass* item = nullptr;

    if (_executors[to_base(renderStage)] != nullptr) {
        item = _renderPassData[to_base(renderStage)]._pass;
        item->dependencies(dependencies);
    } else {
        _executors[to_base(renderStage)] = eastl::make_unique<RenderPassExecutor>(*this, _context, renderStage);
        item = MemoryManager_NEW RenderPass(*this, _context, renderStage, dependencies);
        _renderPassData[to_base(renderStage)]._pass = item;

        //Secondary command buffers. Used in a threaded fashion. Always keep an extra buffer for PostFX
        _renderPassData[to_base(renderStage)]._cmdBuffer = GFX::AllocateCommandBuffer();
    }

    return *item;
}

U32 RenderPassManager::getLastTotalBinSize(const RenderStage renderStage) const noexcept {
    return getPassForStage(renderStage).getLastTotalBinSize();
}

const RenderPass& RenderPassManager::getPassForStage(const RenderStage renderStage) const noexcept {
    return *_renderPassData[to_base(renderStage)]._pass;
}

void RenderPassManager::doCustomPass(Camera* camera, const RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    const PlayerIndex playerPass = _parent.sceneManager()->playerPass();
    _executors[to_base(params._stagePass._stage)]->doCustomPass(playerPass, camera, params, bufferInOut, memCmdInOut);
}
}
