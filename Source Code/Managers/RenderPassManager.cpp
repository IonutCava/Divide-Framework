#include "stdafx.h"

#include "Headers/RenderPassManager.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ProfileTimer.h"

#include "Editor/Headers/Editor.h"

#include "GUI/Headers/GUI.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/RenderPass/Headers/RenderQueue.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"

namespace Divide {

void SetDefaultDrawDescriptor( RenderPassParams& params )
{
    params._clearDescriptorPrePass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;

    params._targetDescriptorMainPass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
    params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::WHITE, true };
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
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i )
    { //All of our render passes should run in parallel
        RenderPassData& passData = _renderPassData[i];
        passData._workTask = CreateTask(nullptr,
                                        [&](const Task& parentTask) {
                                           PROFILE_SCOPE("RenderPass: BuildCommandBuffer", Profiler::Category::Scene );
                                           PROFILE_TAG("Pass IDX", i);

                                           passData._cmdBuffer->clear(false);

                                           passData._memCmd = {};
                                           passData._pass->render(params._playerPass, parentTask, *params._sceneRenderState, *passData._cmdBuffer, passData._memCmd);

                                           passData._cmdBuffer->batch();
                                       },
                                       false);

        Start(*passData._workTask, pool, TaskPriority::DONT_CARE);
    }


    { //PostFX should be pretty fast
        PostFX& postFX = _context.getRenderer().postFX();
        _postFXWorkTask = CreateTask( nullptr,
                                      [&]( const Task& /*parentTask*/ )
                                      {
                                          PROFILE_SCOPE( "PostFX: BuildCommandBuffer", Profiler::Category::Scene );

                                          _postFXCmdBuffer->clear( false );

                                          Time::ScopedTimer time( *_postFxRenderTimer );
                                          postFX.apply( params._playerPass, cameraSnapshot, *_postFXCmdBuffer );

                                          _postFXCmdBuffer->batch();
                                      },
                                      false );
        Start( *_postFXWorkTask, pool, TaskPriority::REALTIME );
    }
}

void RenderPassManager::render(const RenderParams& params)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    if (params._parentTimer != nullptr && !params._parentTimer->hasChildTimer(*_renderPassTimer))
    {
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
       GFX::MemoryBarrierCommand memCmd{};
       {
            PROFILE_SCOPE("RenderPassManager::update sky light", Profiler::Category::Scene );
            _skyLightRenderBuffer->clear(false);
            memCmd = gfx.updateSceneDescriptorSet(*_skyLightRenderBuffer);
            SceneEnvironmentProbePool::UpdateSkyLight(gfx, *_skyLightRenderBuffer, memCmd );
       }

       GFX::CommandBuffer& buf = *_postRenderBuffer;
       buf.clear(false);

       const Rect<I32>& targetViewport = params._targetViewport;
       context.gui().preDraw( gfx, targetViewport, buf, memCmd );
       {
           GFX::BeginRenderPassCommand beginRenderPassCmd{};
           beginRenderPassCmd._name = "Flush Display";
           beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::BLACK, true };
           beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
           beginRenderPassCmd._target = RenderTargetNames::BACK_BUFFER;
           GFX::EnqueueCommand(buf, beginRenderPassCmd);

           const auto& screenAtt = gfx.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
           const auto& texData = screenAtt->texture()->getView();
     
           gfx.drawTextureInViewport(texData, screenAtt->descriptor()._samplerHash, targetViewport, false, false, false, buf);

           {
               Time::ScopedTimer timeGUIBuffer(*_processGUITimer);
               Attorney::SceneManagerRenderPass::drawCustomUI(sceneManager, targetViewport, buf, memCmd);
               if constexpr(Config::Build::ENABLE_EDITOR)
               {
                   context.editor().drawScreenOverlay(cam, targetViewport, buf, memCmd);
               }
               context.gui().draw(gfx, targetViewport, buf, memCmd);
               sceneManager->getEnvProbes()->prepareDebugData();
               gfx.renderDebugUI(targetViewport, buf, memCmd);
           }

           GFX::EnqueueCommand(buf, GFX::EndRenderPassCommand{});
       }

        Attorney::SceneManagerRenderPass::postRender( sceneManager, buf, memCmd );
        GFX::EnqueueCommand( buf, memCmd );
    }

    TaskPool& pool = context.taskPool(TaskPoolType::HIGH_PRIORITY);
    {
        Time::ScopedTimer timeAll(*_renderPassTimer);
        startRenderTasks(params, pool, cam->snapshot());
    }
    GFX::MemoryBarrierCommand flushMemCmd{};
    {
        PROFILE_SCOPE("RenderPassManager::FlushCommandBuffers", Profiler::Category::Scene );
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
            PROFILE_SCOPE("FLUSH_PASSES_WHEN_READY", Profiler::Category::Scene );
            U8 idleCount = 0u;

            while (stillWorking(s_completedPasses)) {

                // For every render pass
                bool finished = true;
                for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i) {
                    if (s_completedPasses[i] || !Finished(*_renderPassData[i]._workTask)) {
                        _parent.platformContext().idle(true);
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
                        PROFILE_TAG("Buffer ID: ", i);
                        Time::ScopedTimer timeGPUFlush(*_processCommandBufferTimer[i]);

                        //Start(*whileRendering);
                        // No running dependency so we can flush the command buffer and add the pass to the skip list
                        _drawCallCount[i] = _context.frameDrawCalls();
                        _context.flushCommandBuffer(*_renderPassData[i]._cmdBuffer, false);
                        _drawCallCount[i] = _context.frameDrawCalls() - _drawCallCount[i];
                        if ( !GFX::Merge( &flushMemCmd, &_renderPassData[i]._memCmd ) )
                        {
                            NOP();
                        }
                        s_completedPasses[i] = true;
                        //Wait(*whileRendering, pool);

                    } else {
                        finished = false;
                    }
                }

                if (!finished) {
                    PROFILE_SCOPE("IDLING", Profiler::Category::Scene );
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
    GFX::EnqueueCommand( *_postRenderBuffer, flushMemCmd );
    gfx.flushCommandBuffer(*_postRenderBuffer, false);

    _context.setCameraSnapshot(params._playerPass, cam->snapshot());

    {
        PROFILE_SCOPE("Executor post-render", Profiler::Category::Scene );
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

void RenderPassManager::doCustomPass(Camera* const camera, const RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    const PlayerIndex playerPass = _parent.sceneManager()->playerPass();
    _executors[to_base(params._stagePass._stage)]->doCustomPass(playerPass, camera, params, bufferInOut, memCmdInOut);
}
}
