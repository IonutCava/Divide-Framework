#include "stdafx.h"

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

namespace Divide {

namespace {
    constexpr U32 g_nodesPerCullingPartition = 8u;
}

void RenderPassCuller::clear() noexcept {
    for (VisibleNodeList<>& cache : _visibleNodes) {
        cache.reset();
    }
}

void RenderPassCuller::postCullNodes(const PlatformContext& context, const NodeCullParams& params, const U16 cullFlags, VisibleNodeList<>& nodesInOut) const {
    OPTICK_EVENT();
    {
        OPTICK_EVENT("Render filter cull");
        const auto removeNodeOfType = [](VisibleNodeList<>& nodes, const SceneNodeType snType, ObjectType objType = ObjectType::COUNT) {
            const I32 nodeCount = to_I32(nodes.size());
            for (I32 i = nodeCount - 1; i >= 0; i--) {
                const SceneGraphNode* node = nodes.node(i)._node;
                assert(node != nullptr);
                if (node->getNode<>().type() != snType) {
                    continue;
                }

                if (snType != SceneNodeType::TYPE_OBJECT3D || node->getNode<Object3D>().geometryType() == objType) {
                    nodes.remove(i);
                }
            }
        };

        const auto& filter = context.config().debug.renderFilter;
        if (!filter.primitives) {
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_OBJECT3D, ObjectType::BOX_3D);
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_OBJECT3D, ObjectType::SPHERE_3D);
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_OBJECT3D, ObjectType::QUAD_3D);
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_OBJECT3D, ObjectType::PATCH_3D);
        }
        if (!filter.meshes) {
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_OBJECT3D, ObjectType::MESH);
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_OBJECT3D, ObjectType::SUBMESH);
        }
        if (!filter.terrain) {
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_OBJECT3D, ObjectType::TERRAIN);
        }
        if (!filter.vegetation) {
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_VEGETATION);
        }
        if (!filter.water) {
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_WATER);
        }
        if (!filter.sky) {
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_SKY);
        }
        if (!filter.particles) {
            removeNodeOfType(nodesInOut, SceneNodeType::TYPE_PARTICLE_EMITTER);
        }
    }
    {
        OPTICK_EVENT("State cull");
        const I32 nodeCount = to_I32(nodesInOut.size());
        for (I32 i = nodeCount - 1; i >= 0; i--) {
            const FrustumCollision collisionResult = Attorney::SceneGraphNodeRenderPassCuller::stateCullNode(nodesInOut.node(i)._node, params, cullFlags, nodesInOut.node(i)._distanceToCameraSq);
            if (collisionResult == FrustumCollision::FRUSTUM_OUT) {
                nodesInOut.remove(i);
            }
        }
    }

    if (BitCompare(cullFlags, CullOptions::CULL_AGAINST_CLIPPING_PLANES)) {
        OPTICK_EVENT("Clip cull");
        const I32 nodeCount = to_I32(nodesInOut.size());
        for (I32 i = nodeCount - 1; i >= 0; i--) {
            const FrustumCollision collisionResult = Attorney::SceneGraphNodeRenderPassCuller::clippingCullNode(nodesInOut.node(i)._node, params);
            if (collisionResult == FrustumCollision::FRUSTUM_OUT) {
                nodesInOut.remove(i);
            }
        }
    }
}

void RenderPassCuller::frustumCull(const NodeCullParams& params, U16 cullFlags, const SceneGraph& sceneGraph, const SceneState& sceneState, PlatformContext& context, VisibleNodeList<>& nodesOut)
{
    OPTICK_EVENT();

    nodesOut.reset();

    if (sceneState.renderState().isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY) ||
        sceneState.renderState().isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME))
    {
        const bool clippingPlanesSet = std::any_of(begin(params._clippingPlanes.planeState()),
                                                   end(params._clippingPlanes.planeState()),
                                                   [](const bool x) { return x; });
        if (!clippingPlanesSet) {
            ClearBit(cullFlags, CullOptions::CULL_AGAINST_CLIPPING_PLANES);
        }

        const SceneGraphNode::ChildContainer& rootChildren = sceneGraph.getRoot()->getChildren();

        SharedLock<SharedMutex> r_lock(rootChildren._lock);
        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = rootChildren._count.load();

        if (descriptor._iterCount > g_nodesPerCullingPartition * 2) {

            descriptor._partitionSize = g_nodesPerCullingPartition;
            descriptor._priority = TaskPriority::DONT_CARE;
            descriptor._useCurrentThread = true;
            descriptor._cbk = [&](const Task*, const U32 start, const U32 end) {
                                for (U32 i = start; i < end; ++i) {
                                    frustumCullNode(rootChildren._data[i], params, cullFlags, 0u, nodesOut);
                                }
                            };
            parallel_for(context, descriptor);
        } else {
            for (U32 i = 0u; i < descriptor._iterCount; ++i){
                frustumCullNode(rootChildren._data[i], params, cullFlags, 0u, nodesOut);
            };
        }
    }

    postCullNodes(context, params, cullFlags, nodesOut);
}

