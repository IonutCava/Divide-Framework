#include "stdafx.h"

#include "Headers/RenderPassManager.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Editor/Headers/Editor.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/RenderPass/Headers/RenderQueue.h"

namespace Divide {
namespace {
    // Just an upper cap for containers. Increasing it will not break anything
    constexpr U32 g_maxRenderPasses = 16u;
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
    for (U8 i = 0; i < to_base(RenderStage::COUNT); ++i) {
        const stringImpl timerName = Util::StringFormat("Process Command Buffers [ %s ]", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
        _processCommandBufferTimer[i] = &Time::ADD_TIMER(timerName.c_str());
        _flushCommandBufferTimer->addChildTimer(*_processCommandBufferTimer[i]);
    }
    _flushCommandBufferTimer->addChildTimer(*_processGUITimer);
    _flushCommandBufferTimer->addChildTimer(*_blitToDisplayTimer);
    _drawCallCount.fill(0);
}

RenderPassManager::~RenderPassManager()
{
    for (GFX::CommandBuffer*& buf : _renderPassCommandBuffer) {
        DeallocateCommandBuffer(buf);
    }

    if (_postFXCommandBuffer != nullptr) {
        DeallocateCommandBuffer(_postFXCommandBuffer);
    }  
    if (_postRenderBuffer != nullptr) {
        DeallocateCommandBuffer(_postRenderBuffer);
    }
    MemoryManager::DELETE_CONTAINER(_renderPasses);
}

void RenderPassManager::postInit() {
    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "OITComposition.glsl";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor shaderResDesc("OITComposition");
    shaderResDesc.propertyDescriptor(shaderDescriptor);
    _OITCompositionShader = CreateResource<ShaderProgram>(parent().resourceCache(), shaderResDesc);

    shaderDescriptor._modules.back()._defines.emplace_back("USE_MSAA_TARGET", true);
    ResourceDescriptor shaderResMSDesc("OITCompositionMS");
    shaderResMSDesc.propertyDescriptor(shaderDescriptor);
    _OITCompositionShaderMS = CreateResource<ShaderProgram>(parent().resourceCache(), shaderResMSDesc);

    fragModule._sourceFile = "display.glsl";
    fragModule._variant = "ResolveScreen";

    shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor shaderScreenResolveDesc("ScreenResolveShader");
    shaderScreenResolveDesc.propertyDescriptor(shaderDescriptor);
    _screenResolveShader = CreateResource<ShaderProgram>(parent().resourceCache(), shaderScreenResolveDesc);

    for (auto& executor : _executors) {
        if (executor != nullptr) {
            executor->postInit(_OITCompositionShader, _OITCompositionShaderMS, _screenResolveShader);
        }
    }

    _renderPassCommandBuffer.push_back(GFX::AllocateCommandBuffer());
    _postFXCommandBuffer = GFX::AllocateCommandBuffer();
    _postRenderBuffer = GFX::AllocateCommandBuffer();
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

    const SceneRenderState& sceneRenderState = *params._sceneRenderState;
    const Camera* cam = Attorney::SceneManagerRenderPass::playerCamera(sceneManager);
    const SceneStatePerPlayer& playerState = Attorney::SceneManagerRenderPass::playerState(sceneManager);
    gfx.setPreviousViewProjection(playerState.previousViewMatrix(), playerState.previousProjectionMatrix());

    LightPool& activeLightPool = Attorney::SceneManagerRenderPass::lightPool(sceneManager);

    activeLightPool.preRenderAllPasses(cam);

    TaskPool& pool = context.taskPool(TaskPoolType::HIGH_PRIORITY);
    RenderTarget& resolvedScreenTarget = gfx.renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN));

    const U8 renderPassCount = to_U8(_renderPasses.size());

