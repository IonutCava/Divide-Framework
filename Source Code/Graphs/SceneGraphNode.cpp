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
    constexpr U16 BYTE_BUFFER_VERSION = 2u;

    FORCE_INLINE bool PropagateFlagToChildren(const SceneGraphNode::Flags flag) noexcept {
        return flag == SceneGraphNode::Flags::SELECTED || 
               flag == SceneGraphNode::Flags::HOVERED ||
               flag == SceneGraphNode::Flags::ACTIVE ||
               flag == SceneGraphNode::Flags::VISIBILITY_LOCKED;
    }
};

bool SceneGraphNode::IsContainerNode(const SceneGraphNode& node) noexcept {
    const SceneNodeType nodeType = node._node->type();

    return nodeType == SceneNodeType::TYPE_TRANSFORM ||
           nodeType == SceneNodeType::TYPE_TRIGGER ||
           (nodeType == SceneNodeType::TYPE_OBJECT3D &&
            static_cast<Object3D*>(node._node.get())->getObjectType() == ObjectType::MESH);
}

SceneGraphNode::SceneGraphNode(SceneGraph* sceneGraph, const SceneGraphNodeDescriptor& descriptor)
    : PlatformContextComponent(sceneGraph->parentScene().context()),
      _relationshipCache(this),
      _sceneGraph(sceneGraph),
      _node(descriptor._node),
      _instanceCount(to_U32(descriptor._instanceCount)),
      _serialize(descriptor._serialize),
      _usageContext(descriptor._usageContext)
{
    std::atomic_init(&_children._count, 0u);
    std::atomic_init(&Events._eventsCount, 0u);
    for (auto& it : Events._eventsFreeList) {
        std::atomic_init(&it, true);
    }
    _name = descriptor._name;
    if (_name.empty()) {
        if (_node == nullptr || _node->resourceName().empty()) {
            _name = Util::StringFormat("%d_SGN", getGUID()).c_str();
        } else {
            _name = Util::StringFormat("%s_SGN", _node->resourceName().c_str()).c_str();
        }
    }
    _nameHash = _ID(_name.c_str());

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
    {
        ScopedLock<SharedMutex> w_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            _sceneGraph->destroySceneGraphNode(_children._data[i]);
        }
        _children._data.clear();
        //_children._count.store(0u);
    }

    RemoveAllComponents();
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

void SceneGraphNode::AddSGNComponentInternal(SGNComponent* comp) {
    Hacks._editorComponents.emplace_back(&comp->editorComponent());

    SetBit(_componentMask, to_U32(comp->type()));

    if (comp->type() == ComponentType::TRANSFORM) {                 //Ewww
        Hacks._transformComponentCache = (TransformComponent*)comp;
    }
    if (comp->type() == ComponentType::BOUNDS) {                    //Ewww x2
        Hacks._boundsComponentCache = (BoundsComponent*)comp;
        if (sceneGraph()->getOctree() &&
            !sceneGraph()->getOctree()->addNode(this))
        {
            NOP();
        }
    }
    if (comp->type() == ComponentType::RENDERING) {                  //Ewww x3
        Hacks._renderingComponent = (RenderingComponent*)comp;
    }
}

