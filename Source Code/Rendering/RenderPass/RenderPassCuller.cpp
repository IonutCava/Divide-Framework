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

    [[nodiscard]] bool isTransformNode(const SceneNodeType nodeType, const ObjectType objType) noexcept {
        return nodeType == SceneNodeType::TYPE_TRANSFORM || 
               nodeType == SceneNodeType::TYPE_TRIGGER || 
               objType  == ObjectType::MESH;
    }

    // Return true if this node should be removed from a shadow pass
    [[nodiscard]] bool doesNotCastShadows(const SceneGraphNode* node, const SceneNodeType sceneNodeType, const ObjectType objType) {
        if (sceneNodeType == SceneNodeType::TYPE_SKY ||
            sceneNodeType == SceneNodeType::TYPE_WATER ||
            sceneNodeType == SceneNodeType::TYPE_INFINITEPLANE ||
            objType       == ObjectType::DECAL)
        {
            return true;
        }

        const RenderingComponent* rComp = node->get<RenderingComponent>();
        assert(rComp != nullptr);
        return !rComp->renderOptionEnabled(RenderingComponent::RenderOptions::CAST_SHADOWS);
    }

    [[nodiscard]] bool shouldCullNode(const RenderStage stage, const SceneGraphNode* node, bool& isTransformNodeOut) {
        const SceneNode& sceneNode = node->getNode();
        const SceneNodeType snType = sceneNode.type();

        ObjectType objectType = ObjectType::COUNT;
        if (snType == SceneNodeType::TYPE_OBJECT3D) {
            objectType = static_cast<const Object3D&>(sceneNode).getObjectType();
        }

        if (node->hasFlag(SceneGraphNode::Flags::VISIBILITY_LOCKED)) {
            return false;
        }

        isTransformNodeOut = isTransformNode(snType, objectType);
        if (!isTransformNodeOut) {
            // only checks nodes and can return true for a shadow stage
            return stage == RenderStage::SHADOW && doesNotCastShadows(node, snType, objectType);
        }

        return true;
    }
}

bool RenderPassCuller::OnStartup([[maybe_unused]] PlatformContext& context) {
    return true;
}

bool RenderPassCuller::OnShutdown([[maybe_unused]] PlatformContext& context) {
    return true;
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

    if (params._stage == RenderStage::DISPLAY) {
        Attorney::SceneGraphNodeRenderPassCuller::visiblePostCulling(currentNode, false);
    }

    // Early out for inactive nodes
    if (currentNode->hasFlag(SceneGraphNode::Flags::ACTIVE)) {

        FrustumCollision collisionResult = FrustumCollision::FRUSTUM_OUT;
        const I64 nodeGUID = currentNode->getGUID();
        const I64* ignoredGUIDs = params._ignoredGUIDS.first;
        const size_t guidCount = params._ignoredGUIDS.second;
        for (size_t i = 0u; i < guidCount; ++i) {
            if (nodeGUID == ignoredGUIDs[i]) {
                return;
            }
        }

        // If it fails the culling test, stop
        bool isTransformNode = false;
        if (shouldCullNode(params._stage, currentNode, isTransformNode)) {
            if (isTransformNode) {
                collisionResult = FrustumCollision::FRUSTUM_INTERSECT;
            } else {
                return;
            }
        }

        // Internal node cull (check against camera frustum and all that ...)
        F32 distanceSqToCamera = 0.0f;
        if (isTransformNode || !Attorney::SceneGraphNodeRenderPassCuller::cullNode(currentNode, params, cullFlags, collisionResult, distanceSqToCamera)) {
            if (!isTransformNode) {
                VisibleNode node;
                node._node = currentNode;
                node._distanceToCameraSq = distanceSqToCamera;
                nodes.append(node);
            }
            if (params._stage == RenderStage::DISPLAY) {
                Attorney::SceneGraphNodeRenderPassCuller::visiblePostCulling(currentNode, true);
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
                addAllChildren(currentNode, params, cullFlags, nodes);
            }
        }
    }
}

void RenderPassCuller::addAllChildren(const SceneGraphNode* currentNode, const NodeCullParams& params, const U16 cullFlags, VisibleNodeList<>& nodes) const {
    OPTICK_EVENT();

    const SceneGraphNode::ChildContainer& children = currentNode->getChildren();
    SharedLock<SharedMutex> r_lock(children._lock);

    const U32 childCount = children._count.load();
    for (U32 i = 0u; i < childCount; ++i) {
        SceneGraphNode* child = children._data[i];
        if (params._stage == RenderStage::DISPLAY) {
            Attorney::SceneGraphNodeRenderPassCuller::visiblePostCulling(child, false);
        }

        if (!child->hasFlag(SceneGraphNode::Flags::ACTIVE)) {
            continue;
        }

        bool isTransformNode = false;
        if (!shouldCullNode(params._stage, child, isTransformNode)) {
            F32 distanceSqToCamera = std::numeric_limits<F32>::max();
            FrustumCollision collisionResult = FrustumCollision::FRUSTUM_OUT;
            U16 quickCullFlags = cullFlags;
            // Parent is already in frustum, so no need to check the children
            ClearBit(quickCullFlags, to_base(CullOptions::CULL_AGAINST_CLIPPING_PLANES));
            ClearBit(quickCullFlags, to_base(CullOptions::CULL_AGAINST_FRUSTUM));
            if (!Attorney::SceneGraphNodeRenderPassCuller::cullNode(child, params, cullFlags, collisionResult, distanceSqToCamera)) {
                VisibleNode node = {};
                node._node = child;
                node._distanceToCameraSq = distanceSqToCamera;
                nodes.append(node);
                if (params._stage == RenderStage::DISPLAY) {
                    Attorney::SceneGraphNodeRenderPassCuller::visiblePostCulling(child, true);
                }

                addAllChildren(child, params, cullFlags, nodes);
            }
        } else if (isTransformNode) {
            addAllChildren(child, params, cullFlags, nodes);
        }
    }
}

void RenderPassCuller::frustumCull(const NodeCullParams& params, const U16 cullFlags, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut) const {
    OPTICK_EVENT();

    nodesOut.reset();

    F32 distanceSqToCamera = std::numeric_limits<F32>::max();
    FrustumCollision collisionResult = FrustumCollision::FRUSTUM_OUT;
    for (SceneGraphNode* node : nodes) {
        if (params._stage == RenderStage::DISPLAY) {
            Attorney::SceneGraphNodeRenderPassCuller::visiblePostCulling(node, false);
        }
        // Internal node cull (check against camera frustum and all that ...)
        if (!Attorney::SceneGraphNodeRenderPassCuller::cullNode(node, params, cullFlags, collisionResult, distanceSqToCamera)) {
            nodesOut.append({ node, distanceSqToCamera });
            if (params._stage == RenderStage::DISPLAY) {
                Attorney::SceneGraphNodeRenderPassCuller::visiblePostCulling(node, false);
            }
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