    Task* postFXTask;
    {
        OPTICK_EVENT("RenderPassManager::BuildCommandBuffers");
        Time::ScopedTimer timeCommands(*_buildCommandBufferTimer);
        {
            Time::ScopedTimer timeAll(*_renderPassTimer);

            for (I8 i = 0; i < renderPassCount; ++i)
            { //All of our render passes should run in parallel

                RenderPass* pass = _renderPasses[i];

                GFX::CommandBuffer* buf = _renderPassCommandBuffer[i];
                _renderTasks[i] = CreateTask(nullptr,
                                             [pass, buf, &sceneRenderState](const Task & parentTask) {
                                                 OPTICK_EVENT("RenderPass: BuildCommandBuffer");
                                                 buf->clear(false);
                                                 pass->render(parentTask, sceneRenderState, *buf);
                                                 buf->batch();
                                             },
                                             false);
                Start(*_renderTasks[i], pool, g_multiThreadedCommandGeneration ? TaskPriority::DONT_CARE : TaskPriority::REALTIME);
            }
            { //PostFX should be pretty fast
                GFX::CommandBuffer* buf = _postFXCommandBuffer;

                Time::ProfileTimer& timer = *_postFxRenderTimer;
                postFXTask = CreateTask(nullptr,
                                        [buf, &gfx, &cam, &timer](const Task & /*parentTask*/) {
                                            OPTICK_EVENT("PostFX: BuildCommandBuffer");

                                            buf->clear(false);

                                            Time::ScopedTimer time(timer);
                                            gfx.getRenderer().postFX().apply(cam, *buf);
                                            buf->batch();
                                        },
                                        false);
                Start(*postFXTask, pool, g_multiThreadedCommandGeneration ? TaskPriority::DONT_CARE : TaskPriority::REALTIME);
            }
        }
    }
    {
       GFX::CommandBuffer& buf = *_postRenderBuffer;
       buf.clear(false);

       if (params._editorRunning) {
           GFX::BeginRenderPassCommand beginRenderPassCmd = {};
           beginRenderPassCmd._target = RenderTargetID(RenderTargetUsage::EDITOR);
           beginRenderPassCmd._name = "BLIT_TO_RENDER_TARGET";
           EnqueueCommand(buf, beginRenderPassCmd);
       }

       EnqueueCommand(buf, GFX::BeginDebugScopeCommand{ "Flush Display" });

       const auto& screenAtt=  resolvedScreenTarget.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
       const TextureData texData = screenAtt.texture()->data();
       const Rect<I32>& targetViewport = params._targetViewport;
       // Apply gamma correction here as PostFX requires everything in linear space
       gfx.drawTextureInViewport(texData, screenAtt.samplerHash(), targetViewport, true, false, buf);

       {
           Time::ScopedTimer timeGUIBuffer(*_processGUITimer);
           Attorney::SceneManagerRenderPass::drawCustomUI(sceneManager, targetViewport, buf);
           if_constexpr(Config::Build::ENABLE_EDITOR) {
               context.editor().drawScreenOverlay(cam, targetViewport, buf);
           }
           context.gui().draw(gfx, targetViewport, buf);
           gfx.renderDebugUI(targetViewport, buf);

           EnqueueCommand(buf, GFX::EndDebugScopeCommand{});
           if (params._editorRunning) {
               EnqueueCommand(buf, GFX::EndRenderPassCommand{});
           }
       }
    }
    {
        OPTICK_EVENT("RenderPassManager::FlushCommandBuffers");
        Time::ScopedTimer timeCommands(*_flushCommandBufferTimer);

        eastl::array<bool, g_maxRenderPasses> completedPasses{};
        {
            OPTICK_EVENT("FLUSH_PASSES_WHEN_READY");
            U8 idleCount = 0u;
            while (!eastl::all_of(cbegin(completedPasses), cbegin(completedPasses) + renderPassCount, [](const bool v) { return v; })) {

                // For every render pass
                bool finished = true;
                for (U8 i = 0; i < renderPassCount; ++i) {
                    if (completedPasses[i] || !Finished(*_renderTasks[i])) {
                        continue;
                    }

                    // Grab the list of dependencies
                    const auto& dependencies = _renderPasses[i]->dependencies();

                    bool dependenciesRunning = false;
                    // For every dependency in the list try and see if it's running
                    for (U8 j = 0u; j < renderPassCount; ++j) {
                        if (dependenciesRunning) {
                            break;
                        }

                        // If it is running, we can't render yet
                        if (j != i && !completedPasses[j]) {
                            for (const auto dep : dependencies) {
                                if (_renderPasses[j]->sortKey() == dep) {
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
                        _context.flushCommandBuffer(*_renderPassCommandBuffer[i], false);
                        _drawCallCount[i] = _context.frameDrawCalls() - _drawCallCount[i];

                        completedPasses[i] = true;
                        //Wait(*whileRendering);

                    } else {
                        finished = false;
                    }
                }

                if (!finished) {
                    OPTICK_EVENT("IDLING");
                    if (idleCount++ < 2) {
                        parent().idle(idleCount == 1);
                    }
                }
            }
        }
    }

    // Flush the postFX stack
    Wait(*postFXTask, pool);
    _context.flushCommandBuffer(*_postFXCommandBuffer, false);

    for (U8 i = 0; i < renderPassCount; ++i) {
        _renderPasses[i]->postRender();
    }

    activeLightPool.postRenderAllPasses();

    Time::ScopedTimer time(*_blitToDisplayTimer);
    gfx.flushCommandBuffer(*_postRenderBuffer);
}

RenderPass& RenderPassManager::addRenderPass(const Str64& renderPassName,
                                             const U8 orderKey,
                                             const RenderStage renderStage,
                                             const vectorEASTL<U8>& dependencies,
                                             const bool usePerformanceCounters) {
    assert(!renderPassName.empty());
    assert(_renderPasses.size() < g_maxRenderPasses);

    assert(_executors[to_base(renderStage)] == nullptr);
    _executors[to_base(renderStage)] = std::make_unique<RenderPassExecutor>(*this, _context, renderStage);

    RenderPass* item = MemoryManager_NEW RenderPass(*this, _context, renderPassName, orderKey, renderStage, dependencies, usePerformanceCounters);
    _renderPasses.push_back(item);

    item->initBufferData();

    //Secondary command buffers. Used in a threaded fashion
    _renderPassCommandBuffer.push_back(GFX::AllocateCommandBuffer());

    eastl::sort(begin(_renderPasses), end(_renderPasses), [](RenderPass* a, RenderPass* b) -> bool { return a->sortKey() < b->sortKey(); });

    _renderTasks.resize(_renderPasses.size());

    return *item;
}

void RenderPassManager::removeRenderPass(const Str64& name) {
    for (vectorEASTL<RenderPass*>::iterator it = begin(_renderPasses); it != end(_renderPasses); ++it) {
         if ((*it)->name().compare(name) == 0) {
             _executors[to_base((*it)->stageFlag())].reset();
            _renderPasses.erase(it);
            // Remove one command buffer
            GFX::CommandBuffer* buf = _renderPassCommandBuffer.back();
            DeallocateCommandBuffer(buf);
            _renderPassCommandBuffer.pop_back();
            break;
        }
    }

    _renderTasks.resize(_renderPasses.size());
}

U32 RenderPassManager::getLastTotalBinSize(const RenderStage renderStage) const {
    return getPassForStage(renderStage).getLastTotalBinSize();
}

const RenderPass& RenderPassManager::getPassForStage(const RenderStage renderStage) const {
    for (RenderPass* pass : _renderPasses) {
        if (pass->stageFlag() == renderStage) {
            return *pass;
        }
    }

    DIVIDE_UNEXPECTED_CALL();
    return *_renderPasses.front();
}

void RenderPassManager::doCustomPass(const RenderPassParams params, GFX::CommandBuffer& bufferInOut) {
    _executors[to_base(params._stagePass._stage)]->doCustomPass(params, bufferInOut);
}
}
