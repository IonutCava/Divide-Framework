

#include "Headers/RenderPassCuller.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Scenes/Headers/SceneState.h"

namespace Divide
{

    namespace
    {
        constexpr U32 g_nodesPerCullingPartition = 8u;
    }

    [[nodiscard]] inline U32 RenderPassCuller::FilterMask(const PlatformContext& context) noexcept
    {
        const auto& filter = context.config().debug.renderFilter;
        return  (filter.primitives ? 0u : to_base(EntityFilter::PRIMITIVES)) |
                (filter.meshes     ? 0u : to_base(EntityFilter::MESHES )) |
                (filter.terrain    ? 0u : to_base(EntityFilter::TERRAIN )) |
                (filter.vegetation ? 0u : to_base(EntityFilter::VEGETATION )) |
                (filter.water      ? 0u : to_base(EntityFilter::WATER )) |
                (filter.sky        ? 0u : to_base(EntityFilter::SKY )) |
                (filter.particles  ? 0u : to_base(EntityFilter::PARTICLES )) |
                (filter.decals     ? 0u : to_base(EntityFilter::DECALS ));
    }

    void RenderPassCuller::PostCullNodes( const NodeCullParams& params, const U16 cullFlags, const U32 filterMask, VisibleNodeList<>& nodesInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );
       
        {
            PROFILE_SCOPE( "State cull", Profiler::Category::Scene );
            const I32 nodeCount = to_I32( nodesInOut.size() );
            for ( I32 i = nodeCount - 1; i >= 0; i-- )
            {
                const VisibleNode& node = nodesInOut.node( i );

                if ( Attorney::SceneGraphNodeRenderPassCuller::stateCullNode( node._node, params, cullFlags, filterMask, node._distanceToCameraSq ) == FrustumCollision::FRUSTUM_OUT )
                {
                    nodesInOut.remove( i );
                }
            }
        }

