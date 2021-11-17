/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   */

#pragma once
#ifndef _SCENE_GRAPH_NODE_H_
#define _SCENE_GRAPH_NODE_H_

#include "SceneNode.h"
#include "SGNRelationshipCache.h"

#include "ECS/Components/Headers/EditorComponent.h"
#include "ECS/Components/Headers/SGNComponent.h"

namespace Divide {

class SceneGraph;
class SceneState;
class PropertyWindow;
class BoundsComponent;
class TransformSystem;
class RenderPassCuller;
class RenderPassManager;
class RenderPassExecutor;
class RenderingComponent;
class TransformComponent;

struct NodeCullParams;

struct SceneGraphNodeDescriptor {
    SceneNode_ptr    _node = nullptr;
    Str64            _name = "";
    size_t           _instanceCount = 1;
    U32              _componentMask = 0u;
    NodeUsageContext _usageContext = NodeUsageContext::NODE_STATIC;
    bool             _serialize = true;
};

namespace Attorney {
    class SceneGraphNodeEditor;
    class SceneGraphNodeComponent;
    class SceneGraphNodeSystem;
    class SceneGraphNodeSceneGraph;
    class SceneGraphNodeRenderPassCuller;
    class SceneGraphNodeRenderPassManager;
    class SceneGraphNodeRelationshipCache;
    class SceneGraphNodeScene;
};

struct SGNRayResult {
    I64 sgnGUID = -1;
    F32 dist = std::numeric_limits<F32>::max();
    const char* name = nullptr;
};

struct SGNIntersectionParams
{
    Ray _ray;
    vec2<F32> _range = { 0.f, 1.f };
    const SceneNodeType* _ignoredTypes = nullptr;
    size_t _ignoredTypesCount = 0;
    bool _includeTransformNodes = true;
};

class SceneGraphNode final : public ECS::Entity<SceneGraphNode>,
                             public GUIDWrapper,
                             public PlatformContextComponent
{
    friend class Attorney::SceneGraphNodeEditor;
    friend class Attorney::SceneGraphNodeComponent;
    friend class Attorney::SceneGraphNodeSystem;
    friend class Attorney::SceneGraphNodeSceneGraph;
    friend class Attorney::SceneGraphNodeRenderPassCuller;
    friend class Attorney::SceneGraphNodeRenderPassManager;
    friend class Attorney::SceneGraphNodeRelationshipCache;
    friend class Attorney::SceneGraphNodeScene;

public:

    enum class Flags : U16 {
        LOADING = toBit(1),
        HOVERED = toBit(2),
        SELECTED = toBit(3),
        ACTIVE = toBit(4),
        VISIBILITY_LOCKED = toBit(5),
        PARENT_POST_RENDERED = toBit(6),
        COUNT = 7
    };

public:
    /// Avoid creating SGNs directly. Use the "addChildNode" on an existing node (even root) or "addNode" on the existing SceneGraph
    explicit SceneGraphNode(SceneGraph* sceneGraph, const SceneGraphNodeDescriptor& descriptor);
    ~SceneGraphNode();

    /// Return a reference to the parent graph. Operator= should be disabled
    SceneGraph* sceneGraph() noexcept { return _sceneGraph; }

    /// Add child node increments the node's ref counter if the node was already added to the scene graph
    SceneGraphNode* addChildNode(const SceneGraphNodeDescriptor& descriptor);

    /// If this function returns true, the node will no longer be part of the scene hierarchy.
    /// If the node is not a child of the calling node, we will recursively look in all of its children for a match
    bool removeChildNode(const SceneGraphNode* node, bool recursive = true, bool deleteNode = true);

    /// Find a child Node using the given name (either SGN name or SceneNode name)
    SceneGraphNode* findChild(U64 nameHash, bool sceneNodeName = false, bool recursive = false) const;

    /// Find a child using the given SGN or SceneNode GUID
    SceneGraphNode* findChild(I64 GUID, bool sceneNodeGuid = false, bool recursive = false) const;

    /// If this function returns true, at least one node of the specified type was removed.
    bool removeNodesByType(SceneNodeType nodeType);

    /// Changing a node's parent means removing this node from the current parent's child list and appending it to the new parent's list (happens after a full frame)
    void setParent(SceneGraphNode* parent, bool defer = false);

    /// Checks if we have a parent matching the typeMask. We check recursively until we hit the top node (if ignoreRoot is false, top node is Root)
    bool isChildOfType(U16 typeMask) const;

    /// Returns true if the current node is related somehow to the specified target node (see RelationshipType enum for more details)
    bool isRelated(const SceneGraphNode* target) const;

    /// Returns true if the specified target node is a parent or grandparent(if recursive == true) of the current node
    bool isChild(const SceneGraphNode* target, bool recursive) const;

    /// Recursively call the delegate function on all children. Returns false if the loop was interrupted. Use start and end to only affect a range (useful for parallel algorithms)
    template<class Predicate>
    bool forEachChild(U32 start, U32 end, Predicate predicate) const;

    template<class Predicate>
    bool forEachChild(Predicate predicate) const;

    /// A "locked" call assumes that either access is guaranteed thread-safe or that the child lock is already acquired
    const vector<SceneGraphNode*>& getChildrenLocked() const noexcept { return _children; }

    /// Return the current number of children that the current node has
    U32 getChildCount() const noexcept { return _childCount.load(); }

    void lockChildrenForWrite() const noexcept { _childLock.lock(); }

    void unlockChildrenForWrite() const noexcept { _childLock.unlock(); }

    void lockChildrenForRead() const noexcept { _childLock.lock_shared(); }

    void unlockChildrenForRead() const noexcept { _childLock.unlock_shared(); }

    /// Return a specific child by index. Does not recurse.
    SceneGraphNode* getChild(const U32 idx) {
        SharedLock<SharedMutex> r_lock(_childLock);
        return _children[idx];
    }

    /// Return a specific child by index. Does not recurse.
    const SceneGraphNode* getChild(const U32 idx) const {
        SharedLock<SharedMutex> r_lock(_childLock);
        return _children[idx];
    }

    /// Called from parent SceneGraph
    void sceneUpdate(U64 deltaTimeUS, SceneState& sceneState);

    /// Invoked by the contained SceneNode when it finishes all of its internal loading and is ready for processing
    void postLoad();

    /// Find the graph nodes whom's bounding boxes intersects the given ray
    bool intersect(const Ray& intersectionRay, const vec2<F32>& range, vector<SGNRayResult>& intersections) const;

    void changeUsageContext(const NodeUsageContext& newContext);

    /// General purpose flag management. Certain flags propagate to children (e.g. selection)!
    void setFlag(Flags flag, bool recursive = true);
    /// Clearing a flag might propagate to child nodes (e.g. selection).
    void clearFlag(Flags flag, bool recursive = true);
    /// Returns true only if the current node has the specified flag. Does not check children!
    [[nodiscard]] bool hasFlag(const Flags flag) const noexcept;

    /// Always use the level of redirection needed to reduce virtual function overhead.
    /// Use getNode<SceneNode> if you need material properties for ex. or getNode<SkinnedSubMesh> for animation transforms
    template <typename T = SceneNode>
    typename std::enable_if<std::is_base_of<SceneNode, T>::value, T&>::type
    getNode() noexcept { return static_cast<T&>(*_node); }

    /// Always use the level of redirection needed to reduce virtual function overhead.
    /// Use getNode<SceneNode> if you need material properties for ex. or getNode<SkinnedSubMesh> for animation transforms
    template <typename T = SceneNode>
    typename std::enable_if<std::is_base_of<SceneNode, T>::value, const T&>::type
    getNode() const noexcept { return static_cast<const T&>(*_node); }

    const SceneNode_ptr& getNodePtr() const noexcept { return _node; }

    /// Returns a pointer to a specific component. Returns null if the SGN does not have the component requested
    template <typename T>
    T* get() const { return _compManager->GetComponent<T>(GetEntityID()); } ///< ToDo: Optimise this -Ionut

    void SendEvent(ECS::CustomEvent&& event);

    /// Sends a global event but dispatched is handled between update steps
    template<class E, class... ARGS>
    void SendEvent(ARGS&&... eventArgs) { GetECSEngine().SendEvent<E>(FWD(eventArgs)...); }
    /// Sends a global event with dispatched happening immediately. Avoid using often. Bad for performance.
    template<class E, class... ARGS>
    void SendAndDispatchEvent(ARGS&&... eventArgs) { GetECSEngine().SendEventAndDispatch<E>(FWD(eventArgs)...); }

    /// Emplacement call for an ECS component. Pass in the component's constructor parameters. Can only add one component of a single type. 
    /// This may be bad for scripts, but there are workarounds
    template<class T, class ...P>
    typename std::enable_if<std::is_base_of<SGNComponent, T>::value, T*>::type
    AddSGNComponent(P&&... param) {
        SGNComponent* comp = static_cast<SGNComponent*>(AddComponent<T>(this, this->context(), FWD(param)...));
        AddSGNComponentInternal(comp);
        return static_cast<T*>(comp);
    }

    /// Remove a component by type (if any). Because we have a limit of one component type per node, this works as expected
    template<class T>
    typename std::enable_if<std::is_base_of<SGNComponent, T>::value, void>::type
    RemoveSGNComponent() {
        RemoveSGNComponentInternal(static_cast<SGNComponent*>(GetComponent<T>()));
        RemoveComponent<T>();
    }

    void AddComponents(U32 componentMask, bool allowDuplicates);
    void RemoveComponents(U32 componentMask);

    /// Serialization: save to XML file
    void saveToXML(const Str256& sceneLocation, DELEGATE<void, std::string_view> msgCallback = {}) const;
    void loadFromXML(const Str256& sceneLocation);
    /// Serialization: load from XML file (expressed as a boost property_tree)
    void loadFromXML(const boost::property_tree::ptree& pt);

private:
    /// Process any events that might of queued up during the ECS Update stages
    void processEvents();
    /// Returns true if the node SHOULD be culled (is not visible for the current stage). Calls "preCullNode" internally.
    bool cullNode(const NodeCullParams& params, const U16 cullFlags, FrustumCollision& collisionTypeOut, F32& distanceToClosestPointSQ) const;
    /// Called after preRender and after we rebuild our command buffers. Useful for modifying the command buffer that's going to be used for this RenderStagePass
    void prepareRender(RenderingComponent& rComp, const RenderStagePass& renderStagePass, const Camera& camera, bool refreshData);
    /// Called whenever we send a networking packet from our NetworkingComponent (if any). FrameCount is the frame ID sent with the packet.
    void onNetworkSend(U32 frameCount) const;
    /// Returns a bottom-up list(leafs -> root) of all of the nodes parented under the current one.
    void getAllNodes(vector<SceneGraphNode*>& nodeList);
    /// Destructs all of the nodes specified in the list and removes them from the _children container.
    void processDeleteQueue(vector<size_t>& childList);
    /// Similar to the saveToXML call but is geared towards temporary state (e.g. save game)
    bool saveCache(ByteBuffer& outputBuffer) const;
    /// Similar to the loadFromXML call but is geared towards temporary state (e.g. save game)
    bool loadCache(ByteBuffer& inputBuffer);
    /// Called by the TransformComponent whenever the transform changed. Useful for Octree updates for example.
    void setTransformDirty(U32 transformMask);
    /// As opposed to "postLoad()" that's called when the SceneNode is ready for processing, this call is used as a callback for when the SceneNode finishes loading as a Resource. postLoad() is called after this always.
    static void PostLoad(SceneNode* sceneNode, SceneGraphNode* sgn);
    /// This indirect is used to avoid including ECS headers in this file, but we still need the ECS engine for templated methods
    ECS::ECSEngine& GetECSEngine() const noexcept;
    /// Only called from withing "setParent()" (which is also called from "addChildNode()"). Used to mark the existing relationship cache as invalid.
    void invalidateRelationshipCache(SceneGraphNode *source = nullptr);
    /// Changes this node's parent
    void setParentInternal();

    void occlusionCull(const RenderStagePass& stagePass,
                       const Texture_ptr& depthBuffer,
                       const Camera& camera,
                       GFX::SendPushConstantsCommand& HIZPushConstantsCMDInOut,
                       GFX::CommandBuffer& bufferInOut) const;

    bool canDraw(const RenderStagePass& stagePass) const;

    void AddSGNComponentInternal(SGNComponent* comp);
    void RemoveSGNComponentInternal(SGNComponent* comp);
private:
    SGNRelationshipCache _relationshipCache;
    vector<SceneGraphNode*> _children;
    std::atomic_uint _childCount;

    // ToDo: Remove this HORRIBLE hack -Ionut
    struct hacks {
        vector_fast<EditorComponent*> _editorComponents;
        TransformComponent* _transformComponentCache = nullptr;
        BoundsComponent* _boundsComponentCache = nullptr;
    } Hacks;

    struct events
    {
        // this should be ample storage PER FRAME for events.
        static constexpr size_t EVENT_QUEUE_SIZE = 128;
        std::array<ECS::CustomEvent, EVENT_QUEUE_SIZE> _events;
        std::array<std::atomic_bool, EVENT_QUEUE_SIZE> _eventsFreeList;
        std::atomic_size_t _eventsCount = 0u;
    } Events;

    mutable SharedMutex _childLock;

    POINTER_R(SceneGraph, sceneGraph, nullptr);
    PROPERTY_R(SceneNode_ptr, node, nullptr);
    POINTER_R(ECS::ComponentManager, compManager, nullptr);
    POINTER_R(SceneGraphNode, parent, nullptr);
    PROPERTY_R(Str64, name, "");
    PROPERTY_R(I64, queuedNewParent, -1);
    PROPERTY_RW(U64, lockToCamera, 0u);
    PROPERTY_R(U64, elapsedTimeUS, 0u);
    PROPERTY_R(U32, componentMask, 0u);
    PROPERTY_R(U32, nodeFlags, 0u);
    PROPERTY_R(U32, instanceCount, 1u);
    PROPERTY_R(bool, serialize, true);
    PROPERTY_R_IW(bool, visiblePostCulling, false);
    PROPERTY_R(NodeUsageContext, usageContext, NodeUsageContext::NODE_STATIC);
    //ToDo: make this work in a multi-threaded environment
    mutable I8 _frustPlaneCache = -1;
};

namespace Attorney {
    class SceneGraphNodeEditor {
        static vector_fast<EditorComponent*>& editorComponents(SceneGraphNode* node) noexcept {
            return node->Hacks._editorComponents;
        }

