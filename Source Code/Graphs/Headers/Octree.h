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
#ifndef _OCTREE_H_
#define _OCTREE_H_

#include "IntersectionRecord.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"

namespace Divide {

class Frustum;
class SceneGraphNode;
class BoundsComponent;

struct SGNIntersectionParams;

FWD_DECLARE_MANAGED_CLASS(OctreeNode);

// ref: http://www.gamedev.net/page/resources/_/technical/game-programming/introduction-to-octrees-r3529
class Octree {
public:
    explicit Octree(U16 nodeMask);

    void update(U64 deltaTimeUS);

    [[nodiscard]] bool addNode(SceneGraphNode* node);
    [[nodiscard]] bool removeNode(SceneGraphNode* node);

    [[nodiscard]] vector<IntersectionRecord> allIntersections(const Frustum& region, U16 typeFilterMask);
    [[nodiscard]] vector<IntersectionRecord> allIntersections(const Ray& intersectionRay, F32 start, F32 end);
    [[nodiscard]] vector<IntersectionRecord> allIntersections(const Ray& intersectionRay, F32 start, F32 end, U16 typeFilterMask);
    [[nodiscard]] IntersectionRecord         nearestIntersection(const Ray& intersectionRay, F32 start, F32 end, U16 typeFilterMask);

    void getAllRegions(vector<BoundingBox>& regionsOut) const;

protected:
    friend class SceneGraph;
    void onNodeMoved(const SceneGraphNode& sgn);

protected:
    friend class OctreeNode;
    void onChildBuildTree();
    void onChildStateChange(OctreeNode* node, bool state);

private:
    void updateTree();
    void handleIntersection(const IntersectionRecord& intersection) const;

protected:
    OctreeNode_uptr _root = nullptr;
    vector<const SceneGraphNode*> _intersectionsObjectCache;
    eastl::queue<SceneGraphNode*> _pendingInsertion;
    eastl::queue<SceneGraphNode*> _pendingRemoval;
    Mutex _pendingInsertLock;
    bool _treeReady = false;
    bool _treeBuilt = false;
    const U16 _nodeExclusionMask = 0u;

    Mutex _linearObjectCacheLock;
    vector<OctreeNode*> _linearObjectCache;
};

FWD_DECLARE_MANAGED_CLASS(Octree);

class OctreeNode  {
public:
    /// Minimum cube size is 1x1x1
    static constexpr F32 MIN_SIZE = 1.0f;
    static constexpr I32 MAX_LIFE_SPAN_LIMIT = 64;

    explicit OctreeNode(OctreeNode* parent, U16 nodeMask, Octree* parentTree);

    void update(U64 deltaTimeUS);
    void getAllRegions(vector<BoundingBox>& regionsOut) const;

    [[nodiscard]] const BoundingBox& getRegion() const noexcept { return _region; }

protected:
    friend class Octree;
    [[nodiscard]] U8 activeNodes() const noexcept;

    void buildTree();
    void insert(const SceneGraphNode* object);
    void findEnclosingBox();
    void findEnclosingCube();

    [[nodiscard]] vector<IntersectionRecord> getIntersection(const Frustum& frustum, U16 typeFilterMask) const;
    [[nodiscard]] vector<IntersectionRecord> getIntersection(const Ray& intersectRay, F32 start, F32 end, U16 typeFilterMask) const;

    [[nodiscard]] size_t getTotalObjectCount() const;
    void updateIntersectionCache(vector<const SceneGraphNode*>& parentObjects);
    
    [[nodiscard]] bool getIntersection(BoundsComponent* bComp, const Frustum& frustum, IntersectionRecord& irOut) const noexcept;
    [[nodiscard]] bool getIntersection(BoundsComponent* bComp1, BoundsComponent* bComp2, IntersectionRecord& irOut) const noexcept;
    [[nodiscard]] bool getIntersection(BoundsComponent* bComp, const Ray& intersectRay, F32 start, F32 end, IntersectionRecord& irOut) const noexcept;
    
    PROPERTY_R_IW(bool, active, false);

private:
    Octree* const _parentTree = nullptr;
    OctreeNode* const _parent = nullptr;
    const U16 _nodeExclusionMask = 0u;

    mutable SharedMutex _objectLock;
    eastl::fixed_vector<const SceneGraphNode*, 8, true, eastl::dvd_allocator> _objects;

    moodycamel::ConcurrentQueue<const SceneGraphNode*> _movedObjects;

    vector<IntersectionRecord> _intersectionsCache;

    BoundingBox _region;
    I32 _curLife = -1;
    I32 _maxLifespan = MAX_LIFE_SPAN_LIMIT / 8;

    std::array<OctreeNode_uptr, 8> _childNodes;

    //ToDo: make this work in a multi-threaded environment
    mutable I8 _frustPlaneCache = -1;
};

FWD_DECLARE_MANAGED_CLASS(OctreeNode);

};  // namespace Divide

#endif