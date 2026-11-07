// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/RenderPassManager.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/RenderSmokeTest.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Editor/Headers/Editor.h"

#include "GUI/Headers/GUI.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/Headers/PlatformRuntime.h"
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
    , _processGUIDisplayTimer(&Time::ADD_TIMER("Process GUI [Flush Display]"))
    , _processGUISceneTimer(&Time::ADD_TIMER("Process GUI [Scene]"))
    , _flushCommandBufferTimer(&Time::ADD_TIMER("Flush Command Buffers"))
    , _postFxRenderTimer(&Time::ADD_TIMER("PostFX Timer"))
    , _blitToDisplayTimer(&Time::ADD_TIMER("Flush To Display Timer"))
{
    _flushCommandBufferTimer->addChildTimer(*_buildCommandBufferTimer);

    for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i)
    {
        _processCommandBufferTimer[i] = &Time::ADD_TIMER( Util::StringFormat( "Process Command Buffers [ {} ]", TypeUtil::RenderStageToString( static_cast<RenderStage>(i) ) ).c_str());
        _flushCommandBufferTimer->addChildTimer(*_processCommandBufferTimer[i]);
    }

    _flushCommandBufferTimer->addChildTimer(*_processGUIDisplayTimer);
    _flushCommandBufferTimer->addChildTimer(*_processGUISceneTimer);
    _flushCommandBufferTimer->addChildTimer(*_blitToDisplayTimer);
}

RenderPassManager::~RenderPassManager()
{
    for ( Handle<ShaderProgram>& shader : _oitCompositionShaders )
    {
        DestroyResource( shader );
    }
    DestroyResource( _gbufferResolveShader );
}

void RenderPassManager::postInit()
{
    const ShaderModuleDescriptor vertModule{ ShaderType::VERTEX, "baseVertexShaders.glsl", "FullScreenQuad" };
    {
        const ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "OITComposition.glsl" };

        for ( const bool writeNormals : { false, true } )
        {
            for ( const bool useMSAA : { false, true } )
            {
                ShaderProgramDescriptor shaderDescriptor = {};
                shaderDescriptor._modules.push_back( vertModule );
                shaderDescriptor._modules.push_back( fragModule );
                if ( !writeNormals )
                {
                    shaderDescriptor._modules.back()._defines.emplace_back( "OIT_COLOUR_ONLY" );
                }
                if ( useMSAA )
                {
                    shaderDescriptor._modules.back()._defines.emplace_back( "USE_MSAA_TARGET" );
                }

                const auto variant = RenderPassExecutor::SelectOITCompositionVariant( writeNormals, useMSAA );
                ResourceDescriptor<ShaderProgram> shaderResDesc(
                    Util::StringFormat( "OITComposition{}{}", writeNormals ? "" : "ColourOnly", useMSAA ? "MS" : "" ),
                    shaderDescriptor );
                _oitCompositionShaders[to_base( variant )] = CreateResource( shaderResDesc );
            }
        }
    }
    {
        const Configuration& config = _parent.platformContext().config();

        ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "display.glsl", "ResolveGBuffer"};
        fragModule._defines.emplace_back(Util::StringFormat("NUM_SAMPLES {}", config.rendering.MSAASamples));

        ResourceDescriptor<ShaderProgram> shaderResolveDesc("GBufferResolveShader");
        ShaderProgramDescriptor& shaderDescriptor = shaderResolveDesc._propertyDescriptor;
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);

        _gbufferResolveShader = CreateResource(shaderResolveDesc);
    }

    RenderPassExecutor::PostInit( _context, _oitCompositionShaders, _gbufferResolveShader );
}

