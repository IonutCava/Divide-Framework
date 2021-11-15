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
    for (GFX::CommandBuffer*& buf : _renderPassCommandBuffer) {
        if (buf != nullptr) {
            DeallocateCommandBuffer(buf);
        }
    }
    if (_postRenderBuffer != nullptr) {
        DeallocateCommandBuffer(_postRenderBuffer);
    }
    for (RenderPass* rPass : _renderPasses) {
        MemoryManager::SAFE_DELETE(rPass);
    }
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
            executor->postInit( _OITCompositionShader,
                               _OITCompositionShaderMS,
                               _screenResolveShader);
        }
    }

    _renderPassCommandBuffer[0] = GFX::AllocateCommandBuffer();
    _postRenderBuffer = GFX::AllocateCommandBuffer();
}

void RenderPassManager::startRenderTasks(const RenderParams& params, TaskPool& pool, const Camera* cam) {
    OPTICK_EVENT();

    const SceneRenderState& sceneRenderState = *params._sceneRenderState;

    Time::ScopedTimer timeAll(*_renderPassTimer);

    for (U8 i = 0u; i < _renderPassCount; ++i)
    { //All of our render passes should run in parallel
        _renderTasks[i] = CreateTask(nullptr,
            [pass = _renderPasses[i], buf = _renderPassCommandBuffer[i], &sceneRenderState](const Task& parentTask) {
            OPTICK_EVENT("RenderPass: BuildCommandBuffer");
            buf->clear(false);
            pass->render(parentTask, sceneRenderState, *buf);
            buf->batch();
        },
            false);
        Start(*_renderTasks[i], pool, g_multiThreadedCommandGeneration ? TaskPriority::DONT_CARE : TaskPriority::REALTIME);
    }
    { //PostFX should be pretty fast
        PostFX& postFX = _context.getRenderer().postFX();
        _renderTasks[_renderPassCount] = CreateTask(nullptr,
            [buf = _renderPassCommandBuffer[_renderPassCount], &postFX, cam, timer = _postFxRenderTimer](const Task& /*parentTask*/) {
                OPTICK_EVENT("PostFX: BuildCommandBuffer");

                buf->clear(false);

                Time::ScopedTimer time(*timer);
                postFX.apply(cam, *buf);
                buf->batch();
            },
            false);
        Start(*_renderTasks[_renderPassCount], pool, g_multiThreadedCommandGeneration ? TaskPriority::DONT_CARE : TaskPriority::REALTIME);
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
    const SceneStatePerPlayer& playerState = Attorney::SceneManagerRenderPass::playerState(sceneManager);
    gfx.setPreviousViewProjection(playerState.previousViewMatrix(), playerState.previousProjectionMatrix());

    LightPool& activeLightPool = Attorney::SceneManagerRenderPass::lightPool(sceneManager);

    activeLightPool.preRenderAllPasses(cam);

    TaskPool& pool = context.taskPool(TaskPoolType::HIGH_PRIORITY);

    RenderPassExecutor::PreRender();

    {
       Time::ScopedTimer timeCommandsBuild(*_buildCommandBufferTimer);
       startRenderTasks(params, pool, cam);

       GFX::CommandBuffer& buf = *_postRenderBuffer;
       buf.clear(false);

       if (params._editorRunning) {
           GFX::BeginRenderPassCommand beginRenderPassCmd{};
           beginRenderPassCmd._target = RenderTargetID(RenderTargetUsage::EDITOR);
           beginRenderPassCmd._name = "BLIT_TO_RENDER_TARGET";
           EnqueueCommand(buf, beginRenderPassCmd);
       }

       GFX::EnqueueCommand(buf, GFX::BeginDebugScopeCommand{ "Flush Display" });

       RenderTarget& resolvedScreenTarget = gfx.renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::SCREEN));
       const auto& screenAtt = resolvedScreenTarget.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
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

           GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(buf);
           if (params._editorRunning) {
               EnqueueCommand(buf, GFX::EndRenderPassCommand{});
           }
       }
    }
    {
        OPTICK_EVENT("RenderPassManager::FlushCommandBuffers");
        Time::ScopedTimer timeCommands(*_flushCommandBufferTimer);

        static eastl::array<bool, MAX_RENDER_PASSES> s_completedPasses;

        s_completedPasses.fill(false);
        const auto stillWorking = [passCount = _renderPassCount](const eastl::array<bool, MAX_RENDER_PASSES>& passes) noexcept {
            for (U8 i = 0u; i < passCount; ++i) {
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
                for (U8 i = 0u; i < _renderPassCount; ++i) {
                    if (s_completedPasses[i] || !Finished(*_renderTasks[i])) {
                        continue;
                    }

                    // Grab the list of dependencies
                    const auto& dependencies = _renderPasses[i]->dependencies();

                    bool dependenciesRunning = false;
                    // For every dependency in the list try and see if it's running
                    for (U8 j = 0u; j < _renderPassCount && !dependenciesRunning; ++j) {
                        // If it is running, we can't render yet
                        if (j != i && !s_completedPasses[j]) {
                            for (const U8 dep : dependencies) {
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
    Wait(*_renderTasks[_renderPassCount], pool);
    _context.flushCommandBuffer(*_renderPassCommandBuffer[_renderPassCount], false);

    for (U8 i = 0u; i < _renderPassCount; ++i) {
        _renderPasses[i]->postRender();
    }

    RenderPassExecutor::PostRender();

    activeLightPool.postRenderAllPasses();

    Time::ScopedTimer time(*_blitToDisplayTimer);
    gfx.flushCommandBuffer(*_postRenderBuffer);
}

RenderPass& RenderPassManager::addRenderPass(const Str64& renderPassName,
                                             const U8 orderKey,
                                             const RenderStage renderStage,
                                             const vector<U8>& dependencies,
                                             const bool usePerformanceCounters) {
    DIVIDE_ASSERT(Runtime::isMainThread());

    assert(!renderPassName.empty());
    assert(_renderPassCount < MAX_RENDER_PASSES);

    if (_executors[to_base(renderStage)] == nullptr) {
        _executors[to_base(renderStage)] = std::make_unique<RenderPassExecutor>(*this, _context, renderStage);
    }

    RenderPass* item = MemoryManager_NEW RenderPass(*this, _context, renderPassName, orderKey, renderStage, dependencies, usePerformanceCounters);
    item->initBufferData();

    _renderPasses[_renderPassCount] = item;
    eastl::sort(begin(_renderPasses), 
                begin(_renderPasses) + _renderPassCount,
                [](RenderPass* a, RenderPass* b) noexcept { return a->sortKey() < b->sortKey(); });

    //Secondary command buffers. Used in a threaded fashion. Always keep an extra buffer for PostFX
    _renderPassCommandBuffer[++_renderPassCount] = GFX::AllocateCommandBuffer();

    return *item;
}

void RenderPassManager::removeRenderPass(const Str64& name) {
    DIVIDE_ASSERT(Runtime::isMainThread());

    for (U8 i = 0u; i < _renderPassCount; ++i) {
        if (_renderPasses[i]->name().compare(name) != 0) {
            continue;
        }

        GFX::DeallocateCommandBuffer(_renderPassCommandBuffer[_renderPassCount + 1u]);

        MemoryManager::SAFE_DELETE(_renderPasses[i]);
        std::copy(begin(_renderPasses) + i + 1u,
                  end(_renderPasses),
                  begin(_renderPasses) + i);
        _renderPasses.back() = nullptr;

        --_renderPassCount;

        break;
    }
}

U32 RenderPassManager::getLastTotalBinSize(const RenderStage renderStage) const noexcept {
    return getPassForStage(renderStage).getLastTotalBinSize();
}

const RenderPass& RenderPassManager::getPassForStage(const RenderStage renderStage) const noexcept {
    for (U8 i = 0u; i < _renderPassCount; ++i) {
        const RenderPass* pass = _renderPasses[i];
        if (pass->stageFlag() == renderStage) {
            return *pass;
        }
    }

    DIVIDE_UNEXPECTED_CALL();
    return *_renderPasses[0];
}

void RenderPassManager::doCustomPass(const RenderPassParams params, GFX::CommandBuffer& bufferInOut) {
    _executors[to_base(params._stagePass._stage)]->doCustomPass(params, bufferInOut);
}
}