void SceneGraphNode::RemoveSGNComponentInternal(SGNComponent* comp) {
    if (comp != nullptr) {
        const I64 targetGUID = comp->editorComponent().getGUID();

        Hacks._editorComponents.erase(std::remove_if(std::begin(Hacks._editorComponents),
            std::end(Hacks._editorComponents),
            [targetGUID](EditorComponent* editorComp) noexcept -> bool {
                return editorComp->getGUID() == targetGUID;
            }),
            std::end(Hacks._editorComponents));

        ClearBit(_componentMask, to_U32(comp->type()));

        if (comp->type() == ComponentType::TRANSFORM) {
            Hacks._transformComponentCache = nullptr;
        }
        if (comp->type() == ComponentType::BOUNDS) {
            Hacks._boundsComponentCache = nullptr;
            if (sceneGraph()->getOctree() && !sceneGraph()->getOctree()->removeNode(this)) {
                NOP();
            }
        }
        if (comp->type() == ComponentType::RENDERING) {
            Hacks._renderingComponent = nullptr;
        }
    }
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
    SharedLock<SharedMutex> r_lock(_children._lock);
    const U32 childCount = _children._count;
    for (U32 i = 0u; i < childCount; ++i) {
        TransformComponent* tComp = _children._data[i]->get<TransformComponent>();
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

    const RenderingComponent* rComp = get<RenderingComponent>();
    if (rComp) {
        Attorney::RenderingComponentSGN::onParentUsageChanged(*rComp, _usageContext);
    }
}

/// Change current SceneGraphNode's parent
void SceneGraphNode::setParent(SceneGraphNode* parent, const bool defer) {
    _queuedNewParent = parent->getGUID();
    if (!defer) {
        setParentInternal();
    } else {
        Attorney::SceneGraphSGN::onNodeParentChange(sceneGraph(), this);
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
    SceneGraphNode* oldParent = _parent;
    _parent = newParent;
    
    {// Add ourselves in the new parent's children map
        {
            ScopedLock<SharedMutex> w_lock(_parent->_children._lock);
            _parent->_children._data.push_back(this);
            _parent->_children._count.fetch_add(1);
        }
        Attorney::SceneGraphSGN::onNodeAdd(_sceneGraph, this);
        // That's it. Parent Transforms will be updated in the next render pass;
        _parent->invalidateRelationshipCache();
    }
    {// Carry over new parent's flags and settings
        constexpr Flags flags[] = { Flags::SELECTED, Flags::HOVERED, Flags::ACTIVE, Flags::VISIBILITY_LOCKED };
        for (const Flags flag : flags) {
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
    {
        //Update transforms to keep everything relative
        Attorney::TransformComponentSGN::onParentChanged(*get<TransformComponent>(), oldParent, _parent);
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

    SharedLock<SharedMutex> r_lock(_children._lock);
    const U32 childCount = _children._count.load();
    for (U32 i = 0u; i < childCount; ++i) {
        if (_children._data[i]->removeNodesByType(nodeType)) {
            ++childRemovalCount;
        }
    }

    for (U32 i = 0u; i < childCount; ++i) {
        if (_children._data[i]->getNode().type() == nodeType) {
            _sceneGraph->addToDeleteQueue(this, i);
            ++removalCount;
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
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 count = _children._count.load();
        for (U32 i = 0u; i < count; ++i) {
            if (_children._data[i]->getGUID() == targetGUID) {
                if (deleteNode) {
                    _sceneGraph->addToDeleteQueue(this, i);
                } else {
                    _children._data.erase(_children._data.begin() + i);
                    _children._count.fetch_sub(1);
                }
                return true;
            }
        }
    }

    if (recursive) {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count.load();
        for (U32 i = 0u; i < childCount; ++i) {
            if (_children._data[i]->removeChildNode(node, true, deleteNode)) {
                return true;
            }
        }

        return false;
    }

    return true;
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
    return sceneNodeName ? findChildInternal<true>(nameHash, recursive)
                         : findChildInternal<false>(nameHash, recursive);
}

SceneGraphNode* SceneGraphNode::findChild(const I64 GUID, const bool sceneNodeGuid, const bool recursive) const {
    return sceneNodeGuid ? findChildInternal<true>(GUID, recursive)
                         : findChildInternal<false>(GUID, recursive);
}

bool SceneGraphNode::intersect(const Ray& intersectionRay, const vec2<F32>& range, vector<SGNRayResult>& intersections) const {
    vector<SGNRayResult> ret = {};

    // Root has its own intersection routine, so we ignore it
    if (_sceneGraph->getRoot()->getGUID() == this->getGUID()) {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            _children._data[i]->intersect(intersectionRay, range, intersections);
        }
    } else {
        // If we hit a bounding sphere, we proceed to the more expensive OBB test
        if (get<BoundsComponent>() && get<BoundsComponent>()->getBoundingSphere().intersect(intersectionRay, range.min, range.max).hit) {
            const RayResult result = get<BoundsComponent>()->getOBB().intersect(intersectionRay, range.min, range.max);
            if (result.hit) {
                intersections.push_back({ getGUID(), result.dist, name().c_str() });

                SharedLock<SharedMutex> r_lock(_children._lock);
                const U32 childCount = _children._count;
                for (U32 i = 0u; i < childCount; ++i) {
                    _children._data[i]->intersect(intersectionRay, range, intersections);
                };
            }
        }
    }

    return !intersections.empty();
}

void SceneGraphNode::getAllNodes(vector<SceneGraphNode*>& nodeList) {
    // Compute from leaf to root to ensure proper calculations
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            _children._data[i]->getAllNodes(nodeList);
        }
    }

    nodeList.push_back(this);
}

void SceneGraphNode::processDeleteQueue(vector<size_t>& childList) {
    // See if we have any children to delete
    if (!childList.empty()) {
        ScopedLock<SharedMutex> w_lock(_children._lock);
        for (const size_t childIdx : childList) {
            _sceneGraph->destroySceneGraphNode(_children._data[childIdx]);
        }
        EraseIndices(_children._data, childList);
        _children._count.store(to_U32(_children._data.size()));
    }
}

/// Please call in MAIN THREAD! Nothing is thread safe here (for now) -Ionut
void SceneGraphNode::sceneUpdate(const U64 deltaTimeUS, SceneState& sceneState) {
    OPTICK_EVENT();
    if (hasFlag(Flags::ACTIVE)) {
        Attorney::SceneNodeSceneGraph::sceneUpdate(_node.get(), deltaTimeUS, this, sceneState);
    }

    if (hasFlag(Flags::PARENT_POST_RENDERED)) {
        clearFlag(Flags::PARENT_POST_RENDERED);
    }
}

void SceneGraphNode::processEvents() {
    OPTICK_EVENT();

    DIVIDE_ASSERT(Events._eventsCount.load() != 0u);

    const ECS::EntityId id = GetEntityID();
    for (size_t idx = 0u; idx < Events.EVENT_QUEUE_SIZE; ++idx) {
        if (Events._eventsFreeList[idx]) {
            continue;
        }

        Events._eventsCount.fetch_sub(1u);
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
                Attorney::SceneGraphSGN::onNodeMoved(sceneGraph(), *this);
            }break;
            case ECS::CustomEvent::Type::NewShaderReady: {
                Attorney::SceneGraphSGN::onNodeShaderReady(sceneGraph(), *this);
            } break;
            default: break;
        }

        PassDataToAllComponents(evt);

        if (evt._type == ECS::CustomEvent::Type::TransformUpdated ||
            evt._type == ECS::CustomEvent::Type::AnimationUpdated) {
            Attorney::SceneGraphSGN::onNodeSpatialChange(sceneGraph(), *this);
        }
        Events._eventsFreeList[idx] = true;
    }
}

void SceneGraphNode::prepareRender(RenderingComponent& rComp, const RenderStagePass renderStagePass, const CameraSnapshot& cameraSnapshot, const bool refreshData) {
    OPTICK_EVENT();

    const AnimationComponent* aComp = get<AnimationComponent>();
    if (aComp) {
        {
            DescriptorSet& set = rComp.getDescriptorSet(renderStagePass);
            const AnimationComponent::AnimData data = aComp->getAnimationData();
            if (data._boneBuffer != nullptr) {
                ShaderBufferBinding bufferBinding;
                bufferBinding._binding = ShaderBufferLocation::BONE_TRANSFORMS;
                bufferBinding._buffer = data._boneBuffer;
                bufferBinding._elementRange = data._boneBufferRange;
                set._buffers.add(bufferBinding);
            }
            if (data._prevBoneBufferRange.max > 0) {
                ShaderBufferBinding bufferBinding;
                bufferBinding._binding = ShaderBufferLocation::BONE_TRANSFORMS_PREV;
                bufferBinding._buffer = data._boneBuffer;
                bufferBinding._elementRange = data._prevBoneBufferRange;
                set._buffers.add(bufferBinding);
            }
        }
    }

    _node->prepareRender(this, rComp, renderStagePass, cameraSnapshot, refreshData);
}

void SceneGraphNode::onNetworkSend(U32 frameCount) const {
    {
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            _children._data[i]->onNetworkSend(frameCount);
        }
    }
    NetworkingComponent* net = get<NetworkingComponent>();
    if (net) {
        net->onNetworkSend(frameCount);
    }
}