void RenderPassManager::startRenderTasks(const RenderParams& params, TaskPool& pool, Task* parentTask)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    Time::ScopedTimer timeAll( *_renderPassTimer );

    RenderStageSchedule submissionSchedule{};
    _stageSchedule.reset();
    for ( U8 stageIndex = 0u; stageIndex < to_base( RenderStage::COUNT ); ++stageIndex )
    {
        const auto& pass = _renderPassData[stageIndex]._pass;
        if ( !DIVIDE_ASSERT( pass != nullptr, "Render stage has no pass" ) )
        {
            return;
        }

        for ( const RenderStage dependency : pass->dependencies() )
        {
            if ( !DIVIDE_ASSERT( submissionSchedule.addDependency( stageIndex, to_base( dependency ) ),
                                 "Invalid render-stage dependency" ) )
            {
                return;
            }
        }

        for ( const RenderStage dependency : pass->recordingDependencies() )
        {
            if ( !DIVIDE_ASSERT( eastl::find( cbegin( pass->dependencies() ), cend( pass->dependencies() ), dependency ) != cend( pass->dependencies() ),
                                 "Render-stage recording dependencies must also be submission dependencies" ) ||
                 !DIVIDE_ASSERT( _stageSchedule.addDependency( stageIndex, to_base( dependency ) ),
                                 "Invalid render-stage recording dependency" ) )
            {
                return;
            }
        }
    }

    if ( !DIVIDE_ASSERT( submissionSchedule.topologicalOrder( _submissionOrder ), "Render-stage dependencies contain a cycle" ) ||
         !DIVIDE_ASSERT( _stageSchedule.isAcyclic(), "Render-stage recording dependencies contain a cycle" ) )
    {
        return;
    }

    _stageSchedule.prepare();
    for ( U8 i = 0u; i < to_base( RenderStage::COUNT ); ++i )
    {
        _renderPassData[i]._workTask = CreateTask( parentTask,
                                        [&, i](const Task& stageTask) {
                                           PROFILE_SCOPE("RenderPass: BuildCommandBuffer", Profiler::Category::Scene );
                                           PROFILE_TAG("Pass IDX", i);

                                           RenderPassData& passData = _renderPassData[i];
                                           {
                                               PROFILE_SCOPE( "RenderPass: Record stage commands", Profiler::Category::Scene );
#if USE_OPTICK
                                               Str<64> stageScopeName{ "Record stage: " };
                                               stageScopeName.append( TypeUtil::RenderStageToString( static_cast<RenderStage>(i) ) );
                                               PROFILE_SCOPE_DYNAMIC( stageScopeName.c_str() );
#endif //USE_OPTICK
                                               Handle<GFX::CommandBuffer> cmdBufferHandle = GFX::AllocateCommandBuffer( TypeUtil::RenderStageToString( static_cast<RenderStage>(i) ), 1024 );
                                               GFX::CommandBuffer* cmdBuffer = GFX::Get(cmdBufferHandle);

                                               passData._memCmd = {};
                                               passData._pass->render(params._playerPass, stageTask, *params._sceneRenderState, *cmdBuffer, passData._memCmd);

                                               Time::ScopedTimer timeGPUFlush( *_processCommandBufferTimer[i] );
                                               cmdBuffer->batch();

                                               I32 drawCount = 0;
                                               for ( const GFX::CommandBase* cmd : cmdBuffer->commands() )
                                               {
                                                   if ( cmd->type() == GFX::CommandType::DRAW_COMMANDS )
                                                   {
                                                       drawCount += to_I32( static_cast<const GFX::DrawCommand*>(cmd)->_drawCommands.size() );
                                                   }
                                               }
                                               _drawCallCount[i] = drawCount;

                                               // Submitted by the main thread in dependency order once every stage has finished recording.
                                               passData._cmdBuffer = cmdBufferHandle;
                                           }

                                           _stageSchedule.complete( i, [this, &pool]( const U8 readyStage )
                                           {
                                               pool.enqueue( *_renderPassData[readyStage]._workTask, TaskPriority::DONT_CARE_NO_IDLE );
                                           } );
                                       });
    }

    _stageSchedule.enqueueRoots( [this, &pool]( const U8 readyStage )
    {
        pool.enqueue( *_renderPassData[readyStage]._workTask, TaskPriority::DONT_CARE_NO_IDLE );
    } );
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
    ProjectManager* projectManager = parent().projectManager().get();
    RenderPassExecutor::PrepareGPUBuffers( gfx, projectManager->activePlayerCount() );
    for ( const auto& executor : _executors )
    {
        if ( executor != nullptr )
        {
            executor->prepareCommandBuffers( params._playerPass );
        }
    }

    const Camera* cam = Attorney::ProjectManagerRenderPass::playerCamera(projectManager);
    const I64 cameraGUID = cam->getGUID();
    if ( _previousCameraGUID[params._playerPass] != cameraGUID )
    {
        _context.invalidatePreviousDepthHistory( params._playerPass );
    }

    LightPool& activeLightPool = Attorney::ProjectManagerRenderPass::lightPool(projectManager);

    const CameraSnapshot& prevSnapshot = _context.getCameraSnapshot(params._playerPass);
    _context.setPreviousViewProjectionMatrix( params._playerPass, prevSnapshot._viewMatrix, prevSnapshot._projectionMatrix);

    activeLightPool.preRenderAllPasses(cam);

    Handle<GFX::CommandBuffer> skyLightRenderBufferHandle = GFX::AllocateCommandBuffer( "Sky Light" );
    Handle<GFX::CommandBuffer> postFXCmdBufferHandle = GFX::AllocateCommandBuffer( "PostFX" );
    Handle<GFX::CommandBuffer> postRenderBufferHandle = GFX::AllocateCommandBuffer( "Post Render" );

    GFX::CommandBuffer* skyLightRenderBuffer = GFX::Get( skyLightRenderBufferHandle );
    GFX::CommandBuffer* postFXCmdBuffer = GFX::Get( postFXCmdBufferHandle );
    GFX::CommandBuffer* postRenderBuffer = GFX::Get( postRenderBufferHandle );

    {
        PROFILE_SCOPE("RenderPassManager::build post-render buffers", Profiler::Category::Scene);

        Time::ScopedTimer timeCommandsBuild(*_buildCommandBufferTimer);
        GFX::MemoryBarrierCommand memCmd{};
        {
            PROFILE_SCOPE("RenderPassManager::update sky light", Profiler::Category::Scene );
            gfx.updateSceneDescriptorSet(*skyLightRenderBuffer, memCmd );
            SceneEnvironmentProbePool::UpdateSkyLight(gfx, *skyLightRenderBuffer, memCmd );
            projectManager->getEnvProbes()->prepareDebugData();
        }

        const Rect<I32>& targetViewport = params._targetViewport;
        context.gui().preDraw( gfx, targetViewport, *postFXCmdBuffer, memCmd );
        {

            PROFILE_SCOPE("PostFX: CommandBuffer build", Profiler::Category::Scene);
            Time::ScopedTimer time(*_postFxRenderTimer);
            _context.getRenderer().postFX().apply(params._playerPass, cam->snapshot(), *postFXCmdBuffer);
        }
        {
            PROFILE_SCOPE("In-game GUI overlays", Profiler::Category::Scene);
            Time::ScopedTimer timeGUIBuffer(*_processGUISceneTimer);

            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._target = RenderTargetNames::SCREEN;
            std::ranges::fill(beginRenderPassCmd._descriptor._drawMask, false);
            beginRenderPassCmd._descriptor._drawMask[to_base(GFXDevice::ScreenTargets::ALBEDO)] = true;
            beginRenderPassCmd._name = "DO_IN_SCENE_UI_PASS";
            beginRenderPassCmd._clearDescriptor[to_base(RTColourAttachmentSlot::SLOT_0)]._enabled = false;
            GFX::EnqueueCommand(*postFXCmdBuffer, beginRenderPassCmd);

            Attorney::ProjectManagerRenderPass::drawCustomUI(projectManager, targetViewport, *postFXCmdBuffer, memCmd);
            if constexpr(Config::Build::ENABLE_EDITOR)
            {
                context.editor().drawScreenOverlay(cam, targetViewport, *postFXCmdBuffer, memCmd);
            }

            GFX::EnqueueCommand(*postFXCmdBuffer, GFX::EndRenderPassCommand{});
        }

        Attorney::ProjectManagerRenderPass::postRender( projectManager, *postFXCmdBuffer, memCmd );

        {
            PROFILE_SCOPE("Flush to backbuffer", Profiler::Category::Scene);

            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._name = "Flush Display";
            beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::BLACK, true };
            beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
            beginRenderPassCmd._target = RenderTargetNames::BACK_BUFFER;
            GFX::EnqueueCommand(*postFXCmdBuffer, beginRenderPassCmd);

            const auto& screenAtt = gfx.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO);
            const auto& texData = Get(screenAtt->texture())->getView();
            {
                PROFILE_SCOPE("Backbuffer GUI overlays", Profiler::Category::Scene);
                Time::ScopedTimer timeGUIBuffer(*_processGUIDisplayTimer);
                gfx.drawTextureInViewport(texData, screenAtt->_descriptor._sampler, targetViewport, false, false, false, *postFXCmdBuffer );
                context.gui().draw(gfx, targetViewport, *postFXCmdBuffer, memCmd);
                gfx.renderDebugUI(targetViewport, *postFXCmdBuffer, memCmd);
            }

            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( *postFXCmdBuffer );
        }

        GFX::EnqueueCommand( *postFXCmdBuffer, memCmd );

        postFXCmdBuffer->batch();
    }

    TaskPool& pool = context.taskPool(TaskPoolType::RENDERER);
    Task* renderTask = CreateTask( TASK_NOP );
    startRenderTasks(params, pool, renderTask);
    pool.enqueue( *renderTask );
    
    GFX::MemoryBarrierCommand flushMemCmd{};
    {
        PROFILE_SCOPE("RenderPassManager::FlushCommandBuffers", Profiler::Category::Scene );
        Time::ScopedTimer timeCommands(*_flushCommandBufferTimer);

        gfx.flushCommandBuffer(MOV(skyLightRenderBufferHandle));

        pool.wait( *renderTask );
        for ( const U8 stageIndex : _submissionOrder )
        {
            RenderPassData& passData = _renderPassData[stageIndex];
            if ( passData._cmdBuffer != INVALID_HANDLE<GFX::CommandBuffer> )
            {
                gfx.flushCommandBuffer( MOV( passData._cmdBuffer ) );
                passData._cmdBuffer = INVALID_HANDLE<GFX::CommandBuffer>;
            }
        }
        activeLightPool.patchShadowIndices( flushMemCmd );
        RenderPassExecutor::FlushBuffersToGPU(flushMemCmd);

        if ( RenderSmokeTest::PerfModeRequested() ) [[unlikely]]
        {
            // GL only resolves GPU timer queries while stats are requested; VK timestamps are always on.
            _context.queryPerformanceStats( true );
            _context.queryGPUScopeTimings( RenderSmokeTest::GPUScopeTimingsRequested() );
            if ( GFXDevice::FrameCount() % PERF_METRICS_LOG_INTERVAL == 0u )
            {
                logPerfMetrics();
            }
        }

        if constexpr ( Config::Build::ENABLE_EDITOR )
        {
            Attorney::EditorRenderPassExecutor::getCommandBuffer(context.editor(), *postRenderBuffer, flushMemCmd);
        }
        _parent.platformContext().idle();

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
        _context.flushCommandBuffer( MOV(postFXCmdBufferHandle) );
    }
    {
        Time::ScopedTimer time(*_blitToDisplayTimer);
        GFX::EnqueueCommand( *postRenderBuffer, flushMemCmd );
        gfx.flushCommandBuffer( MOV(postRenderBufferHandle) );
    }

    _context.setCameraSnapshot(params._playerPass, cam->snapshot());
    _previousCameraGUID[params._playerPass] = cameraGUID;
}

