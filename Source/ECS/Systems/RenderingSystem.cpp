// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/RenderingSystem.h"

#include "Graphs/Headers/SceneNode.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Graphs/Headers/SceneGraph.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "Geometry/Material/Headers/Material.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{
    RenderingSystem::RenderingSystem( ECS::ECSEngine& parentEngine, PlatformContext& context )
        : PlatformContextComponent( context )
        , ECSSystem( parentEngine )
    {
    }

    void RenderingSystem::PreUpdate( const F32 dt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PreUpdate( dt );

        const U64 microSec = Time::MillisecondsToMicroseconds( dt );

        for ( RenderingComponent* comp : _componentCache )
        {
            if ( comp->_materialInstance != INVALID_HANDLE<Material> )
            {
                comp->_materialUpdateMask |= Get(comp->_materialInstance)->update( microSec );
            }
            if (comp->parentSGN()->hasFlag(SceneGraphNode::Flags::LOADING))
            {
                if (comp->_worldAOParticipant)
                {
                    comp->parentSGN()->sceneGraph()->invalidateWorldAO();
                    comp->_worldAOParticipant = false;
                }
                comp->_worldAOPolicyRevision = U64_MAX;
                continue;
            }
            const U64 materialRevision = comp->_materialInstance == INVALID_HANDLE<Material>
                                       ? 0u : Get(comp->_materialInstance)->coverageRevision();
            const U64 nodeRevision = comp->parentSGN()->getNode().renderState().coverageRevision();
            SceneGraph& graph = *comp->parentSGN()->sceneGraph();
            const bool coverageChanged = materialRevision != comp->_materialCoverageRevision ||
                                         nodeRevision != comp->_nodeCoverageRevision;
            if (coverageChanged)
            {
                const BoundsComponent* bounds = comp->parentSGN()->get<BoundsComponent>();
                if (bounds != nullptr)
                {
                    graph.invalidateShadowBounds(*comp->parentSGN(), bounds->getBoundingSphere(),
                        comp->parentSGN()->usageContext() == NodeUsageContext::NODE_STATIC);
                }
            }
            if (coverageChanged || graph.worldAORevision() != comp->_worldAOPolicyRevision)
            {
                const bool participates = comp->worldAOContributor();
                if (participates != comp->_worldAOParticipant || (participates && coverageChanged))
                {
                    graph.invalidateWorldAO();
                }
                comp->_worldAOParticipant = participates;
                comp->_worldAOPolicyRevision = graph.worldAORevision();
            }
            comp->_materialCoverageRevision = materialRevision;
            comp->_nodeCoverageRevision = nodeRevision;
        }

        for ( RenderingComponent* comp : _componentCache )
        {
            comp->updateReflectRefractDescriptors( false, false );
            if ( comp->rebuildDrawCommands() || comp->parentSGN()->getNode().rebuildDrawCommands() )
            {
                comp->parentSGN()->getNode().rebuildDrawCommands( false );
                comp->rebuildDrawCommands( false );
                comp->_materialUpdateMask |= to_base( MaterialUpdateResult::NEW_SHADER );
                comp->_materialUpdateMask |= to_base( MaterialUpdateResult::NEW_CULL );
            }
        }
    }

    void RenderingSystem::Update( const F32 dt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::Update( dt );

        const U64 microSec = Time::MillisecondsToMicroseconds( dt );

        for ( RenderingComponent* comp : _componentCache )
        {
            if ( comp->_materialUpdateMask == to_base( MaterialUpdateResult::OK ) )
            {
                continue;
            }

            DIVIDE_ASSERT( comp->_materialInstance != INVALID_HANDLE<Material> );

            if ( comp->_materialUpdateMask & to_base(MaterialUpdateResult::NEW_SHADER ) ||
                 comp->_materialUpdateMask & to_base(MaterialUpdateResult::NEW_CULL ) )
            {
                comp->clearDrawPackages();
                Get(comp->_materialInstance)->clearRenderStates();
            }

            if ( comp->_materialUpdateMask & to_base(MaterialUpdateResult::NEW_CULL ) )
            {
                Get(comp->_materialInstance)->updateCullState();
            }
            if ( comp->_materialUpdateMask & to_base(MaterialUpdateResult::NEW_TRANSPARENCY ) )
            {
                NOP();
            }

            comp->updateReflectRefractDescriptors( comp->_materialUpdateMask & to_base(MaterialUpdateResult::NEW_REFLECTION ),
                                                   comp->_materialUpdateMask & to_base(MaterialUpdateResult::NEW_REFRACTION ) );
        }

        Material::Update( microSec );
    }

    void RenderingSystem::PostUpdate( const F32 dt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PostUpdate( dt );

        for ( RenderingComponent* comp : _componentCache )
        {
            comp->_materialUpdateMask = to_base( MaterialUpdateResult::OK );
        }
    }

    bool RenderingSystem::saveCache( const SceneGraphNode* sgn, ByteBuffer& outputBuffer )
    {
        if ( Parent::saveCache( sgn, outputBuffer ) )
        {
            const RenderingComponent* rComp = sgn->GetComponent<RenderingComponent>();
            return (rComp == nullptr || rComp->saveCache( outputBuffer ));
        }

        return false;
    }

    bool RenderingSystem::loadCache( SceneGraphNode* sgn, ByteBuffer& inputBuffer )
    {
        if ( Parent::loadCache( sgn, inputBuffer ) )
        {
            RenderingComponent* rComp = sgn->GetComponent<RenderingComponent>();
            return (rComp == nullptr || rComp->loadCache( inputBuffer ));
        }

        return false;
    }

} //namespace Divide