bool SceneGraphNode::canDraw(const RenderStagePass stagePass) const {
    RenderingComponent* rComp = get<RenderingComponent>();

    return rComp != nullptr && rComp->canDraw(stagePass);
}

namespace {
    [[nodiscard]] inline bool castsShadows(const SceneGraphNode* const node) {
        const SceneNodeType nodeType = node->getNode().type();

        if (nodeType == SceneNodeType::TYPE_SKY ||
            nodeType == SceneNodeType::TYPE_WATER ||
            nodeType == SceneNodeType::TYPE_INFINITEPLANE ||
            (nodeType == SceneNodeType::TYPE_OBJECT3D && node->getNode<Object3D>().getObjectType() == ObjectType::DECAL))
        {
            return false;
        }

        const RenderingComponent* rComp = node->get<RenderingComponent>();
        return rComp != nullptr &&  rComp->renderOptionEnabled(RenderingComponent::RenderOptions::CAST_SHADOWS);
    }

    [[nodiscard]] F32 distanceSquareToPos(const vec3<F32>& pos, const BoundingSphere& sphere) noexcept {
        return sphere.getCenter().distanceSquared(pos) - SQUARED(sphere.getRadius());
    }
};

// Returns true if the node SHOULD be culled!
FrustumCollision SceneGraphNode::cullNode(const NodeCullParams& params,
                                          const U16 cullFlags,
                                          F32& distanceToClosestPointSQ) const {
    OPTICK_EVENT("CullNode - complex");

    distanceToClosestPointSQ = std::numeric_limits<F32>::max();

    // Early out for inactive nodes
    if (!hasFlag(SceneGraphNode::Flags::ACTIVE)) {
        return FrustumCollision::FRUSTUM_OUT;
    }

    // If the node is still loading, DO NOT RENDER IT. Bad things happen :D
    if (hasFlag(Flags::LOADING)) {
        return FrustumCollision::FRUSTUM_OUT;
    }

    // Drawing is disabled in general for this node
    if (!_node->renderState().drawState()) {
        return FrustumCollision::FRUSTUM_OUT;
    }

    // We may also wish to keep the sky always visible (e.g. for dynamic cubemaps)
    const bool keepSky = BitCompare(cullFlags, CullOptions::KEEP_SKY_NODES) && _node->type() == SceneNodeType::TYPE_SKY;
    // We may wish to always render this node for whatever reason (e.g. to preload shaders)
    if (hasFlag(Flags::VISIBILITY_LOCKED) || keepSky) {
        if (keepSky || _node->renderState().drawState(RenderStagePass{ params._stage, RenderPassType::COUNT })) {
            distanceToClosestPointSQ = distanceSquareToPos(params._cameraEyePos, get<BoundsComponent>()->getBoundingSphere());
            return FrustumCollision::FRUSTUM_IN;
        }

        return FrustumCollision::FRUSTUM_OUT;
    }

    if (usageContext() == NodeUsageContext::NODE_STATIC) {
        if (BitCompare(cullFlags, CullOptions::CULL_STATIC_NODES)) {
            return FrustumCollision::FRUSTUM_OUT;
        }
    } else {
        if (BitCompare(cullFlags, CullOptions::CULL_DYNAMIC_NODES)) {
            return FrustumCollision::FRUSTUM_OUT;
        }
    }

    // This one should be obvious
    if (params._stage == RenderStage::SHADOW && !castsShadows(this)) {
        return FrustumCollision::FRUSTUM_OUT;
    }

    const BoundsComponent* bComp = get<BoundsComponent>();
    if (params._skipBoundsChecking || !bComp) {
        distanceToClosestPointSQ = bComp == nullptr ? 0.f : distanceSquareToPos(params._cameraEyePos, get<BoundsComponent>()->getBoundingSphere());
        return FrustumCollision::FRUSTUM_IN;
    }

    const BoundingSphere& boundingSphere = bComp->getBoundingSphere();
    const BoundingBox& boundingBox = bComp->getBoundingBox();
    const vec3<F32>& bSphereCenter = boundingSphere.getCenter();
    const F32 radius = boundingSphere.getRadius();

    STUBBED("ToDo: make this work in a multi-threaded environment -Ionut");
    _frustPlaneCache = -1;
    I8 fakePlaneCache = -1;

    // Get camera info
    const vec3<F32>& eye = params._cameraEyePos;
    {
        OPTICK_EVENT("cullNode - Bounding Sphere Distance Test");
        distanceToClosestPointSQ = bSphereCenter.distanceSquared(eye) - SQUARED(radius);
        if (distanceToClosestPointSQ > SQUARED(params._cullMaxDistance)) {
            // Node is too far away
            return FrustumCollision::FRUSTUM_OUT;
        }
    }
    {
        OPTICK_EVENT("cullNode - Bounding Box Distance Test");
        distanceToClosestPointSQ = boundingBox.nearestPoint(eye).distanceSquared(eye);
        if (distanceToClosestPointSQ > SQUARED(params._cullMaxDistance)) {
            // Check again using the AABB
            return FrustumCollision::FRUSTUM_OUT;
        }

        const F32 upperBound = params._minExtents.maxComponent();
        if (upperBound > 0.0f && boundingBox.getExtent().maxComponent() < upperBound) {
            // Node is too small for the current render pass
            return FrustumCollision::FRUSTUM_OUT;
        }

    }

    if (BitCompare(cullFlags, CullOptions::CULL_AGAINST_CLIPPING_PLANES)) {
        OPTICK_EVENT("cullNode - Bounding Sphere - Clipping Planes Test");
        auto& planes = params._clippingPlanes.planes();
        auto& states = params._clippingPlanes.planeState();
        for (U8 i = 0u; i < to_U8(ClipPlaneIndex::COUNT); ++i) {
            if (states[i]) {
                if (PlaneBoundingSphereIntersect(planes[i], boundingSphere) == FrustumCollision::FRUSTUM_OUT) {
                    // Fails the clipping plane test
                    return FrustumCollision::FRUSTUM_OUT;
                }
            }
        }
    }

    FrustumCollision collisionType = FrustumCollision::FRUSTUM_IN;
    if (BitCompare(cullFlags, CullOptions::CULL_AGAINST_FRUSTUM)) {
        OPTICK_EVENT("cullNode - Bounding Sphere & Box Frustum Test");

        // Sphere is in range, so check bounds primitives against the frustum
        if (!boundingBox.containsPoint(eye)) {
            // Check if the bounding sphere is in the frustum, as Frustum <-> Sphere check is fast
            collisionType = params._frustum->ContainsSphere(bSphereCenter, radius, fakePlaneCache);
            if (collisionType == FrustumCollision::FRUSTUM_INTERSECT) {
                // If the sphere is not completely in the frustum, check the AABB
                collisionType = params._frustum->ContainsBoundingBox(boundingBox, fakePlaneCache);
            }
        } else {// else we are inside the AABB, so INTERSECT is correct
            collisionType = FrustumCollision::FRUSTUM_INTERSECT;
        }
        if (collisionType == FrustumCollision::FRUSTUM_OUT) {
            return FrustumCollision::FRUSTUM_OUT;
        }
    }

    if (BitCompare(cullFlags, CullOptions::CULL_AGAINST_LOD) && !IsContainerNode(*this)) {
        OPTICK_EVENT("cullNode - LoD check")

        RenderingComponent* rComp = get<RenderingComponent>();
        const vec2<F32>& renderRange = rComp->renderRange();
        const F32 minDistanceSQ = SQUARED(renderRange.min) * (renderRange.min < 0.f ? -1.f : 1.f); //Keep the sign. Might need it for rays or shadows.
        const F32 maxDistanceSQ = SQUARED(renderRange.max);

        if (!IS_IN_RANGE_INCLUSIVE(distanceToClosestPointSQ, minDistanceSQ, maxDistanceSQ)) {
            return FrustumCollision::FRUSTUM_OUT;
        } 

        // We are in range, so proceed to LoD checks
        const U8 LoDLevel = rComp->getLoDLevel(distanceToClosestPointSQ, params._stage, params._lodThresholds);
        if ((params._maxLoD > -1 && LoDLevel > params._maxLoD) || LoDLevel > _node->renderState().maxLodLevel()) {
            return FrustumCollision::FRUSTUM_OUT;
        }
    }

    return collisionType;
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

        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            if (!source || _children._data[i]->getGUID() != source->getGUID()) {
                _children._data[i]->invalidateRelationshipCache(this);
            }
        }
    }
}

