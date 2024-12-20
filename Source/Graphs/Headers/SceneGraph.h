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
#ifndef DVD_SCENE_GRAPH_H_
#define DVD_SCENE_GRAPH_H_

#include "SceneNode.h"
#include "IntersectionRecord.h"
#include "Scenes/Headers/SceneComponent.h"
#include "Core/Headers/FrameListener.h"

namespace ECS {
    class ECSEngine;
};

namespace Divide {
class SceneState;
class SceneGraphNode;

struct Ray;
struct SGNRayResult;

namespace Attorney
{
    class SceneGraphSGN;
};

struct SceneGraphNodeDescriptor;

FWD_DECLARE_MANAGED_CLASS(ECSManager);

class SceneGraph final : NonCopyable,
                         public FrameListener,
                         public SceneComponent
{

    friend class Attorney::SceneGraphSGN;

   public:
    explicit SceneGraph(Scene& parentScene);
    ~SceneGraph() override;

    void load();
    void unload();

    const SceneGraphNode* getRoot() const noexcept { return _root; }
    SceneGraphNode* getRoot() noexcept { return _root; }

    SceneGraphNode* findNode(const Str<128>& name, bool sceneNodeName = false) const;
    SceneGraphNode* findNode(U64 nameHash, bool sceneNodeName = false) const;
    SceneGraphNode* findNode(I64 guid) const;

    /// Update all nodes. Called from "updateSceneState" from class Scene
    void sceneUpdate(U64 deltaTimeUS, SceneState& sceneState);

    bool intersect(const SGNIntersectionParams& params, vector<SGNRayResult>& intersectionsOut) const;

    SceneGraphNode* createSceneGraphNode(PlatformContext& context, SceneGraph* sceneGraph, const SceneGraphNodeDescriptor& descriptor);

    void destroySceneGraphNode(SceneGraphNode*& node, bool inPlace = true);
    void addToDeleteQueue(SceneGraphNode* node, size_t childIdx);

    // If this function returns true, the node was successfully removed (or queued for removal)
    bool removeNode(I64 guid);
    bool removeNode(SceneGraphNode* node);
    // If this function returns true, nodes of the specified type were successfully removed (or queued for removal)
    bool removeNodesByType(SceneNodeType nodeType);

    const vector<SceneGraphNode*>& getNodesByType(SceneNodeType type) const;

    void getNodesByType(std::initializer_list<SceneNodeType> types, vector<SceneGraphNode*>& nodesOut) const
    {
        efficient_clear( nodesOut );
        for (const SceneNodeType type : types)
        {
            const vector<SceneGraphNode*>& nodes = getNodesByType(type);
            nodesOut.insert(cend(nodesOut), cbegin(nodes), cend(nodes));
        }
    }

    size_t getTotalNodeCount() const noexcept;

    void onNetworkSend(U32 frameCount);

    void postLoad();

    bool saveNodeToXML(const SceneGraphNode* node) const;
    bool loadNodeFromXML( const ResourcePath& assetsFile, SceneGraphNode* node) const;

    void saveToXML( const ResourcePath& assetsFile, DELEGATE<void, std::string_view> msgCallback ) const;
    void loadFromXML( const ResourcePath& assetsFile);

    ECSManager& GetECSManager() { return *_ecsManager; }
    const ECSManager& GetECSManager() const { return *_ecsManager; }

    ECS::EntityManager* GetEntityManager();
    ECS::EntityManager* GetEntityManager() const;
    ECS::ComponentManager* GetComponentManager();
    ECS::ComponentManager* GetComponentManager() const;

    bool saveCache(ByteBuffer& outputBuffer) const;
    bool loadCache(ByteBuffer& inputBuffer);

    ECS::ECSEngine& GetECSEngine() noexcept { return _ecsEngine; }
    const ECS::ECSEngine& GetECSEngine() const noexcept { return _ecsEngine; }


    /// This will return a bounding sphere even for nodes that don't have a BoundsComponent by
    /// doing some rough estimations based on the TransformComponent (which all nodes do have)
    [[nodiscard]] static BoundingSphere GetBounds( const SceneGraphNode* sgn );

   protected:
    void onNodeMoved(const SceneGraphNode& node);
    void onNodeDestroy(SceneGraphNode* oldNode);
    void onNodeAdd(SceneGraphNode* newNode);
    void onNodeUpdated(const SceneGraphNode& node);
    void onNodeSpatialChange(const SceneGraphNode& node);

    bool frameStarted(const FrameEvent& evt) override;
    bool frameEnded(const FrameEvent& evt) override;

    void checkCollisions(BoundsComponent* bComp);

    static void HandleIntersection( const IntersectionRecord& intersection );

   private:
    ECS::ECSEngine _ecsEngine;
    ECSManager_uptr _ecsManager;
    bool _nodeListChanged = false;

    SceneGraphNode* _root = nullptr;
    vector<SceneGraphNode*> _nodeList;
    Mutex _intersectionsLock;
    IntersectionContainer _intersectionsCache;
    std::array<vector<SceneGraphNode*>, to_base(SceneNodeType::COUNT)> _nodesByType;

    mutable Mutex _nodeCreateMutex;
    mutable SharedMutex _nodesByTypeLock;
    mutable SharedMutex _pendingDeletionLock;
    hashMap<SceneGraphNode*, vector<size_t>> _pendingDeletion;

    mutable Mutex _nodeEventLock;
    eastl::fixed_vector<SceneGraphNode*, 1024, true> _nodeEventQueue;

    mutable Mutex _nodeParentChangeLock;
    eastl::fixed_vector<SceneGraphNode*, 256, true> _nodeParentChangeQueue;
};

FWD_DECLARE_MANAGED_CLASS(SceneGraph);

namespace Attorney
{

class SceneGraphSGN
{
    static void onNodeAdd(Divide::SceneGraph* sceneGraph, SceneGraphNode* newNode)
    {
        sceneGraph->onNodeAdd(newNode);
    }

    static void onNodeDestroy(Divide::SceneGraph* sceneGraph, SceneGraphNode* oldNode)
    {
        sceneGraph->onNodeDestroy(oldNode);
    }

    static void onNodeMoved(Divide::SceneGraph* sceneGraph, const SceneGraphNode& node)
    {
        sceneGraph->onNodeMoved(node);
    }

    static void onNodeShaderReady(Divide::SceneGraph* sceneGraph, const SceneGraphNode& node)
    {
        sceneGraph->onNodeUpdated(node);
    }

    static void onNodeSpatialChange(Divide::SceneGraph* sceneGraph, const SceneGraphNode& node)
    {
        sceneGraph->onNodeSpatialChange(node);
    }

    static void onNodeEvent(Divide::SceneGraph* sceneGraph, SceneGraphNode* node)
    {
        LockGuard<Mutex> w_lock(sceneGraph->_nodeEventLock);
        insert_unique(sceneGraph->_nodeEventQueue, node);
    } 
    
    static void onNodeParentChange(Divide::SceneGraph* sceneGraph, SceneGraphNode* node)
    {
        LockGuard<Mutex> w_lock(sceneGraph->_nodeParentChangeLock);
        insert_unique(sceneGraph->_nodeParentChangeQueue, node);
    }

    friend class Divide::SceneGraphNode;
};

};  // namespace Attorney

};  // namespace Divide

#endif //DVD_SCENE_GRAPH_H_