RenderPass& RenderPassManager::setRenderPass(const RenderStage renderStage, const vector<RenderStage>& dependencies, const vector<RenderStage>& recordingDependencies)
{
    DIVIDE_ASSERT(Runtime::isMainThread());

    RenderPass* item = nullptr;

    if (_executors[to_base(renderStage)] == nullptr)
    {
        _executors[to_base(renderStage)] = std::make_unique<RenderPassExecutor>(*this, _context, renderStage);
        _renderPassData[to_base(renderStage)]._pass = std::make_unique<RenderPass>( *this, _context, renderStage, dependencies );
    }

    item = _renderPassData[to_base( renderStage )]._pass.get();
    item->dependencies( dependencies );
    item->recordingDependencies( recordingDependencies );
    return *item;
}

U32 RenderPassManager::getLastTotalBinSize(const RenderStage renderStage) const noexcept
{
    return getPassForStage(renderStage).getLastTotalBinSize();
}

void RenderPassManager::logPerfMetrics() const
{
    const PerformanceMetrics& metrics = _context.getPerformanceMetrics();

    string stageCounts;
    for ( U8 i = 0u; i < to_base( RenderStage::COUNT ); ++i )
    {
        const RenderStage stage = static_cast<RenderStage>(i);
        stageCounts.append( Util::StringFormat( "{}={}/{} ", TypeUtil::RenderStageToString( stage ), _drawCallCount[i],
                                                _renderPassData[i]._pass != nullptr ? getLastTotalBinSize( stage ) : 0u ) );
    }

    const uint4& cullCount = _context.lastCullCount();
    Console::printfn( "[PerfMetrics] frame={} gpuMS={:.2f} backendDraws={} hiZCulled={} hiZOverflow={} hiZSkipped={} stageDraws/binItems: {}",
                      GFXDevice::FrameCount(), metrics._gpuTimeInMS, _context.frameDrawCallsPrev(), cullCount.x, cullCount.y, cullCount.w, stageCounts );

    GPUPassTimingTable& passTimings = _context.gpuPassTimings();
    if ( passTimings._frameCount > 0u )
    {
        std::array<size_t, GPU_PASS_TIMESTAMP_CAPACITY> order{};
        for ( size_t i = 0u; i < passTimings._entryCount; ++i )
        {
            order[i] = i;
        }
        const auto orderEnd = order.begin() + passTimings._entryCount;
        std::sort( order.begin(), orderEnd, [&passTimings]( const size_t lhs, const size_t rhs )
        {
            return passTimings._entries[lhs]._totalMS > passTimings._entries[rhs]._totalMS;
        });

        D64 totalMS = 0.0;
        string passList;
        for ( size_t i = 0u; i < passTimings._entryCount; ++i )
        {
            const GPUPassTimingTable::Entry& entry = passTimings._entries[order[i]];
            const D64 avgMS = entry._totalMS / passTimings._frameCount;
            totalMS += avgMS;
            if ( i < PERF_METRICS_TOP_GPU_PASSES )
            {
                passList.append( Util::StringFormat( "[{}]={:.2f} ", entry._name.c_str(), avgMS ) );
            }
        }
        Console::printfn( "[PerfMetrics] gpuPasses frames={} sumMS={:.2f} top: {}", passTimings._frameCount, totalMS, passList );
    }
    passTimings.reset();
}

const RenderPass& RenderPassManager::getPassForStage(const RenderStage renderStage) const noexcept
{
    return *_renderPassData[to_base(renderStage)]._pass;
}

void RenderPassManager::doCustomPass(Camera* const camera, const RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut)
{
    const PlayerIndex playerPass = _parent.projectManager()->playerPass();
    _executors[to_base(params._stagePass._stage)]->doCustomPass(playerPass, camera, params, bufferInOut, memCmdInOut);
}

}