        static const vector_fast<EditorComponent*>& editorComponents(const SceneGraphNode* node) noexcept {
            return node->Hacks._editorComponents;
        }

        friend class Divide::PropertyWindow;
    };

    class SceneGraphNodeSceneGraph {
        static void processEvents(SceneGraphNode* node) {
            node->processEvents();
        }

        static void onNetworkSend(const SceneGraphNode* node, const U32 frameCount) {
            node->onNetworkSend(frameCount);
        }

        static void getAllNodes(SceneGraphNode* node, vector<SceneGraphNode*>& nodeList) {
            node->getAllNodes(nodeList);
        }

;        static void processDeleteQueue(SceneGraphNode* node, vector<size_t>& childList) {
            node->processDeleteQueue(childList);
        }

        static bool saveCache(const SceneGraphNode* node, ByteBuffer& outputBuffer) {
            return node->saveCache(outputBuffer);
        }

        static bool loadCache(SceneGraphNode* node, ByteBuffer& inputBuffer) {
            return node->loadCache(inputBuffer);
        }

        friend class Divide::SceneGraph;
    };

    class SceneGraphNodeSystem
    {
        static void setTransformDirty(SceneGraphNode* node, const U32 transformMask) {
            node->setTransformDirty(transformMask);
        }  
        
