

#include "config.h"

#include "Headers/RenderPass.h"
#include "Headers/NodeBufferedData.h"

#include "Core/Headers/Kernel.h"
#include "Editor/Headers/Editor.h"
#include "Core/Headers/Configuration.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/ProjectManager.h"

#include "Platform/Video/Headers/GFXDevice.h"

#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Geometry/Material/Headers/Material.h"

#include "Scenes/Headers/Scene.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"

#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide
{

    namespace
    {
        // We need a proper, time-based system, to check reflection budget
        namespace ReflectionUtil
        {
            U16 g_reflectionBudget = 0;

            [[nodiscard]] bool isInBudget() noexcept
            {
                return g_reflectionBudget < Config::MAX_REFLECTIVE_NODES_IN_VIEW;
            }
            void resetBudget() noexcept
            {
                g_reflectionBudget = 0;
            }
            void updateBudget() noexcept
            {
                ++g_reflectionBudget;
            }
            [[nodiscard]] U16  currentEntry() noexcept
            {
                return g_reflectionBudget;
            }
        }

        namespace RefractionUtil
        {
            U16 g_refractionBudget = 0;

            [[nodiscard]] bool isInBudget() noexcept
            {
                return g_refractionBudget < Config::MAX_REFRACTIVE_NODES_IN_VIEW;
            }
            void resetBudget() noexcept
            {
                g_refractionBudget = 0;
            }
            void updateBudget() noexcept
            {
                ++g_refractionBudget;
            }
            [[nodiscard]] U16  currentEntry() noexcept
            {
                return g_refractionBudget;
            }
        }
    }

    RenderPass::RenderPass( RenderPassManager& parent, GFXDevice& context, const RenderStage renderStage, const vector<RenderStage>& dependencies )
        : _dependencies( dependencies )
        , _context( context )
        , _parent( parent )
        , _config( context.context().config() )
        , _name( TypeUtil::RenderStageToString( renderStage ) )
        , _stageFlag( renderStage )
    {
        for ( U8 i = 0u; i < to_base( _stageFlag ); ++i )
        {
            const U8 passCountToSkip = TotalPassCountForStage( static_cast<RenderStage>(i) );
            _transformIndexOffset += passCountToSkip * Config::MAX_VISIBLE_NODES;
        }
    }

    RenderPass::PassData RenderPass::getPassData() const noexcept
    {
        return
        {
            ._lastCommandCount = &_lastCmdCount,
            ._lastNodeCount = &_lastNodeCount,
            ._uniforms = &_uniforms
        };
    }

    void RenderPass::render( [[maybe_unused]] const PlayerIndex idx, [[maybe_unused]] const Task& parentTask, const SceneRenderState& renderState, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        switch ( _stageFlag )
        {
            case RenderStage::DISPLAY:
            {
                PROFILE_SCOPE( "RenderPass - Main", Profiler::Category::Scene );

                RenderPassParams params{};
                params._singleNodeRenderGUID = -1;
                params._passName = "MainRenderPass";
                params._stagePass = RenderStagePass{ _stageFlag, RenderPassType::COUNT };

                params._targetDescriptorPrePass._drawMask[to_base( GFXDevice::ScreenTargets::VELOCITY )] = true;
                params._targetDescriptorPrePass._drawMask[to_base( GFXDevice::ScreenTargets::NORMALS )] = true;
                params._targetDescriptorPrePass._keepMSAADataAfterResolve = true;

                params._targetDescriptorMainPass._drawMask[to_base( GFXDevice::ScreenTargets::ALBEDO )] = true;
                params._targetDescriptorMainPass._autoResolveMSAA = false; ///< We use a custom GBuffer resolve for this
                params._targetDescriptorMainPass._keepMSAADataAfterResolve = true;

                params._targetHIZ = RenderTargetNames::HI_Z;
                params._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX]._enabled = false;

                if constexpr (true)
                {
                    STUBBED("TODO: Figure out why we need to clear the main render target in order to avoid NAN/INF issues in SSR -Ionut");
                    params._clearDescriptorMainPass[to_base( GFXDevice::ScreenTargets::ALBEDO )] = { DefaultColours::DIVIDE_BLUE, true };
                }

                //Not everything gets drawn during the depth PrePass (E.g. sky)
                params._clearDescriptorPrePass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
                params._clearDescriptorPrePass[to_base( GFXDevice::ScreenTargets::VELOCITY )] = { VECTOR4_ZERO, true };
                params._clearDescriptorPrePass[to_base( GFXDevice::ScreenTargets::NORMALS )] = { VECTOR4_ZERO, true };
                params._targetOIT = RenderTargetNames::OIT;
                params._target = RenderTargetNames::SCREEN;
                params._useMSAA = _config.rendering.MSAASamples > 0u;

                GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Main Display Pass";

                Camera* playerCamera = Attorney::ProjectManagerCameraAccessor::playerCamera( _parent.parent().projectManager().get() );
                _parent.doCustomPass( playerCamera, params, bufferInOut, memCmdInOut );
                const CameraSnapshot& camSnapshot = playerCamera->snapshot();

                GFX::EnqueueCommand<GFX::PushCameraCommand>( bufferInOut )->_cameraSnapshot = camSnapshot;

                GFX::BeginRenderPassCommand* beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
                beginRenderPassCmd->_name = "DO_POST_RENDER_PASS";
                beginRenderPassCmd->_target = RenderTargetNames::SCREEN; ///< Resolve here since rendering should be done
                beginRenderPassCmd->_descriptor._drawMask[to_base( GFXDevice::ScreenTargets::ALBEDO )] = true;

                GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Debug Draw Pass";
                Attorney::ProjectManagerRenderPass::debugDraw( _parent.parent().projectManager().get(), bufferInOut, memCmdInOut );
                GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );

                if constexpr ( Config::Build::ENABLE_EDITOR )
                {
                    Attorney::EditorRenderPassExecutor::postRender( _context.context().editor(), RenderStage::DISPLAY, camSnapshot, RenderTargetNames::SCREEN, bufferInOut, memCmdInOut );
                }

                GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );

                GFX::EnqueueCommand<GFX::PopCameraCommand>( bufferInOut );

                GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
            } break;
            case RenderStage::NODE_PREVIEW:
            {
                if constexpr( Config::Build::ENABLE_EDITOR )
                {
                    PROFILE_SCOPE( "RenderPass - Node Preview", Profiler::Category::Scene );
                    const Editor& editor = _context.context().editor();
                    if (editor.running() && editor.nodePreviewWindowVisible())
                    {
                        RenderPassParams params = {};
                        SetDefaultDrawDescriptor(params);

                        params._singleNodeRenderGUID = renderState.singleNodeRenderGUID();
                        params._minExtents.set( 1.0f );
                        params._stagePass = { _stageFlag, RenderPassType::COUNT };
                        params._target = editor.getNodePreviewTarget()._targetID;
                        params._passName = "Node Preview";
                        params._clearDescriptorPrePass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
                        params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = {editor.nodePreviewBGColour(), true};
                        params._targetDescriptorPrePass._keepMSAADataAfterResolve = true;
                        params._targetDescriptorMainPass._autoResolveMSAA = false; ///< We use a custom GBuffer resolve for this
                        params._targetDescriptorMainPass._keepMSAADataAfterResolve = true;

                        _parent.doCustomPass( editor.nodePreviewCamera(), params, bufferInOut, memCmdInOut );

                        const CameraSnapshot& camSnapshot = editor.nodePreviewCamera()->snapshot();
                        GFX::EnqueueCommand<GFX::PushCameraCommand>( bufferInOut )->_cameraSnapshot = camSnapshot;

                        GFX::BeginRenderPassCommand* beginRenderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
                        beginRenderPassCmd->_name = "DO_POST_RENDER_PASS";
                        beginRenderPassCmd->_target = params._target; ///< Resolve here since rendering should be done
                        beginRenderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

                        Attorney::EditorRenderPassExecutor::postRender( _context.context().editor(), RenderStage::NODE_PREVIEW, camSnapshot, params._target, bufferInOut, memCmdInOut );

                        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );

                        GFX::EnqueueCommand<GFX::PopCameraCommand>( bufferInOut );
                    }
                }
            } break;
            case RenderStage::SHADOW:
            {
                PROFILE_SCOPE( "RenderPass - Shadow", Profiler::Category::Scene );
                if ( _config.rendering.shadowMapping.enabled )
                {
                    ProjectManager* mgr = _parent.parent().projectManager().get();
                    LightPool& lightPool = Attorney::ProjectManagerRenderPass::lightPool( mgr );

                    const Camera* camera = Attorney::ProjectManagerCameraAccessor::playerCamera( mgr );

                    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Shadow Render Stage";
                    lightPool.sortLightData( RenderStage::SHADOW, camera->snapshot() );
                    lightPool.generateShadowMaps( *camera, bufferInOut, memCmdInOut );

                    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
                }
            } break;

            case RenderStage::REFLECTION:
            {
                ProjectManager* mgr = _parent.parent().projectManager().get();
                Camera* camera = Attorney::ProjectManagerCameraAccessor::playerCamera( mgr );

                GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Reflection Pass";
                {
                    PROFILE_SCOPE( "RenderPass - Probes", Profiler::Category::Scene );
                    SceneEnvironmentProbePool::Prepare( bufferInOut );

                    SceneEnvironmentProbePool* envProbPool = Attorney::SceneRenderPass::getEnvProbes( mgr->activeProject()->getActiveScene() );
                    envProbPool->lockProbeList();
                    const EnvironmentProbeList& probes = envProbPool->sortAndGetLocked( camera->snapshot()._eye );
                    U32 probeIdx = 0u;
                    for ( const auto& probe : probes )
                    {
                        if ( probe->refresh( bufferInOut, memCmdInOut ) && ++probeIdx == Config::MAX_REFLECTIVE_PROBES_PER_PASS )
                        {
                            break;
                        }
                    }
                    envProbPool->unlockProbeList();
                }
                {
                    PROFILE_SCOPE( "RenderPass - Reflection", Profiler::Category::Scene );
                    static VisibleNodeList<> s_Nodes;
                    //Update classic reflectors (e.g. mirrors, water, etc)
                    //Get list of reflective nodes from the scene manager
                    mgr->getSortedReflectiveNodes( camera, RenderStage::REFLECTION, true, s_Nodes );

                    // While in budget, update reflections
                    ReflectionUtil::resetBudget();
                    for ( size_t i = 0; i < s_Nodes.size(); ++i )
                    {
                        const VisibleNode& node = s_Nodes.node( i );
                        RenderingComponent* const rComp = node._node->get<RenderingComponent>();
                        if ( Attorney::RenderingCompRenderPass::updateReflection( *rComp,
                                                                                  ReflectionUtil::currentEntry(),
                                                                                  ReflectionUtil::isInBudget(),
                                                                                  camera,
                                                                                  renderState,
                                                                                  bufferInOut,
                                                                                  memCmdInOut ) )
                        {
                            ReflectionUtil::updateBudget();
                        }
                    }
                }
                GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );

            } break;

            case RenderStage::REFRACTION:
            {
                static VisibleNodeList<> s_Nodes;

                GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Refraction Pass";

                PROFILE_SCOPE( "RenderPass - Refraction", Profiler::Category::Scene );
                // Get list of refractive nodes from the scene manager
                const ProjectManager* mgr = _parent.parent().projectManager().get();
                Camera* camera = Attorney::ProjectManagerCameraAccessor::playerCamera( mgr );
                {
                    mgr->getSortedRefractiveNodes( camera, RenderStage::REFRACTION, true, s_Nodes );
                    // While in budget, update refractions
                    RefractionUtil::resetBudget();
                    for ( size_t i = 0; i < s_Nodes.size(); ++i )
                    {
                        const VisibleNode& node = s_Nodes.node( i );
                        RenderingComponent* const rComp = node._node->get<RenderingComponent>();
                        if ( Attorney::RenderingCompRenderPass::updateRefraction( *rComp,
                                                                                  RefractionUtil::currentEntry(),
                                                                                  RefractionUtil::isInBudget(),
                                                                                  camera,
                                                                                  renderState,
                                                                                  bufferInOut,
                                                                                  memCmdInOut ) )
                        {
                            RefractionUtil::updateBudget();
                        }
                    }
                }

                GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );

            } break;

            case RenderStage::COUNT:
                DIVIDE_UNEXPECTED_CALL();
                break;
        };
    }

} //namespace Divide