bool SceneGraphNode::saveCache(ByteBuffer& outputBuffer) const {
    outputBuffer << BYTE_BUFFER_VERSION;

    return getNode().saveCache(outputBuffer) &&
           _sceneGraph->GetECSManager().saveCache(this, outputBuffer);
}

bool SceneGraphNode::loadCache(ByteBuffer& inputBuffer) {
    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    inputBuffer >> tempVer;

    return tempVer == BYTE_BUFFER_VERSION &&
           getNode().loadCache(inputBuffer) &&
           _sceneGraph->GetECSManager().loadCache(this, inputBuffer);
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

    for (const EditorComponent* editorComponent : Hacks._editorComponents) {
        Attorney::EditorComponentSceneGraphNode::saveToXML(*editorComponent, pt);
    }

    ResourcePath savePath{ sceneLocation.c_str() };
    savePath.append("/nodes/");

    ResourcePath targetFile{parent()->name().c_str()};
    targetFile.append("_");
    targetFile.append(name());
    XML::writeXML((savePath + Util::MakeXMLSafe(targetFile) + ".xml").c_str(), pt);

    SharedLock<SharedMutex> r_lock(_children._lock);
    const U32 childCount = _children._count;
    for (U32 i = 0u; i < childCount; ++i) {
        _children._data[i]->saveToXML(sceneLocation, msgCallback);
    }
}