/// This method performs the visibility check on the given node and all of its children and adds them to the RenderQueue
void RenderPassCuller::frustumCullNode(SceneGraphNode* currentNode, const NodeCullParams& params, U16 cullFlags, U8 recursionLevel, VisibleNodeList<>& nodes) const {
    OPTICK_EVENT();

    // We can manually exclude nodes by GUID, so check that
    if (params._ignoredGUIDS._count > 0u) {
        // This is used, for example, by reflective nodes that should exclude themselves (mirrors, water, etc)
        const I64 nodeGUID = currentNode->getGUID();
        for (size_t i = 0u; i < params._ignoredGUIDS._count; ++i) {
            if (nodeGUID == params._ignoredGUIDS._guids[i]) {
                return;
            }
        }
    }

    // Internal node cull (check against camera frustum and all that ...)
    F32 distanceSqToCamera = 0.0f;
    const FrustumCollision collisionResult = Attorney::SceneGraphNodeRenderPassCuller::frustumCullNode(currentNode, params, cullFlags, distanceSqToCamera);
    if (collisionResult != FrustumCollision::FRUSTUM_OUT) {
        if (!currentNode->hasFlag(SceneGraphNode::Flags::IS_CONTAINER)) {
            // Only add non-container nodes to the visible list. Otherwise, proceed and check children
            nodes.append({currentNode, distanceSqToCamera});
        }

        // Parent node intersects the view, so check children
        if (collisionResult == FrustumCollision::FRUSTUM_IN) {
            // If the parent node is all in, we don't need to frustum check the children, but we still
            // need to grab the distance to each of them
            ClearBit(cullFlags, to_base(CullOptions::CULL_AGAINST_FRUSTUM));
        }

        SceneGraphNode::ChildContainer& children = currentNode->getChildren();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = children._count.load();

        if (descriptor._iterCount > 0u) {
            SharedLock<SharedMutex> r_lock(children._lock);

            if (descriptor._iterCount > g_nodesPerCullingPartition * 2) {
                descriptor._partitionSize = g_nodesPerCullingPartition;
                descriptor._priority = recursionLevel < 2 ? TaskPriority::DONT_CARE : TaskPriority::REALTIME;
                descriptor._useCurrentThread = true;
                descriptor._cbk = [&](const Task*, const U32 start, const U32 end) {
                    for (U32 i = start; i < end; ++i) {
                        frustumCullNode(children._data[i], params, cullFlags, recursionLevel + 1, nodes);
                    }
                };
                parallel_for(currentNode->context(), descriptor);
            } else {
                for (U32 i = 0u; i < descriptor._iterCount; ++i) {
                    frustumCullNode(children._data[i], params, cullFlags, recursionLevel + 1, nodes);
                };
            }
        }
    }
}

void RenderPassCuller::frustumCull(const PlatformContext& context, const NodeCullParams& params, const U16 cullFlags, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut) const {
    OPTICK_EVENT();

    nodesOut.reset();

    F32 distanceSqToCamera = std::numeric_limits<F32>::max();
    for (SceneGraphNode* node : nodes) {
        if (Attorney::SceneGraphNodeRenderPassCuller::frustumCullNode(node, params, cullFlags, distanceSqToCamera) != FrustumCollision::FRUSTUM_OUT) {
            nodesOut.append({ node, distanceSqToCamera });
        }
    }

    postCullNodes(context, params, cullFlags, nodesOut);
}

void RenderPassCuller::toVisibleNodes([[maybe_unused]] const PlatformContext& context, const Camera* camera, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut) const {
    OPTICK_EVENT();

    nodesOut.reset();

    const vec3<F32>& cameraEye = camera->getEye();
    for (SceneGraphNode* node : nodes) {
        F32 distanceSqToCamera = std::numeric_limits<F32>::max();
        const BoundsComponent* bComp = node->get<BoundsComponent>();
        if (bComp != nullptr) {
            distanceSqToCamera = bComp->getBoundingSphere().getCenter().distanceSquared(cameraEye);
        }
        nodesOut.append({ node, distanceSqToCamera });
    }
}
}