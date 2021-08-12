#include "stdafx.h"

#include "Headers/SceneGraphNode.h"
#include "Headers/SceneGraph.h"

#include "Core/Headers/PlatformContext.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Water/Headers/Water.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Scenes/Headers/SceneState.h"

#include "Editor/Headers/Editor.h"

#include "ECS/Systems/Headers/ECSManager.h"

#include "ECS/Components/Headers/AnimationComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/NetworkingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"


namespace Divide {

namespace {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    bool PropagateFlagToChildren(const SceneGraphNode::Flags flag) noexcept {
        return flag == SceneGraphNode::Flags::SELECTED || 
               flag == SceneGraphNode::Flags::HOVERED ||
               flag == SceneGraphNode::Flags::ACTIVE ||
               flag == SceneGraphNode::Flags::VISIBILITY_LOCKED;
    }
};

SceneGraphNode::SceneGraphNode(SceneGraph* sceneGraph, const SceneGraphNodeDescriptor& descriptor)
    : GUIDWrapper(),
      PlatformContextComponent(sceneGraph->parentScene().context()),
      _relationshipCache(this),
      _sceneGraph(sceneGraph),
      _node(descriptor._node),
      _compManager(sceneGraph->GetECSEngine().GetComponentManager()),
      _instanceCount(to_U32(descriptor._instanceCount)),
      _serialize(descriptor._serialize),
      _usageContext(descriptor._usageContext)
{
    std::atomic_init(&_childCount, 0u);
    for (auto& it : Events._eventsFreeList) {
        std::atomic_init(&it, true);
    }
    _name = descriptor._name.empty() ? Util::StringFormat("%s_SGN", _node->resourceName().empty() ? "ERROR"   
                                                                        : _node->resourceName().c_str()).c_str()
                : descriptor._name;

    setFlag(Flags::ACTIVE);
    clearFlag(Flags::VISIBILITY_LOCKED);

    if (_node == nullptr) {
        _node = std::make_shared<SceneNode>(sceneGraph->parentScene().resourceCache(),
                                              generateGUID(),
                                              "EMPTY",
                                              ResourcePath{"EMPTY"},
                                              ResourcePath{},
                                              SceneNodeType::TYPE_TRANSFORM,
                                              to_base(ComponentType::TRANSFORM));
    }

    if (_node->type() == SceneNodeType::TYPE_TRANSFORM) {
        _node->load();
    }

    assert(_node != nullptr);

    AddComponents(descriptor._componentMask, false);
    AddComponents(_node->requiredComponentMask(), false);

    Attorney::SceneNodeSceneGraph::registerSGNParent(_node.get(), this);
}

/// If we are destroying the current graph node
SceneGraphNode::~SceneGraphNode()
{
    Console::printfn(Locale::Get(_ID("REMOVE_SCENEGRAPH_NODE")), name().c_str(), _node->resourceName().c_str());

    Attorney::SceneGraphSGN::onNodeDestroy(_sceneGraph, this);
    Attorney::SceneNodeSceneGraph::unregisterSGNParent(_node.get(), this);
    if (Attorney::SceneNodeSceneGraph::parentCount(_node.get()) == 0) {
        assert(_node.use_count() <= Attorney::SceneNodeSceneGraph::maxReferenceCount(_node.get()));

        _node.reset();
    }

    // Bottom up
    for (U32 i = 0; i < getChildCount(); ++i) {
        _sceneGraph->destroySceneGraphNode(_children[i]);
    }
    _children.clear();
    //_childCount.store(0u);

    _compManager->RemoveAllComponents(GetEntityID());
}

ECS::ECSEngine& SceneGraphNode::GetECSEngine() const noexcept {
    return _sceneGraph->GetECSEngine();
}

void SceneGraphNode::AddComponents(const U32 componentMask, const bool allowDuplicates) {

    for (auto i = 1u; i < to_base(ComponentType::COUNT) + 1; ++i) {
        const U32 componentBit = 1 << i;

        // Only add new components;
        if (BitCompare(componentMask, componentBit) && (allowDuplicates || !BitCompare(_componentMask, componentBit))) {
            _componentMask |= componentBit;
            SGNComponent::construct(static_cast<ComponentType>(componentBit), this);
        }
    };
}

void SceneGraphNode::RemoveComponents(const U32 componentMask) {
    for (auto i = 1u; i < to_base(ComponentType::COUNT) + 1; ++i) {
        const U32 componentBit = 1 << i;
        if (BitCompare(componentMask, componentBit) && BitCompare(_componentMask, componentBit)) {
            SGNComponent::destruct(static_cast<ComponentType>(componentBit), this);
        }
    }
}

void SceneGraphNode::setTransformDirty(const U32 transformMask) {
    SharedLock<SharedMutex> r_lock(_childLock);
    for (SceneGraphNode* node : _children) {
        TransformComponent* tComp = node->get<TransformComponent>();
        if (tComp != nullptr) {
            Attorney::TransformComponentSGN::onParentTransformDirty(*tComp, transformMask);
        }
    }
}

void SceneGraphNode::changeUsageContext(const NodeUsageContext& newContext) {
    _usageContext = newContext;

    TransformComponent* tComp = get<TransformComponent>();
    if (tComp) {
        Attorney::TransformComponentSGN::onParentUsageChanged(*tComp, _usageContext);
    }

    RenderingComponent* rComp = get<RenderingComponent>();
    if (rComp) {
        Attorney::RenderingComponentSGN::onParentUsageChanged(*rComp, _usageContext);
    }
}

/// Change current SceneGraphNode's parent
void SceneGraphNode::setParent(SceneGraphNode* parent, const bool defer) {
    _queuedNewParent = parent->getGUID();
    if (!defer) {
        setParentInternal();
    }
}

void SceneGraphNode::setParentInternal() {
    if (_queuedNewParent == -1) {
        return;
    }
    SceneGraphNode* newParent = sceneGraph()->findNode(_queuedNewParent);
    _queuedNewParent = -1;

    if (newParent == nullptr) {
        return;
    }

    assert(newParent->getGUID() != getGUID());
    
    { //Clear old parent
        if (_parent != nullptr) {
            if (_parent->getGUID() == newParent->getGUID()) {
                return;
            }

            // Remove us from the old parent's children map
            _parent->removeChildNode(this, false, false);
        }
    }
    // Set the parent pointer to the new parent
    _parent = newParent;

    {// Add ourselves in the new parent's children map
        {
            _parent->_childCount.fetch_add(1);
            ScopedLock<SharedMutex> w_lock(_parent->_childLock);
            _parent->_children.push_back(this);
        }
        Attorney::SceneGraphSGN::onNodeAdd(_sceneGraph, this);
        // That's it. Parent Transforms will be updated in the next render pass;
        _parent->invalidateRelationshipCache();
    }
    {// Carry over new parent's flags and settings
        constexpr Flags flags[] = { Flags::SELECTED, Flags::HOVERED, Flags::ACTIVE, Flags::VISIBILITY_LOCKED };
        for (Flags flag : flags) {
            if (_parent->hasFlag(flag)) {
                setFlag(flag);
            } else {
                clearFlag(flag);
            }
        }

        // Dynamic > Static. Not the other way around (e.g. Root is static. Terrain is static, etc)
        if (_parent->usageContext() == NodeUsageContext::NODE_DYNAMIC) {
            changeUsageContext(_parent->usageContext());
        }
    }
}

/// Add a new SceneGraphNode to the current node's child list based on a SceneNode
SceneGraphNode* SceneGraphNode::addChildNode(const SceneGraphNodeDescriptor& descriptor) {
    // Create a new SceneGraphNode with the SceneNode's info
    // We need to name the new SceneGraphNode
    // If we did not supply a custom name use the SceneNode's name

    SceneGraphNode* sceneGraphNode = _sceneGraph->createSceneGraphNode(_sceneGraph, descriptor);
    assert(sceneGraphNode != nullptr && sceneGraphNode->_node->getState() != ResourceState::RES_CREATED);

    // Set the current node as the new node's parent
    sceneGraphNode->setParent(this);

    if (sceneGraphNode->_node->getState() == ResourceState::RES_LOADED) {
        PostLoad(sceneGraphNode->_node.get(), sceneGraphNode);
    } else if (sceneGraphNode->_node->getState() == ResourceState::RES_LOADING) {
        setFlag(Flags::LOADING);
        sceneGraphNode->_node->addStateCallback(ResourceState::RES_LOADED,
            [this, sceneGraphNode](CachedResource* res) {
                PostLoad(static_cast<SceneNode*>(res), sceneGraphNode);
                clearFlag(Flags::LOADING);
            }
        );
    }
    // return the newly created node
    return sceneGraphNode;
}

void SceneGraphNode::PostLoad(SceneNode* sceneNode, SceneGraphNode* sgn) {
    Attorney::SceneNodeSceneGraph::postLoad(sceneNode, sgn);
    sgn->Hacks._editorComponents.emplace_back(&Attorney::SceneNodeSceneGraph::getEditorComponent(sceneNode));
    if (!sgn->_relationshipCache.isValid()) {
        sgn->_relationshipCache.rebuild();
    }
}

bool SceneGraphNode::removeNodesByType(SceneNodeType nodeType) {
    // Bottom-up pattern
    U32 removalCount = 0, childRemovalCount = 0;
    forEachChild([nodeType, &childRemovalCount](SceneGraphNode* child, I32 /*childIdx*/) {
        if (child->removeNodesByType(nodeType)) {
            ++childRemovalCount;
        }
        return true;
    });

    {
        SharedLock<SharedMutex> r_lock(_childLock);
        const U32 count = to_U32(_children.size());
        for (U32 i = 0; i < count; ++i) {
            if (_children[i]->getNode().type() == nodeType) {
                _sceneGraph->addToDeleteQueue(this, i);
                ++removalCount;
            }
        }
    }

    if (removalCount > 0) {
        return true;
    }

    return childRemovalCount > 0;
}

bool SceneGraphNode::removeChildNode(const SceneGraphNode* node, const bool recursive, bool deleteNode) {

    const I64 targetGUID = node->getGUID();
    {
        SharedLock<SharedMutex> r_lock(_childLock);
        const U32 count = to_U32(_children.size());
        for (U32 i = 0; i < count; ++i) {
            if (_children[i]->getGUID() == targetGUID) {
                if (deleteNode) {
                    _sceneGraph->addToDeleteQueue(this, i);
                } else {
                    _children.erase(_children.begin() + i);
                    _childCount.fetch_sub(1);
                }
                return true;
            }
        }
    }

    // If this didn't finish, it means that we found our node
    return !recursive || 
           !forEachChild([&node, deleteNode](SceneGraphNode* child, I32 /*childIdx*/) {
                if (child->removeChildNode(node, true, deleteNode)) {
                    return false;
                }
                return true;
            });
}

void SceneGraphNode::postLoad() {
    SendEvent(
    {
        ECS::CustomEvent::Type::EntityPostLoad
    });
}

bool SceneGraphNode::isChildOfType(const U16 typeMask) const {
    SceneGraphNode* parentNode = parent();
    while (parentNode != nullptr) {
        if (BitCompare(typeMask, to_base(parentNode->getNode<>().type()))) {
            return true;
        }
        parentNode = parentNode->parent();
    }

    return false;
}

bool SceneGraphNode::isRelated(const SceneGraphNode* target) const {
    const I64 targetGUID = target->getGUID();
    // We also ignore grandparents as this will usually be the root;
    if (_relationshipCache.isValid()) {
        return _relationshipCache.classifyNode(targetGUID) != SGNRelationshipCache::RelationshipType::COUNT;
    }

    return false;
}

bool SceneGraphNode::isChild(const SceneGraphNode* target, const bool recursive) const {
    const I64 targetGUID = target->getGUID();

    const SGNRelationshipCache::RelationshipType type = _relationshipCache.classifyNode(targetGUID);
    if (type == SGNRelationshipCache::RelationshipType::GRANDCHILD && recursive) {
        return true;
    }

    return type == SGNRelationshipCache::RelationshipType::CHILD;
}

SceneGraphNode* SceneGraphNode::findChild(const U64 nameHash, const bool sceneNodeName, const bool recursive) const {
    SharedLock<SharedMutex> r_lock(_childLock);
    for (auto& child : _children) {
        const U64 cmpHash = sceneNodeName ? _ID(child->getNode().resourceName().c_str()) : _ID(child->name().c_str());
        if (cmpHash == nameHash) {
            return child;
        }
        if (recursive) {
            SceneGraphNode* recChild = child->findChild(nameHash, sceneNodeName, recursive);
            if (recChild != nullptr) {
                return recChild;
            }
        }
    }

    // no child's name matches or there are no more children
    // so return nullptr, indicating that the node was not found yet
    return nullptr;
}

SceneGraphNode* SceneGraphNode::findChild(const I64 GUID, const bool sceneNodeGuid, const bool recursive) const {
    if (GUID != -1) {
        SharedLock<SharedMutex> r_lock(_childLock);
        for (auto& child : _children) {
            if (sceneNodeGuid ? child->getNode().getGUID() == GUID : child->getGUID() == GUID) {
                return child;
            }
            if (recursive) {
                SceneGraphNode* recChild = child->findChild(GUID, sceneNodeGuid, true);
                if (recChild != nullptr) {
                    return recChild;
                }
            }
        }
    }
    // no child's name matches or there are no more children
    // so return nullptr, indicating that the node was not found yet
    return nullptr;
}

bool SceneGraphNode::intersect(const Ray& intersectionRay, const vec2<F32>& range, vectorEASTL<SGNRayResult>& intersections) const {
    vectorEASTL<SGNRayResult> ret = {};

    // Root has its own intersection routine, so we ignore it
    if (_sceneGraph->getRoot()->getGUID() == this->getGUID()) {
        forEachChild([&](const SceneGraphNode* child, I32 /*childIdx*/) {
            child->intersect(intersectionRay, range, intersections);
            return true;
        });
    } else {
        // If we hit a bounding sphere, we proceed to the more expensive OBB test
        if (get<BoundsComponent>()->getBoundingSphere().intersect(intersectionRay, range.min, range.max).hit) {
            const RayResult result = get<BoundsComponent>()->getOBB().intersect(intersectionRay, range.min, range.max);
            if (result.hit) {
                intersections.push_back({ getGUID(), result.dist, name().c_str() });
                forEachChild([&](const SceneGraphNode* child, I32 /*childIdx*/) {
                    child->intersect(intersectionRay, range, intersections);
                    return true;
                });
            }
        }
    }

    return !intersections.empty();
}

void SceneGraphNode::getAllNodes(vectorEASTL<SceneGraphNode*>& nodeList) {
    // Compute from leaf to root to ensure proper calculations
    {
        SharedLock<SharedMutex> r_lock(_childLock);
        for (auto& child : _children) {
            child->getAllNodes(nodeList);
        }
    }

    nodeList.push_back(this);
}

void SceneGraphNode::processDeleteQueue(vectorEASTL<size_t>& childList) {
    // See if we have any children to delete
    if (!childList.empty()) {
        ScopedLock<SharedMutex> w_lock(_childLock);
        for (const size_t childIdx : childList) {
            _sceneGraph->destroySceneGraphNode(_children[childIdx]);
        }
        EraseIndices(_children, childList);
        _childCount.store(to_U32(_children.size()));
    }
}

/// Please call in MAIN THREAD! Nothing is thread safe here (for now) -Ionut
void SceneGraphNode::sceneUpdate(const U64 deltaTimeUS, SceneState& sceneState) {
    OPTICK_EVENT();

    setParentInternal();

    // update local time
    _elapsedTimeUS += deltaTimeUS;

    if (hasFlag(Flags::ACTIVE)) {
        if (_lockToCamera != 0) {
            TransformComponent* tComp = get<TransformComponent>();
            if (tComp) {
                Camera* cam = Camera::findCamera(_lockToCamera);
                if (cam) {
                    cam->updateLookAt();
                    tComp->setOffset(true, cam->worldMatrix());
                }
            }
        }

        Attorney::SceneNodeSceneGraph::sceneUpdate(_node.get(), deltaTimeUS, this, sceneState);
    }

    if (hasFlag(Flags::PARENT_POST_RENDERED)) {
        clearFlag(Flags::PARENT_POST_RENDERED);
    }

    if (get<RenderingComponent>() == nullptr) {
        BoundsComponent* bComp = get<BoundsComponent>();
        if (bComp->showAABB()) {
            const BoundingBox& bb = bComp->getBoundingBox();
            _context.gfx().debugDrawBox(bb.getMin(), bb.getMax(), DefaultColours::WHITE);
        }
    }
}

void SceneGraphNode::processEvents() {
    OPTICK_EVENT();
    const ECS::EntityId id = GetEntityID();
    
    for (size_t idx = 0; idx < Events.EVENT_QUEUE_SIZE; ++idx) {
        if (!Events._eventsFreeList[idx].exchange(true)) {
            const ECS::CustomEvent& evt = Events._events[idx];

            switch (evt._type) {
                case ECS::CustomEvent::Type::RelationshipCacheInvalidated: {
                    if (!_relationshipCache.isValid()) {
                        _relationshipCache.rebuild();
                    }
                } break;
                case ECS::CustomEvent::Type::EntityFlagChanged: {
                    if (static_cast<Flags>(evt._flag) == Flags::SELECTED) {
                        RenderingComponent* rComp = get<RenderingComponent>();
                        if (rComp != nullptr) {
                            const bool state = evt._dataFirst == 1u;
                            const bool recursive = evt._dataSecond == 1u;
                            rComp->toggleRenderOption(RenderingComponent::RenderOptions::RENDER_SELECTION, state, recursive);
                        }
                    }
                } break;
                case ECS::CustomEvent::Type::BoundsUpdated: {
                    Attorney::SceneGraphSGN::onNodeMoved(_sceneGraph, *this);
                }break;
                case ECS::CustomEvent::Type::NewShaderReady: {
                    Attorney::SceneGraphSGN::onNodeShaderReady(_sceneGraph, *this);
                } break;
                default: break;
            }

            _compManager->PassDataToAllComponents(id, evt);
        }
    }
}

void SceneGraphNode::prepareRender(RenderingComponent& rComp, const RenderStagePass& renderStagePass, const Camera& camera, const bool refreshData) {
    OPTICK_EVENT();

    AnimationComponent* aComp = get<AnimationComponent>();
    if (aComp) {
        RenderPackage& pkg = rComp.getDrawPackage(renderStagePass);
        {
            const AnimationComponent::AnimData data = aComp->getAnimationData();
            if (data._boneBuffer != nullptr) {
                ShaderBufferBinding bufferBinding;
                bufferBinding._binding = ShaderBufferLocation::BONE_TRANSFORMS;
                bufferBinding._buffer = data._boneBuffer;
                bufferBinding._elementRange = data._boneBufferRange;
                pkg.get<GFX::BindDescriptorSetsCommand>(0)->_set._buffers.add(bufferBinding);
            }
            if (data._prevBoneBufferRange.max > 0) {
                ShaderBufferBinding bufferBinding;
                bufferBinding._binding = ShaderBufferLocation::BONE_TRANSFORMS_PREV;
                bufferBinding._buffer = data._boneBuffer;
                bufferBinding._elementRange = data._prevBoneBufferRange;
                pkg.get<GFX::BindDescriptorSetsCommand>(0)->_set._buffers.add(bufferBinding);
            }
        }
    }

    _node->prepareRender(this, rComp, renderStagePass, camera, refreshData);
}

void SceneGraphNode::onNetworkSend(U32 frameCount) const {
    forEachChild([frameCount](SceneGraphNode* child, I32 /*childIdx*/) {
        child->onNetworkSend(frameCount);
        return true;
    });

    NetworkingComponent* net = get<NetworkingComponent>();
    if (net) {
        net->onNetworkSend(frameCount);
    }
}

bool SceneGraphNode::canDraw(const RenderStagePass& stagePass) const {
    RenderingComponent* rComp = get<RenderingComponent>();

    return rComp != nullptr && rComp->canDraw(stagePass);
}

// Returns true if the node SHOULD be culled!
bool SceneGraphNode::cullNode(const NodeCullParams& params,
                              const U16 cullFlags,
                              FrustumCollision& collisionTypeOut,
                              F32& distanceToClosestPointSQ) const {
    OPTICK_EVENT();

    distanceToClosestPointSQ = std::numeric_limits<F32>::max();
    collisionTypeOut = FrustumCollision::FRUSTUM_OUT;

    // If the node is still loading, DO NOT RENDER IT. Bad things happen :D
    if (hasFlag(Flags::LOADING)) {
        return true;
    }

    const SceneNodeRenderState nodeRenderState = _node->renderState();

    // Drawing is disabled in general for this node
    if (!nodeRenderState.drawState()) {
        return true;
    }

    // Some nodes should always render for different reasons (eg, trees are instanced and bound to the parent chunk)
    if (hasFlag(Flags::VISIBILITY_LOCKED)) {
        if (nodeRenderState.drawState(RenderStagePass{ params._stage, RenderPassType::COUNT })) {
            collisionTypeOut = FrustumCollision::FRUSTUM_IN;
            const vec3<F32>& eye = params._currentCamera->getEye();
            const BoundingSphere& boundingSphere = get<BoundsComponent>()->getBoundingSphere();
            distanceToClosestPointSQ = boundingSphere.getCenter().distanceSquared(eye) - SQUARED(boundingSphere.getRadius());
            return false;
        }

        return true;
    }

    const bool isStaticNode = usageContext() == NodeUsageContext::NODE_STATIC;
    if (BitCompare(cullFlags, CullOptions::CULL_STATIC_NODES) && isStaticNode) {
        return true;
    }
    if (BitCompare(cullFlags, CullOptions::CULL_DYNAMIC_NODES) && !isStaticNode) {
        return true;
    }

    const BoundsComponent* bComp = get<BoundsComponent>();
    const BoundingSphere& boundingSphere = bComp->getBoundingSphere();
    const BoundingBox& boundingBox = bComp->getBoundingBox();
    const vec3<F32>& bSphereCenter = boundingSphere.getCenter();
    const F32 radius = boundingSphere.getRadius();

    STUBBED("ToDo: make this work in a multi-threaded environment -Ionut");
    _frustPlaneCache = -1;
    I8 fakePlaneCache = -1;

    // Get camera info
    const vec3<F32>& eye = params._currentCamera->getEye();
    {
        OPTICK_EVENT("cullNode - Bounding Sphere Distance Test");
        distanceToClosestPointSQ = bSphereCenter.distanceSquared(eye) - SQUARED(radius);
        if (distanceToClosestPointSQ > params._cullMaxDistanceSq) {
            // Node is too far away
            return true;
        }
    }
    {
        OPTICK_EVENT("cullNode - Bounding Box Distance Test");
        distanceToClosestPointSQ = boundingBox.nearestPoint(eye).distanceSquared(eye);
        if (distanceToClosestPointSQ > params._cullMaxDistanceSq) {
            // Check again using the AABB
            return true;
        }

        const F32 upperBound = params._minExtents.maxComponent();
        if (upperBound > 0.0f && boundingBox.getExtent().maxComponent() < upperBound) {
            // Node is too small for the current render pass
            return true;
        }

    }

    if (BitCompare(cullFlags, CullOptions::CULL_AGAINST_CLIPPING_PLANES)) {
        OPTICK_EVENT("cullNode - Bounding Sphere - Clipping Planes Test");
        auto& planes = params._clippingPlanes.planes();
        auto& states = params._clippingPlanes.planeState();
        for (U8 i = 0u; i < to_U8(ClipPlaneIndex::COUNT); ++i) {
            if (states[i]) {
                collisionTypeOut = PlaneBoundingSphereIntersect(planes[i], boundingSphere);
                if (collisionTypeOut == FrustumCollision::FRUSTUM_OUT) {
                    // Fails the clipping plane test
                    return true;
                }
            }
        }
    }

    if (BitCompare(cullFlags, CullOptions::CULL_AGAINST_FRUSTUM)) {
        OPTICK_EVENT("cullNode - Bounding Sphere & Box Frustum Test");
        // Sphere is in range, so check bounds primitives against the frustum
        if (!boundingBox.containsPoint(eye)) {
            const Frustum& frustum = params._currentCamera->getFrustum();
            // Check if the bounding sphere is in the frustum, as Frustum <-> Sphere check is fast
            collisionTypeOut = frustum.ContainsSphere(bSphereCenter, radius, fakePlaneCache);
            if (collisionTypeOut == FrustumCollision::FRUSTUM_INTERSECT) {
                // If the sphere is not completely in the frustum, check the AABB
                collisionTypeOut = frustum.ContainsBoundingBox(boundingBox, fakePlaneCache);
            }
        } else {
            // We are inside the AABB. So ... intersect?
            collisionTypeOut = FrustumCollision::FRUSTUM_INTERSECT;
        }
    } else {
        collisionTypeOut = FrustumCollision::FRUSTUM_INTERSECT;
    }

    if (collisionTypeOut != FrustumCollision::FRUSTUM_OUT && BitCompare(cullFlags, CullOptions::CULL_AGAINST_LOD)) {
        OPTICK_EVENT("cullNode - LoD check")

        RenderingComponent* rComp = get<RenderingComponent>();
        const vec2<F32>& renderRange = rComp->renderRange();
        const F32 minDistanceSQ = SQUARED(renderRange.min) * (renderRange.min < 0.f ? -1.f : 1.f); //Keep the sign. Might need it for rays or shadows.
        const F32 maxDistanceSQ = SQUARED(renderRange.max);

        if (!IS_IN_RANGE_INCLUSIVE(distanceToClosestPointSQ, minDistanceSQ, maxDistanceSQ)) {
            collisionTypeOut = FrustumCollision::FRUSTUM_OUT;
        } else {
            // We are in range, so proceed to LoD checks
            const U8 LoDLevel = rComp->getLoDLevel(distanceToClosestPointSQ, params._stage, params._lodThresholds);
            if ((params._maxLoD > -1 && LoDLevel > params._maxLoD) || LoDLevel > nodeRenderState.maxLodLevel()) {
                collisionTypeOut = FrustumCollision::FRUSTUM_OUT;
            }
        }
    }

    return collisionTypeOut == FrustumCollision::FRUSTUM_OUT;
}

void SceneGraphNode::invalidateRelationshipCache(SceneGraphNode* source) {
    if (source == this || !_relationshipCache.isValid()) {
        return;
    }

    SendEvent(
    {
        ECS::CustomEvent::Type::RelationshipCacheInvalidated
    });

    _relationshipCache.invalidate();

    if (_parent && _parent->parent()) {
        _parent->invalidateRelationshipCache(this);

        forEachChild([this, source](SceneGraphNode* child, I32 /*childIdx*/) {
            if (!source || child->getGUID() != source->getGUID()) {
                child->invalidateRelationshipCache(this);
            }
            return true;
        });
    }
}

bool SceneGraphNode::saveCache(ByteBuffer& outputBuffer) const {
    outputBuffer << BYTE_BUFFER_VERSION;

    if (getNode().saveCache(outputBuffer)) {

        for (EditorComponent* editorComponent : Hacks._editorComponents) {
            if (!Attorney::EditorComponentSceneGraphNode::saveCache(*editorComponent, outputBuffer)) {
                return false;
            }
        }

        return _sceneGraph->GetECSManager().saveCache(this, outputBuffer);
    }

    return false;
}

bool SceneGraphNode::loadCache(ByteBuffer& inputBuffer) {
    U16 tempVer = 0u;
    inputBuffer >> tempVer;
    if (tempVer == BYTE_BUFFER_VERSION) {
        if (getNode().loadCache(inputBuffer)) {
            for (EditorComponent* editorComponent : Hacks._editorComponents) {
                if (!Attorney::EditorComponentSceneGraphNode::loadCache(*editorComponent, inputBuffer)) {
                    return false;
                }
            }

            return _sceneGraph->GetECSManager().loadCache(this, inputBuffer);
        }
    }

    return false;
}

void SceneGraphNode::saveToXML(const Str256& sceneLocation, DELEGATE<void, std::string_view> msgCallback) const {
    if (!serialize()) {
        return;
    }

    if (msgCallback) {
        msgCallback(Util::StringFormat("Saving node [ %s ] ...", name().c_str()).c_str());
    }

    boost::property_tree::ptree pt;
    pt.put("static", usageContext() == NodeUsageContext::NODE_STATIC);

    getNode().saveToXML(pt);

    for (EditorComponent* editorComponent : Hacks._editorComponents) {
        Attorney::EditorComponentSceneGraphNode::saveToXML(*editorComponent, pt);
    }

    auto targetFile = sceneLocation + "/nodes/";
    targetFile.append(parent()->name());
    targetFile.append("_");
    targetFile.append(name());
    targetFile.append(".xml");
    XML::writeXML(targetFile.c_str(), pt);

    forEachChild([&sceneLocation, &msgCallback](const SceneGraphNode* child, I32 /*childIdx*/){
        child->saveToXML(sceneLocation, msgCallback);
        return true;
    });
}

void SceneGraphNode::loadFromXML(const Str256& sceneLocation) {
    boost::property_tree::ptree pt;
    auto targetFile = sceneLocation + "/nodes/";
    targetFile.append(parent()->name());
    targetFile.append("_");
    targetFile.append(name());
    targetFile.append(".xml");
    XML::readXML(targetFile.c_str(), pt);

    loadFromXML(pt);
}

void SceneGraphNode::loadFromXML(const boost::property_tree::ptree& pt) {
    if (!serialize()) {
        return;
    }

    changeUsageContext(pt.get("static", false) ? NodeUsageContext::NODE_STATIC : NodeUsageContext::NODE_DYNAMIC);

    U32 componentsToLoad = 0;
    for (auto i = 1u; i < to_base(ComponentType::COUNT) + 1; ++i) {
        const U32 componentBit = 1 << i;
        const ComponentType type = static_cast<ComponentType>(componentBit);
        if (pt.count(TypeUtil::ComponentTypeToString(type)) > 0) {
            componentsToLoad |= componentBit;
        }
    }

    if (componentsToLoad != 0) {
        AddComponents(componentsToLoad, false);
    }

    for (EditorComponent* editorComponent : Hacks._editorComponents) {
        Attorney::EditorComponentSceneGraphNode::loadFromXML(*editorComponent, pt);
    }
}

void SceneGraphNode::setFlag(const Flags flag, const bool recursive) noexcept {
    if (!hasFlag(flag)) {
        SetBit(_nodeFlags, to_U32(flag));
        ECS::CustomEvent evt = {
           ECS::CustomEvent::Type::EntityFlagChanged,
           nullptr,
           to_U32(flag)
        };
        evt._dataFirst = 1u;
        evt._dataSecond = recursive ? 1u : 0u;

        SendEvent(MOV(evt));
    }

    if (recursive && PropagateFlagToChildren(flag)) {
        forEachChild([flag, recursive](SceneGraphNode* child, I32 /*childIdx*/) {
            child->setFlag(flag, true);
            return true;
        });
    }
}

void SceneGraphNode::clearFlag(const Flags flag, const bool recursive) noexcept {
    if (hasFlag(flag)) {
        ClearBit(_nodeFlags, to_U32(flag));
        ECS::CustomEvent evt = {
            ECS::CustomEvent::Type::EntityFlagChanged,
            nullptr,
            to_U32(flag)
        };
        evt._dataFirst = 0u;
        evt._dataSecond = recursive ? 1u : 0u;

        SendEvent(MOV(evt));
    }

    if (recursive && PropagateFlagToChildren(flag)) {
        forEachChild([flag, recursive](SceneGraphNode* child, I32 /*childIdx*/) {
            child->clearFlag(flag, true);
            return true;
        });
    }
}

void SceneGraphNode::SendEvent(ECS::CustomEvent&& event) {
    size_t idx = 0;
    while (true) {
        bool flush = false;
        {
            if (Events._eventsFreeList[idx].exchange(false)) {
                Events._events[idx] = MOV(event);
                return;
            }

            if (++idx >= Events.EVENT_QUEUE_SIZE) {
                idx %= Events.EVENT_QUEUE_SIZE;
                flush = Runtime::isMainThread();
            }
        }
        if (flush) {
            processEvents();
        }
    }
}

};