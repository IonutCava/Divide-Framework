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
    : KernelComponent(parent)
    , _context(context)
    , _renderPassTimer(&Time::ADD_TIMER("Render Passes"))
    , _buildCommandBufferTimer(&Time::ADD_TIMER("Build Command Buffers"))
    , _processGUITimer(&Time::ADD_TIMER("Process GUI"))
    , _flushCommandBufferTimer(&Time::ADD_TIMER("Flush Command Buffers"))
    , _postFxRenderTimer(&Time::ADD_TIMER("PostFX Timer"))
    , _blitToDisplayTimer(&Time::ADD_TIMER("Flush To Display Timer"))
{
    _flushCommandBufferTimer->addChildTimer(*_buildCommandBufferTimer);

    for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i)
    {
        const string timerName = Util::StringFormat("Process Command Buffers [ %s ]", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
        _processCommandBufferTimer[i] = &Time::ADD_TIMER(timerName.c_str());
        _flushCommandBufferTimer->addChildTimer(*_processCommandBufferTimer[i]);
    }

    _flushCommandBufferTimer->addChildTimer(*_processGUITimer);
    _flushCommandBufferTimer->addChildTimer(*_blitToDisplayTimer);
}

RenderPassManager::~RenderPassManager()
{
    for (auto& data : _renderPassData)
    {
        if (data._cmdBuffer != nullptr)
        {
            DeallocateCommandBuffer(data._cmdBuffer);
        }
        MemoryManager::SAFE_DELETE(data._pass);
    }

    if (_postFXCmdBuffer != nullptr)
    {
        DeallocateCommandBuffer(_postFXCmdBuffer);
    }

    if (_postRenderBuffer != nullptr)
    {
        DeallocateCommandBuffer(_postRenderBuffer);
    } 

    if (_skyLightRenderBuffer != nullptr)
    {
        DeallocateCommandBuffer(_skyLightRenderBuffer);
    }
}

void RenderPassManager::postInit()
{
    const ShaderModuleDescriptor vertModule{ ShaderType::VERTEX, "baseVertexShaders.glsl", "FullScreenQuad" };
    {
        const ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "OITComposition.glsl" };

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);

        {
            ResourceDescriptor shaderResDesc("OITComposition");
            shaderResDesc.propertyDescriptor(shaderDescriptor);
            _OITCompositionShader = CreateResource<ShaderProgram>(parent().resourceCache(), shaderResDesc);
        }
        {
            shaderDescriptor._modules.back()._defines.emplace_back("USE_MSAA_TARGET");

            ResourceDescriptor shaderResMSDesc("OITCompositionMS");
            shaderResMSDesc.propertyDescriptor(shaderDescriptor);
            _OITCompositionShaderMS = CreateResource<ShaderProgram>(parent().resourceCache(), shaderResMSDesc);
        }
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

    for (auto& executor : _executors)
    {
        if (executor != nullptr)
        {
            executor->postInit( _OITCompositionShader, _OITCompositionShaderMS, _gbufferResolveShader );
        }
    }

    _postFXCmdBuffer = GFX::AllocateCommandBuffer();
    _postRenderBuffer = GFX::AllocateCommandBuffer();
    _skyLightRenderBuffer = GFX::AllocateCommandBuffer();
}

