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
    constexpr U32 g_nodesPerCullingPartition = 32u;
}

void RenderPassCuller::clear() noexcept {
    for (VisibleNodeList<>& cache : _visibleNodes) {
        cache.reset();
    }
}

VisibleNodeList<>& RenderPassCuller::frustumCull(const NodeCullParams& params, const U16 cullFlags, const SceneGraph& sceneGraph, const SceneState& sceneState, PlatformContext& context)
{
    OPTICK_EVENT();

    const RenderStage stage = params._stage;
    VisibleNodeList<>& nodeCache = getNodeCache(stage);
    nodeCache.reset();

    if (sceneState.renderState().isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY) ||
        sceneState.renderState().isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME))
    {
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
                                    frustumCullNode(rootChildren._data[i], params, cullFlags, 0u, nodeCache);
                                }
                            };
            parallel_for(context, descriptor);
        } else {
            for (U32 i = 0u; i < descriptor._iterCount; ++i){
                frustumCullNode(rootChildren._data[i], params, cullFlags, 0u, nodeCache);
            };
        }
    }

    const auto removeNodeOfType = [](VisibleNodeList<>& nodes, const SceneNodeType snType, ObjectType objType = ObjectType::COUNT) {
        const I32 nodeCount = to_I32(nodes.size());
        for (I32 i = nodeCount - 1; i >= 0; i--) {
            const SceneGraphNode* node = nodes.node(i)._node;
            if (node == nullptr) {
                // already culled
                continue;
            }

            if (node->getNode<>().type() != snType) {
                continue;
            }

            if (snType != SceneNodeType::TYPE_OBJECT3D || node->getNode<Object3D>().getObjectType() == objType) {
                nodes.node(i)._node = nullptr;
            }
        }
    };

    const auto& filter = context.config().debug.renderFilter;
    if (!filter.primitives) {
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_OBJECT3D, ObjectType::BOX_3D);
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_OBJECT3D, ObjectType::SPHERE_3D);
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_OBJECT3D, ObjectType::QUAD_3D);
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_OBJECT3D, ObjectType::PATCH_3D);
    }
    if (!filter.meshes) {
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_OBJECT3D, ObjectType::MESH);
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_OBJECT3D, ObjectType::SUBMESH);
    }
    if (!filter.terrain) {
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_OBJECT3D, ObjectType::TERRAIN);
    }
    if (!filter.vegetation) {
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_VEGETATION);
    }
    if (!filter.water) {
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_WATER);
    }
    if (!filter.sky) {
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_SKY);
    }
    if (!filter.particles) {
        removeNodeOfType(nodeCache, SceneNodeType::TYPE_PARTICLE_EMITTER);
    }
    return nodeCache;
}

/// This method performs the visibility check on the given node and all of its children and adds them to the RenderQueue
void RenderPassCuller::frustumCullNode(SceneGraphNode* currentNode, const NodeCullParams& params, const U16 cullFlags, U8 recursionLevel, VisibleNodeList<>& nodes) const {
    OPTICK_EVENT();

    // We can manually exclude nodes by GUID, so check that
    if (params._ignoredGUIDS.second > 0u) {
        // This is used, for example, by reflective nodes that should exclude themselves (mirrors, water, etc)
        const I64 nodeGUID = currentNode->getGUID();
        for (size_t i = 0u; i < params._ignoredGUIDS.second; ++i) {
            if (nodeGUID == params._ignoredGUIDS.first[i]) {
                return;
            }
        }
    }

    // Internal node cull (check against camera frustum and all that ...)
    F32 distanceSqToCamera = 0.0f;
    const FrustumCollision collisionResult = Attorney::SceneGraphNodeRenderPassCuller::cullNode(currentNode, params, cullFlags, distanceSqToCamera);
    if (collisionResult != FrustumCollision::FRUSTUM_OUT) {
        if (!SceneGraphNode::IsContainerNode(*currentNode)) {
            // Only add non-container nodes to the visible list. Otherwise, proceed and check children
            nodes.append({currentNode, distanceSqToCamera});
        }

        // Parent node intersects the view, so check children
        if (collisionResult == FrustumCollision::FRUSTUM_INTERSECT) {
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
        } else {
            // All nodes are in view entirely
            U16 quickCullFlags = cullFlags; // Parent is already in frustum, so no need to check the children
            ClearBit(quickCullFlags, to_base(CullOptions::CULL_AGAINST_CLIPPING_PLANES));
            ClearBit(quickCullFlags, to_base(CullOptions::CULL_AGAINST_FRUSTUM));
            NodeCullParams nodeChildParams = params;
            nodeChildParams._skipBoundsChecking = true;
            addAllChildren(currentNode, nodeChildParams, quickCullFlags, nodes);
        }
    }
}

void RenderPassCuller::addAllChildren(const SceneGraphNode* currentNode, const NodeCullParams& params, const U16 cullFlags, VisibleNodeList<>& nodes) const {
    OPTICK_EVENT();

    const SceneGraphNode::ChildContainer& children = currentNode->getChildren();
    U32 childCount = children._count.load();

    if (childCount == 0u) {
        return;
    }

    SharedLock<SharedMutex> r_lock(children._lock);
    childCount = children._count.load(); //double check
    for (U32 i = 0u; i < childCount; ++i) {
        SceneGraphNode* child = children._data[i];

        bool visible = false;
        if (!SceneGraphNode::IsContainerNode(*child)) {
            F32 distanceSqToCamera = std::numeric_limits<F32>::max();
            if (Attorney::SceneGraphNodeRenderPassCuller::cullNode(child, params, cullFlags, distanceSqToCamera) != FrustumCollision::FRUSTUM_OUT) {
                nodes.append({child, distanceSqToCamera });
                visible = true;
            }
        } else {
            visible = Attorney::SceneGraphNodeRenderPassCuller::cullNode(child, params, cullFlags) != FrustumCollision::FRUSTUM_OUT;
        }

        if (visible) {
            addAllChildren(child, params, cullFlags, nodes);
        }
    }
}

void RenderPassCuller::frustumCull(const NodeCullParams& params, const U16 cullFlags, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut) const {
    OPTICK_EVENT();

    nodesOut.reset();

    F32 distanceSqToCamera = std::numeric_limits<F32>::max();
    for (SceneGraphNode* node : nodes) {
        if (Attorney::SceneGraphNodeRenderPassCuller::cullNode(node, params, cullFlags, distanceSqToCamera) != FrustumCollision::FRUSTUM_OUT) {
            nodesOut.append({ node, distanceSqToCamera });
        }
    }
}

void RenderPassCuller::toVisibleNodes(const Camera* camera, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut) const {
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