        friend class Divide::TransformSystem;
    };

    class SceneGraphNodeComponent {
        static void prepareRender(SceneGraphNode* node, RenderingComponent& rComp, const Camera& camera, const RenderStagePass& renderStagePass, const bool refreshData) {
            node->prepareRender(rComp, renderStagePass, camera, refreshData);
        }

        friend class Divide::BoundsComponent;
        friend class Divide::RenderingComponent;
        friend class Divide::TransformComponent;
    };

    class SceneGraphNodeRenderPassCuller {
        // Returns true if the node should be culled (is not visible for the current stage)
        static bool cullNode(const SceneGraphNode* node, const NodeCullParams& params, const U16 cullFlags, FrustumCollision& collisionTypeOut, F32& distanceToClosestPointSQ) {
            return node->cullNode(params, cullFlags, collisionTypeOut, distanceToClosestPointSQ);
        }
        
        static void visiblePostCulling(SceneGraphNode* node, const bool state) noexcept {
            node->visiblePostCulling(state);
        }

        friend class Divide::RenderPassCuller;
    };

    class SceneGraphNodeRenderPassManager
    {
        static void occlusionCullNode(const SceneGraphNode* node, const RenderStagePass& stagePass, const Texture_ptr& depthBuffer, const Camera& camera, GFX::SendPushConstantsCommand& HIZPushConstantsCMDInOut, GFX::CommandBuffer& bufferInOut) {
            node->occlusionCull(stagePass, depthBuffer, camera, HIZPushConstantsCMDInOut, bufferInOut);
        }

        static bool canDraw(const SceneGraphNode* node, const RenderStagePass& stagePass) {
            return node->canDraw(stagePass);
        }

        friend class Divide::RenderPassExecutor;
        friend class Divide::RenderPassManager;
    };


    class SceneGraphNodeRelationshipCache {
        static const SGNRelationshipCache& relationshipCache(const SceneGraphNode* node) noexcept {
            return node->_relationshipCache;
        }

        friend class Divide::SGNRelationshipCache;
    };
    
    class SceneGraphNodeScene {
        static void reserveChildCount(SceneGraphNode* node, const size_t count) {
            node->lockChildrenForWrite();
            node->_children.reserve(count);
            node->unlockChildrenForWrite();
        }

        friend class Divide::Scene;

    };
};  // namespace Attorney


};  // namespace Divide
#endif

#include "SceneGraphNode.inl"