void SceneGraphNode::loadFromXML(const Str256& sceneLocation) {
    boost::property_tree::ptree pt;
    ResourcePath savePath{ sceneLocation.c_str() };
    savePath.append("/nodes/");

    ResourcePath targetFile{ parent()->name().c_str() };
    targetFile.append("_");
    targetFile.append(name());
    XML::readXML((savePath + Util::MakeXMLSafe(targetFile) + ".xml").c_str(), pt);

    loadFromXML(pt);
}

void SceneGraphNode::loadFromXML(const boost::property_tree::ptree& pt) {
    if (!serialize()) {
        return;
    }

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

    changeUsageContext(pt.get("static", false) ? NodeUsageContext::NODE_STATIC : NodeUsageContext::NODE_DYNAMIC);
}

void SceneGraphNode::setFlag(const Flags flag, const bool recursive) {
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
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            _children._data[i]->setFlag(flag, true);
        }
    }
}

void SceneGraphNode::clearFlag(const Flags flag, const bool recursive) {
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
        SharedLock<SharedMutex> r_lock(_children._lock);
        const U32 childCount = _children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            _children._data[i]->clearFlag(flag, true);
        }
    }
}

void SceneGraphNode::SendEvent(ECS::CustomEvent&& event) {
    size_t idx = 0;
    while (true) {
        bool flush = false;
        {
            if (Events._eventsFreeList[idx].exchange(false)) {
                Events._eventsCount.fetch_add(1u);
                Events._events[idx] = MOV(event);
                Attorney::SceneGraphSGN::onNodeEvent(sceneGraph(), this);
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