void RenderPassManager::startRenderTasks(const RenderParams& params, TaskPool& pool, Task* parentTask, const CameraSnapshot& cameraSnapshot)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    Time::ScopedTimer timeAll( *_renderPassTimer );

    GFXDevice& gfx = _context;
    for ( auto& state : _renderPassCompleted )
    {
        state.store(false);
    }

    //All of our render passes should run in parallel
    for (I8 i = to_base(RenderStage::COUNT) - 1; i >= 0; i-- )
    { 
        RenderPassData& passData = _renderPassData[i];
        passData._workTask = CreateTask( parentTask,
                                        [&, i](const Task& parentTask) {
                                           PROFILE_SCOPE("RenderPass: BuildCommandBuffer", Profiler::Category::Scene );
                                           PROFILE_TAG("Pass IDX", i);

                                           passData._cmdBuffer->clear(false);

                                           passData._memCmd = {};
                                           passData._pass->render(params._playerPass, parentTask, *params._sceneRenderState, *passData._cmdBuffer, passData._memCmd);

                                           Time::ScopedTimer timeGPUFlush( *_processCommandBufferTimer[i] );
                                           passData._cmdBuffer->batch();

                                           if (!passData._pass->dependencies().empty())
                                           {
                                               UniqueLock<Mutex> lock( _waitForDependenciesLock );
                                               _waitForDependencies.wait(lock, [&]() noexcept
                                               {
                                                   for ( const RenderStage dep : passData._pass->dependencies() )
                                                   {
                                                       if ( !_renderPassCompleted[to_base( dep )] )
                                                       {
                                                           return false;
                                                       }
                                                   }

                                                   return true;
                                               });
                                            }

                                            gfx.flushCommandBuffer(*passData._cmdBuffer, true);
                                            _renderPassCompleted[i].store(true);

                                           LockGuard<Mutex> w_lock( _waitForDependenciesLock );
                                           _waitForDependencies.notify_all();
                                       },
                                       false);

        Start(*passData._workTask, pool, TaskPriority::DONT_CARE);
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
            gfx.updateSceneDescriptorSet(*_skyLightRenderBuffer, memCmd );
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

           GFX::EnqueueCommand<GFX::EndRenderPassCommand>(buf);
       }

        Attorney::SceneManagerRenderPass::postRender( sceneManager, buf, memCmd );
        GFX::EnqueueCommand( buf, memCmd );
    }

    TaskPool& pool = context.taskPool(TaskPoolType::HIGH_PRIORITY);
    Task* renderTask = CreateTask( TASK_NOP );
    startRenderTasks(params, pool, renderTask, cam->snapshot());
    
    GFX::MemoryBarrierCommand flushMemCmd{};
    {
        PROFILE_SCOPE("RenderPassManager::FlushCommandBuffers", Profiler::Category::Scene );
        Time::ScopedTimer timeCommands(*_flushCommandBufferTimer);

        gfx.flushCommandBuffer(*_skyLightRenderBuffer);

        { //PostFX should be pretty fast
            PROFILE_SCOPE( "PostFX: CommandBuffer build", Profiler::Category::Scene );

            _postFXCmdBuffer->clear( false );

            Time::ScopedTimer time( *_postFxRenderTimer );
            _context.getRenderer().postFX().apply( params._playerPass, cam->snapshot(), *_postFXCmdBuffer );
        }

        Start( *renderTask, pool );
        if constexpr ( Config::Build::ENABLE_EDITOR )
        {
            Attorney::EditorRenderPassExecutor::getCommandBuffer(context.editor(), *_postRenderBuffer, flushMemCmd);
        }
        _parent.platformContext().idle();
        Wait( *renderTask, pool );

        for ( RenderPassData& passData : _renderPassData )
        {
            if ( !GFX::Merge( &flushMemCmd, &passData._memCmd ) )
            {
                NOP();
            }
        }
    }

    {
        PROFILE_SCOPE( "PostFX: CommandBuffer flush", Profiler::Category::Scene );
        _context.flushCommandBuffer( *_postFXCmdBuffer, false );
    }
    {
        Time::ScopedTimer time(*_blitToDisplayTimer);
        GFX::EnqueueCommand( *_postRenderBuffer, flushMemCmd );
        gfx.flushCommandBuffer(*_postRenderBuffer, false);
    }

    _context.setCameraSnapshot(params._playerPass, cam->snapshot());

    {
        PROFILE_SCOPE("Executor post-render", Profiler::Category::Scene );
        Task* sortTask = CreateTask( TASK_NOP );
        for (auto& executor : _executors)
        {
            if ( executor != nullptr )
            {
                Start( *CreateTask( sortTask,
                [&executor](const Task&)
                {
                    executor->postRender();
                }), pool);
            }
        }
        Start( *sortTask, pool );
        Wait( *sortTask, pool );
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
