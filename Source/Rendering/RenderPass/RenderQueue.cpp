

#include "Headers/RenderQueue.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Utility/Headers/Localization.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{
    RenderQueue::RenderQueue( Kernel& parent, const RenderStage stage )
        : KernelComponent( parent ),
          _stage( stage )
    {
    }

    RenderingOrder RenderQueue::getSortOrder( const RenderStagePass stagePass, const RenderBinType rbType ) const
    {
        RenderingOrder sortOrder = RenderingOrder::COUNT;
        switch ( rbType )
        {
            case RenderBinType::OPAQUE:
            {
                // Opaque items should be rendered front to back in depth passes for early-Z reasons
                sortOrder = IsDepthPass( stagePass ) ? RenderingOrder::FRONT_TO_BACK_ALPHA_LAST
                                                     : RenderingOrder::BY_STATE;
            } break;
            case RenderBinType::SKY:
            {
                sortOrder = RenderingOrder::NONE;
            } break;
            case RenderBinType::IMPOSTOR:
            case RenderBinType::WATER:
            case RenderBinType::TERRAIN:
            case RenderBinType::TERRAIN_AUX:
            {
                sortOrder = RenderingOrder::FRONT_TO_BACK;
            } break;
            case RenderBinType::TRANSLUCENT:
            {
                // We are using weighted blended OIT. State is fine (and faster)
                // Use an override one level up from this if we need a regular forward-style pass
                sortOrder = RenderingOrder::BY_STATE;
            } break;
            default:
            case RenderBinType::COUNT:
            {
                Console::errorfn( LOCALE_STR( "ERROR_INVALID_RENDER_BIN_CREATION" ) );
            } break;
        };

        return sortOrder;
    }

    RenderBinType RenderQueue::getBinForNode( const SceneGraphNode* node, const Handle<Material> matInstance )
    {
        switch ( node->getNode().type() )
        {
            case SceneNodeType::TYPE_VEGETATION:
            case SceneNodeType::TYPE_PARTICLE_EMITTER: return RenderBinType::TRANSLUCENT;
            case SceneNodeType::TYPE_SKY:              return RenderBinType::SKY;
            case SceneNodeType::TYPE_WATER:            return RenderBinType::WATER;
            case SceneNodeType::TYPE_INFINITEPLANE:    return RenderBinType::TERRAIN_AUX;
            case SceneNodeType::TYPE_TERRAIN:          return RenderBinType::TERRAIN;
            case SceneNodeType::TYPE_TRANSFORM:
            {
                constexpr U32 compareMask = to_U32( ComponentType::SPOT_LIGHT ) |
                                            to_U32( ComponentType::POINT_LIGHT ) |
                                            to_U32( ComponentType::DIRECTIONAL_LIGHT ) |
                                            to_U32( ComponentType::ENVIRONMENT_PROBE );
                if ( node->componentMask() & compareMask )
                {
                    return RenderBinType::IMPOSTOR;
                }
            } break;
            default:
            {
                if ( Is3DObject( node->getNode().type() ) )
                {
                    // Check if the object has a material with transparency/translucency
                    if ( matInstance != INVALID_HANDLE<Material> && Get(matInstance)->hasTransparency() )
                    {
                        // Add it to the appropriate bin if so ...
                        return RenderBinType::TRANSLUCENT;
                    }

                    //... else add it to the general geometry bin
                    return RenderBinType::OPAQUE;
                }
            } break;
            case SceneNodeType::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
        }

        return RenderBinType::COUNT;
    }

    void RenderQueue::addNodeToQueue( const SceneGraphNode* sgn,
                                      const RenderStagePass stagePass,
                                      const F32 minDistToCameraSq,
                                      const RenderBinType targetBinType )
    {
        const RenderingComponent* const renderingCmp = sgn->get<RenderingComponent>();
        // We need a rendering component to render the node
        assert( renderingCmp != nullptr );
        RenderBinType rbType = getBinForNode( sgn, renderingCmp->getMaterialInstance() );
        assert( rbType != RenderBinType::COUNT );

        if ( targetBinType == RenderBinType::COUNT || rbType == targetBinType )
        {
            getBin(rbType).addNodeToBin(sgn, stagePass, minDistToCameraSq);
        }
    }

    void RenderQueue::populateRenderQueues( const PopulateQueueParams& params, RenderQueuePackages& queueInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( params._binType == RenderBinType::COUNT )
        {
            if ( !params._filterByBinType )
            {
                for ( const RenderBin& renderBin : _renderBins )
                {
                    renderBin.populateRenderQueue( params._stagePass, queueInOut );
                }
            }
            else
            {
                // Why are we allowed to exclude everything? idk.
                NOP();
            }
        }
        else
        {
            if ( !params._filterByBinType )
            {
                if ( params._binType != RenderBinType::COUNT)
                {
                    getBin(params._binType).populateRenderQueue(params._stagePass, queueInOut);
                }
            }
            else
            {
                for ( U8 i = 0u; i < to_base( RenderBinType::COUNT ); ++i )
                {
                    if ( i == to_base( params._binType ) )
                    {
                        continue;
                    }
                    _renderBins[i].populateRenderQueue( params._stagePass, queueInOut );
                }
            }
        }
    }

    void RenderQueue::postRender( const SceneRenderState& renderState, const RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut )
    {
        for ( RenderBin& renderBin : _renderBins )
        {
            renderBin.postRender( renderState, stagePass, bufferInOut );
        }
    }

    void RenderQueue::sort( const RenderStagePass stagePass, const RenderBinType targetBinType, const RenderingOrder renderOrder )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        // How many elements should a render bin contain before we decide that sorting should happen on a separate thread
        constexpr U16 k_threadBias = 64u;

        if ( targetBinType != RenderBinType::COUNT )
        {
            const RenderingOrder sortOrder = renderOrder == RenderingOrder::COUNT ? getSortOrder( stagePass, targetBinType ) : renderOrder;
            _renderBins[to_base( targetBinType )].sort( targetBinType, sortOrder );
        }
        else
        {
            bool sortTaskDirty = false;
            TaskPool& pool = parent().platformContext().taskPool( TaskPoolType::RENDERER );
            Task* sortTask = CreateTask( TASK_NOP );
            for (U8 i = 0u; i < to_base( RenderBinType::COUNT ); ++i)
            {
                RenderBin& renderBin = _renderBins[i];
                const RenderBinType rbType = static_cast<RenderBinType>(i);

                if ( renderBin.getBinSize() > k_threadBias )
                {
                    const RenderingOrder sortOrder = renderOrder == RenderingOrder::COUNT ? getSortOrder( stagePass, rbType ) : renderOrder;
                    Start( *CreateTask( sortTask,
                                        [&renderBin, rbType, sortOrder]( const Task& )
                                        {
                                            renderBin.sort( rbType, sortOrder );
                                        } ),
                           pool );
                    sortTaskDirty = true;
                }
            }

            if ( sortTaskDirty )
            {
                Start( *sortTask, pool );
            }

            for ( U8 i = 0u; i < to_base( RenderBinType::COUNT ); ++i )
            {
                RenderBin& renderBin = _renderBins[i];
                if ( renderBin.getBinSize() <= k_threadBias )
                {
                    const RenderBinType rbType = static_cast<RenderBinType>(i);
                    const RenderingOrder sortOrder = renderOrder == RenderingOrder::COUNT ? getSortOrder( stagePass, rbType ) : renderOrder;
                    renderBin.sort( rbType, sortOrder );
                }
            }

            if ( sortTaskDirty )
            {
                Wait( *sortTask, pool );
            }
        }
    }

    void RenderQueue::clear( const RenderBinType targetBinType ) noexcept
    {
        if ( targetBinType == RenderBinType::COUNT )
        {
            for ( RenderBin& renderBin : _renderBins )
            {
                renderBin.clear();
            }
        }
        else
        {
            for ( U8 i = 0u; i < to_base( RenderBinType::COUNT ); ++i )
            {
                if ( i == to_base( targetBinType ) )
                {
                    _renderBins[i].clear();
                }
            }
        }
    }

    size_t RenderQueue::getSortedQueues( const vector<RenderBinType>& binTypes, RenderBin::SortedQueues& queuesOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        size_t countOut = 0u;

        if ( binTypes.empty() )
        {
            for ( U8 i = 0u; i < to_base( RenderBinType::COUNT ); ++i )
            {
                countOut += _renderBins[i].getSortedNodes( queuesOut[i] );
            }
        }
        else
        {
            for ( const RenderBinType type : binTypes )
            {
                countOut += getBin( type ).getSortedNodes( queuesOut[to_base( type )] );
            }
        }
        return countOut;
    }

} //namespace Divide