        if ( cullFlags & to_base(CullOptions::CULL_AGAINST_CLIPPING_PLANES ) )
        {
            PROFILE_SCOPE( "Clip cull", Profiler::Category::Scene );
            const I32 nodeCount = to_I32( nodesInOut.size() );
            for ( I32 i = nodeCount - 1; i >= 0; i-- )
            {
                const FrustumCollision collisionResult = Attorney::SceneGraphNodeRenderPassCuller::clippingCullNode( nodesInOut.node( i )._node, params );
                if ( collisionResult == FrustumCollision::FRUSTUM_OUT )
                {
                    nodesInOut.remove( i );
                }
            }
        }
    }

    void RenderPassCuller::FrustumCull( const NodeCullParams& params, U16 cullFlags, const SceneGraph& sceneGraph, const SceneState& sceneState, PlatformContext& context, VisibleNodeList<>& nodesOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        nodesOut.reset();

        if ( sceneState.renderState().isEnabledOption( SceneRenderState::RenderOptions::RENDER_GEOMETRY ) ||
             sceneState.renderState().isEnabledOption( SceneRenderState::RenderOptions::RENDER_WIREFRAME ) )
        {
            for ( const bool state : params._clippingPlanes.planeState() )
            {
                if ( state )
                {
                    cullFlags &= ~to_base(CullOptions::CULL_AGAINST_CLIPPING_PLANES);
                    break;
                }
            }

            const SceneGraphNode::ChildContainer& rootChildren = sceneGraph.getRoot()->getChildren();

            SharedLock<SharedMutex> r_lock( rootChildren._lock );
            const U32 iterCount = rootChildren._count.load();

            if ( iterCount > g_nodesPerCullingPartition * 2 )
            {
                ParallelForDescriptor descriptor = {};
                descriptor._iterCount = iterCount;
                descriptor._partitionSize = g_nodesPerCullingPartition;
                descriptor._priority = TaskPriority::DONT_CARE;
                descriptor._useCurrentThread = true;
                descriptor._cbk = [&]( const Task*, const U32 start, const U32 end )
                {
                    for ( U32 i = start; i < end; ++i )
                    {
                        FrustumCullNode( rootChildren._data[i], params, cullFlags, 0u, nodesOut );
                    }
                };
                parallel_for( context, descriptor );
            }
            else
            {
                for ( U32 i = 0u; i < iterCount; ++i )
                {
                    FrustumCullNode( rootChildren._data[i], params, cullFlags, 0u, nodesOut );
                };
            }
        }

        PostCullNodes( params, cullFlags, FilterMask( context ), nodesOut );
    }

    /// This method performs the visibility check on the given node and all of its children and adds them to the RenderQueue
    void RenderPassCuller::FrustumCullNode( SceneGraphNode* currentNode, const NodeCullParams& params, U16 cullFlags, U8 recursionLevel, VisibleNodeList<>& nodes )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        // We can manually exclude nodes by GUID, so check that
        if ( params._ignoredGUIDS._count > 0u )
        {
            // This is used, for example, by reflective nodes that should exclude themselves (mirrors, water, etc)
            const I64 nodeGUID = currentNode->getGUID();
            for ( size_t i = 0u; i < params._ignoredGUIDS._count; ++i )
            {
                if ( nodeGUID == params._ignoredGUIDS._guids[i] )
                {
                    return;
                }
            }
        }

        // Internal node cull (check against camera frustum and all that ...)
        F32 distanceSqToCamera = 0.0f;
        const FrustumCollision collisionResult = Attorney::SceneGraphNodeRenderPassCuller::frustumCullNode( currentNode, params, cullFlags, distanceSqToCamera );
        if ( collisionResult != FrustumCollision::FRUSTUM_OUT )
        {
            if ( !currentNode->hasFlag( SceneGraphNode::Flags::IS_CONTAINER ) )
            {
                // Only add non-container nodes to the visible list. Otherwise, proceed and check children
                nodes.append( { currentNode, distanceSqToCamera } );
            }

            // Parent node intersects the view, so check children
            if ( collisionResult == FrustumCollision::FRUSTUM_IN )
            {
                // If the parent node is all in, we don't need to frustum check the children, but we still
                // need to grab the distance to each of them
                cullFlags &= ~to_base(CullOptions::CULL_AGAINST_FRUSTUM);
            }

            SceneGraphNode::ChildContainer& children = currentNode->getChildren();

            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = children._count.load();

            if ( descriptor._iterCount > 0u )
            {
                SharedLock<SharedMutex> r_lock( children._lock );

                if ( descriptor._iterCount > g_nodesPerCullingPartition * 2 )
                {
                    descriptor._partitionSize = g_nodesPerCullingPartition;
                    descriptor._priority = recursionLevel < 2 ? TaskPriority::DONT_CARE : TaskPriority::REALTIME;
                    descriptor._useCurrentThread = true;
                    descriptor._cbk = [&]( const Task*, const U32 start, const U32 end )
                    {
                        for ( U32 i = start; i < end; ++i )
                        {
                            FrustumCullNode( children._data[i], params, cullFlags, recursionLevel + 1, nodes );
                        }
                    };
                    parallel_for( currentNode->context(), descriptor );
                }
                else
                {
                    for ( U32 i = 0u; i < descriptor._iterCount; ++i )
                    {
                        FrustumCullNode( children._data[i], params, cullFlags, recursionLevel + 1, nodes );
                    };
                }
            }
        }
    }

    void RenderPassCuller::FrustumCull( const PlatformContext& context, const NodeCullParams& params, const U16 cullFlags, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        nodesOut.reset();

        F32 distanceSqToCamera = F32_MAX;
        for ( SceneGraphNode* node : nodes )
        {
            if ( Attorney::SceneGraphNodeRenderPassCuller::frustumCullNode( node, params, cullFlags, distanceSqToCamera ) != FrustumCollision::FRUSTUM_OUT )
            {
                nodesOut.append( { node, distanceSqToCamera } );
            }
        }

        PostCullNodes( params, cullFlags, FilterMask( context ), nodesOut );
    }

    void RenderPassCuller::ToVisibleNodes(const Camera* camera, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        nodesOut.reset();

        const vec3<F32>& cameraEye = camera->snapshot()._eye;
        for ( SceneGraphNode* node : nodes )
        {
            BoundsComponent* bComp = node->get<BoundsComponent>();
            const F32 distanceSqToCamera = bComp != nullptr ? bComp->getBoundingSphere().getCenter().distanceSquared( cameraEye ) : F32_MAX;
            nodesOut.append( { node, distanceSqToCamera } );
        }
    }
}