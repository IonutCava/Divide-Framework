#include "stdafx.h"

#include "Headers/Octree.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"

namespace Divide {


bool Octree::s_treeReady = false;
bool Octree::s_treeBuilt = false;

Mutex Octree::s_pendingInsertLock;
eastl::queue<SceneGraphNode*> Octree::s_pendingInsertion;
eastl::queue<SceneGraphNode*> Octree::s_pendingRemoval;
vector<const SceneGraphNode*> Octree::s_intersectionsObjectCache;

Octree::Octree(Octree* parent, const U16 nodeMask)
    : _parent(parent),
      _region { VECTOR3_ZERO, VECTOR3_ZERO },
      _nodeExclusionMask(nodeMask)
{
}

void Octree::update(const U64 deltaTimeUS) {
    if (!s_treeBuilt) {
        buildTree();
        return;
    }

    // prune any dead objects from the tree
    {
        ScopedLock<SharedMutex> w_lock(_objectLock);
        erase_if(_objects,
                 [](const SceneGraphNode* crtNode) noexcept -> bool {
                     return !crtNode || !crtNode->hasFlag(SceneGraphNode::Flags::ACTIVE);
                 });
    }
    {
        SharedLock<SharedMutex> r_lock(_objectLock);
        if (_objects.empty()) {
            if (activeNodes() == 0) {
                if (_curLife == -1) {
                    _curLife = _maxLifespan;
                } else if (_curLife > 0) {
                    if (_maxLifespan <= MAX_LIFE_SPAN_LIMIT) {
                        _maxLifespan *= 2;
                    }
                    --_curLife;
                }
            }
        } else {
            if (_curLife != -1) {
                _curLife = -1;
            }
        }
    }
    //recursively update any child nodes.
    for (auto& child : _childNodes) {
        if (child && child->active()) {
            child->update(deltaTimeUS);
        }
    }

    //If an object moved, we can insert it into the parent and that will insert it into the correct tree node.
    //note that we have to do this last so that we don't accidentally update the same object more than once per frame.
    const SceneGraphNode* movedObjPtr = nullptr;
    while(_movedObjects.try_dequeue(movedObjPtr)) {
        Octree*  current = this;

        //figure out how far up the tree we need to go to reinsert our moved object
        //try to move the object into an enclosing parent node until we've got full containment
        const SceneGraphNode* movedObj = movedObjPtr;
        assert(movedObj);

        const BoundingBox& bb = movedObj->get<BoundsComponent>()->getBoundingBox();
        while(!current->_region.containsBox(bb)) {
            if (current->_parent != nullptr) {
                current = current->_parent;
            } else {
                break; //prevent infinite loops when we go out of bounds of the root node region
            }
        }

        //now, remove the object from the current node and insert it into the current containing node.
        {
            ScopedLock<SharedMutex> w_lock(_objectLock);
            erase_if(_objects,
                     [guid = movedObj->getGUID()](const SceneGraphNode* updatedNode) noexcept -> bool {
                         return updatedNode && updatedNode->getGUID() == guid;
                     });
        }
        //this will try to insert the object as deep into the tree as we can go.
        current->insert(movedObj);
    }

    //prune out any dead branches in the tree
    for (auto& child : _childNodes) {
        if (child && child->active() && child->_curLife == 0) {
            child->active(false);
        }
    }
    // root node
    if (_parent == nullptr) {
        s_intersectionsObjectCache.resize(0);
        s_intersectionsObjectCache.reserve(getTotalObjectCount());

        updateIntersectionCache(s_intersectionsObjectCache);

        for(const IntersectionRecord& ir : _intersectionsCache) {
            handleIntersection(ir);
        }
    }
}

bool Octree::addNode(SceneGraphNode* node) const {
    const U16 nodeType = 1 << to_U16(node->getNode<>().type());
    if (node && !BitCompare(_nodeExclusionMask, nodeType) && node->get<BoundsComponent>()) {
        ScopedLock<Mutex> w_lock(s_pendingInsertLock);
        s_pendingInsertion.push(node);
        s_treeReady = false;
        return true;
    }

    return false;
}

bool Octree::removeNode(SceneGraphNode* node) const {
    ScopedLock<Mutex> w_lock(s_pendingInsertLock);
    s_pendingRemoval.push(node);
    s_treeReady = false;
    return true;
}

/// A tree has already been created, so we're going to try to insert an item into the tree without rebuilding the whole thing
void Octree::insert(const SceneGraphNode* object) {
    const vec3<F32> dimensions(_region.getExtent());
    {
        ScopedLock<SharedMutex> w_lock(_objectLock);

        /*make sure we're not inserting an object any deeper into the tree than we have to.
        -if the current node is an empty leaf node, just insert and leave it.*/
        if (_objects.size() <= 1 && activeNodes() == 0) {
            _objects.push_back(object);
            return;
        }

        //Check to see if the dimensions of the box are greater than the minimum dimensions
        if (dimensions.x <= MIN_SIZE && dimensions.y <= MIN_SIZE && dimensions.z <= MIN_SIZE) {
            _objects.push_back(object);
            return;
        }
    }
    const BoundingBox& bb = object->get<BoundsComponent>()->getBoundingBox();

    //First, is the item completely contained within the root bounding box?
    //note2: I shouldn't actually have to compensate for this. If an object is out of our predefined bounds, then we have a problem/error.
    //          Wrong. Our initial bounding box for the terrain is constricting its height to the highest peak. Flying units will be above that.
    //             Fix: I resized the enclosing box to 256x256x256. This should be sufficient.
    if (_region.containsBox(bb)) {
        const vec3<F32> half(dimensions * 0.5f);
        const vec3<F32> center(_region.getMin() + half);

        //Find or create subdivided regions for each octant in the current region
        BoundingBox childOctant[8];
        const vec3<F32>& regMin = _region.getMin();
        const vec3<F32>& regMax = _region.getMax();
        childOctant[0].set(_childNodes[0] && _childNodes[0]->active() ? _childNodes[0]->_region : BoundingBox(regMin, center));
        childOctant[1].set(_childNodes[1] && _childNodes[1]->active() ? _childNodes[1]->_region : BoundingBox(vec3<F32>(center.x, regMin.y, regMin.z), vec3<F32>(regMax.x, center.y, center.z)));
        childOctant[2].set(_childNodes[2] && _childNodes[2]->active() ? _childNodes[2]->_region : BoundingBox(vec3<F32>(center.x, regMin.y, center.z), vec3<F32>(regMax.x, center.y, regMax.z)));
        childOctant[3].set(_childNodes[3] && _childNodes[3]->active() ? _childNodes[3]->_region : BoundingBox(vec3<F32>(regMin.x, regMin.y, center.z), vec3<F32>(center.x, center.y, regMax.z)));
        childOctant[4].set(_childNodes[4] && _childNodes[4]->active() ? _childNodes[4]->_region : BoundingBox(vec3<F32>(regMin.x, center.y, regMin.z), vec3<F32>(center.x, regMax.y, center.z)));
        childOctant[5].set(_childNodes[5] && _childNodes[5]->active() ? _childNodes[5]->_region : BoundingBox(vec3<F32>(center.x, center.y, regMin.z), vec3<F32>(regMax.x, regMax.y, center.z)));
        childOctant[6].set(_childNodes[6] && _childNodes[6]->active() ? _childNodes[6]->_region : BoundingBox(center, regMax));
        childOctant[7].set(_childNodes[7] && _childNodes[7]->active() ? _childNodes[7]->_region : BoundingBox(vec3<F32>(regMin.x, center.y, center.z), vec3<F32>(center.x, regMax.y, regMax.z)));

        bool found = false;
        //we will try to place the object into a child node. If we can't fit it in a child node, then we insert it into the current node object list.
        for (U8 i = 0u; i < 8u; ++i)  {
            auto& child = _childNodes[i];
            //is the object fully contained within a quadrant?
            if (childOctant[i].containsBox(bb)) {
                if (!child) {
                    child = eastl::make_unique<Octree>(this, _nodeExclusionMask);
                }
                if (child->active()) {
                    child->insert(object);   //Add the item into that tree and let the child tree figure out what to do with it
                } else {
                    if (object != nullptr) {
                        child->_region = childOctant[i];
                        {
                            ScopedLock<SharedMutex> w_lock(child->_objectLock);
                            child->_objects.resize(0);
                            child->_objects.push_back(object);
                        }
                        child->active(true);
                        child->buildTree();
                    } else {
                        child->active(false);
                    }
                }
                found = true;
            }
        }
        if (!found) {
            ScopedLock<SharedMutex> w_lock(_objectLock);
            _objects.push_back(object);
        }
    } else {
        //either the item lies outside of the enclosed bounding box or it is intersecting it. Either way, we need to rebuild
        //the entire tree by enlarging the containing bounding box
        buildTree();
    }
}

void Octree::onNodeMoved(const SceneGraphNode& sgn) {
    {
        SharedLock<SharedMutex> r_lock(_objectLock);
        //go through and update every object in the current tree node
        const I64 targetGUID = sgn.getGUID();
        for (const SceneGraphNode* crtNode : _objects) {
            if (crtNode->getGUID() != targetGUID) {
                continue;
            }

            _movedObjects.enqueue(&sgn);
            return;
        }
    }

    //recursively update any child nodes.
    for (auto& child : _childNodes) {
        if (child && child->active()) {
            child->onNodeMoved(sgn);
        }
    }
}

/// Naively builds an oct tree from scratch.
void Octree::buildTree() {
    // terminate the recursion if we're a leaf node
    {
        SharedLock<SharedMutex> w_lock(_objectLock);
        if (_objects.size() <= 1) {
            return;
        }
    }

    vec3<F32> dimensions(_region.getExtent());

    if (dimensions == VECTOR3_ZERO) {
        findEnclosingCube();
        dimensions.set(_region.getExtent());
    }

    if (dimensions.x <= MIN_SIZE || dimensions.y <= MIN_SIZE || dimensions.z <= MIN_SIZE) {
        return;
    }

    const vec3<F32> half(dimensions * 0.5f);
    const vec3<F32> regionMin(_region.getMin());
    const vec3<F32> regionMax(_region.getMax());
    const vec3<F32> center(regionMin + half);

    BoundingBox octant[8];
    octant[0].set(regionMin, center);
    octant[1].set(vec3<F32>(center.x, regionMin.y, regionMin.z), vec3<F32>(regionMax.x, center.y, center.z));
    octant[2].set(vec3<F32>(center.x, regionMin.y, center.z), vec3<F32>(regionMax.x, center.y, regionMax.z));
    octant[3].set(vec3<F32>(regionMin.x, regionMin.y, center.z), vec3<F32>(center.x, center.y, regionMax.z));
    octant[4].set(vec3<F32>(regionMin.x, center.y, regionMin.z), vec3<F32>(center.x, regionMax.y, center.z));
    octant[5].set(vec3<F32>(center.x, center.y, regionMin.z), vec3<F32>(regionMax.x, regionMax.y, center.z));
    octant[6].set(center, regionMax);
    octant[7].set(vec3<F32>(regionMin.x, center.y, center.z), vec3<F32>(center.x, regionMax.y, regionMax.z));

    //This will contain all of our objects which fit within each respective octant.
    vector<const SceneGraphNode*> octList[8];
    for (auto& list : octList) {
        list.reserve(8);
    }

    //this list contains all of the objects which got moved down the tree and can be delisted from this node.
    vector<I64> delist;
    delist.reserve(8);

    {
        SharedLock<SharedMutex> r_lock(_objectLock);
        for (const SceneGraphNode* obj : _objects) {
            if (obj) {
                const BoundingBox& bb = obj->get<BoundsComponent>()->getBoundingBox();
                for (U8 i = 0u; i < 8u; ++i) {
                    if (octant[i].containsBox(bb)) {
                        octList[i].push_back(obj);
                        delist.push_back(obj->getGUID());
                        break;
                    }
                }

            }
        }
    }
    {
        ScopedLock<SharedMutex> w_lock(_objectLock);
        //delist every moved object from this node.
        erase_if(_objects,
                 [&delist](const SceneGraphNode* movedNode) noexcept -> bool {
                     if (movedNode) {
                         for (const I64 guid : delist) {
                             if (guid == movedNode->getGUID()) {
                                 return true;
                             }
                         }
                     }
                     return false;
                 });
    }
    //Create child nodes where there are items contained in the bounding region
    for (U8 i = 0u; i < 8u; ++i) {
        auto& child = _childNodes[i];
        if (!octList[i].empty()) {
            if (!child) {
                child = eastl::make_unique<Octree>(this, _nodeExclusionMask);
            }
            child->_region = octant[i];
            {
                ScopedLock<SharedMutex> w_lock(child->_objectLock);
                child->_objects.resize(0);
                child->_objects.reserve(octList[i].size());
                child->_objects.insert(cend(child->_objects), cbegin(octList[i]), cend(octList[i]));
            }
            child->active(true);
            child->buildTree();
        } else {
            if (child) {
                child->active(false);
            }
        }
    }

    s_treeBuilt = true;
    s_treeReady = true;
}

void Octree::findEnclosingBox() noexcept
{
    SharedLock<SharedMutex> r_lock(_objectLock);

    vec3<F32> globalMin(_region.getMin());
    vec3<F32> globalMax(_region.getMax());

    //go through all the objects in the list and find the extremes for their bounding areas.
    for (const SceneGraphNode* obj : _objects) {
        if (obj) {
            const BoundingBox& bb = obj->get<BoundsComponent>()->getBoundingBox();
            const vec3<F32>& localMin = bb.getMin();
            const vec3<F32>& localMax = bb.getMax();

            if (localMin.x < globalMin.x) {
                globalMin.x = localMin.x;
            }

            if (localMin.y < globalMin.y) {
                globalMin.y = localMin.y;
            }

            if (localMin.z < globalMin.z) {
                globalMin.z = localMin.z;
            }

            if (localMax.x > globalMax.x) {
                globalMax.x = localMax.x;
            }

            if (localMax.y > globalMax.y) {
                globalMax.y = localMax.y;
            }

            if (localMax.z > globalMax.z) {
                globalMax.z = localMax.z;
            }
        }
    }

    _region.setMin(globalMin);
    _region.setMax(globalMax);
}

/// This finds the smallest enclosing cube which is a power of 2, for all objects in the list.
void Octree::findEnclosingCube() noexcept {
    findEnclosingBox();

    //we can't guarantee that all bounding regions will be relative to the origin, so to keep the math
    //simple, we're going to translate the existing region to be oriented off the origin and remember the translation.
    //find the min offset from (0,0,0) and translate by it for a short while
    const vec3<F32> offset(_region.getMin() - VECTOR3_ZERO);
    _region.translate(offset);
    const vec3<F32>& regionMax = _region.getMax();
    //A 3D rectangle has a length, height, and width. Of those three dimensions, we want to find the largest dimension.
    //the largest dimension will be the minimum dimensions of the cube we're creating.
    const I32 highX = to_I32(std::floor(std::max(std::max(regionMax.x, regionMax.y), regionMax.z)));

    //see if our cube dimension is already at a power of 2. If it is, we don't have to do any work.
    for (I32 bit = 0; bit < 32; ++bit) {
        if (highX == 1 << bit) {
            _region.setMax(to_F32(highX));
            _region.translate(-offset);
            return;
        }
    }

    //We have a cube with non-power of two dimensions. We want to find the next highest power of two.
    //example: 63 -> 64; 65 -> 128;
    _region.setMax(to_F32(nextPOW2(highX)));
    _region.translate(-offset);
}

void Octree::updateTree() {
    ScopedLock<Mutex> w_lock1(s_pendingInsertLock);

    bool needsRebuild = false;
    if (!s_treeBuilt) {
        ScopedLock<SharedMutex> w_lock2(_objectLock);
        while (!s_pendingInsertion.empty()) {
            _objects.push_back(s_pendingInsertion.front());
            s_pendingInsertion.pop();
        }
        needsRebuild = true;

    } else {
        while (!s_pendingInsertion.empty()) {
            insert(s_pendingInsertion.front());
            s_pendingInsertion.pop();
        }
    }

    //ToDo: We can optimise this by settings removed nodes to inactive! -Ionut
    {
        ScopedLock<SharedMutex> w_lock2(_objectLock);
        while (!s_pendingRemoval.empty()) {
            const SceneGraphNode* nodeToRemove = s_pendingRemoval.front();
            needsRebuild = dvd_erase_if(_objects, [targetGUID = nodeToRemove->getGUID()](const SceneGraphNode* node) {
                return node->getGUID() == targetGUID;
            }) || needsRebuild;
            s_pendingRemoval.pop();
        }
    }
    if (needsRebuild) {
        buildTree();
    }

    s_treeReady = true;
}

void Octree::getAllRegions(vector<BoundingBox>& regionsOut) const {
    for (const auto& child : _childNodes) {
        if (child && child->active()) {
            child->getAllRegions(regionsOut);
        }
    }
    
    regionsOut.emplace_back(getRegion().getMin(), getRegion().getMax());
}

U8 Octree::activeNodes() const noexcept {
    U8 ret = 0u;
    for (const auto& child : _childNodes) {
        ret += (child && child->active() ? 1u : 0u);
    }
    return ret;
}

size_t Octree::getTotalObjectCount() const {
    size_t count = 0u;
    {
        SharedLock<SharedMutex> w_lock(_objectLock);
        count += _objects.size();
    }
    for (const auto& child : _childNodes) {
        count += (child && child->active() ? child->getTotalObjectCount() : 0u);
    }
    return count;
}

/// Gives you a list of all intersection records which intersect or are contained within the given frustum area
vector<IntersectionRecord> Octree::getIntersection(const Frustum& frustum, const U16 typeFilterMask) const {

    vector<IntersectionRecord> ret{};

    SharedLock<SharedMutex> w_lock(_objectLock);
    //terminator for any recursion
    if (_objects.empty() == 0 && activeNodes() == 0) {   
        return ret;
    }

    //test each object in the list for intersection
    for (const SceneGraphNode* objPtr : _objects) {
        assert(objPtr);
        //skip any objects which don't meet our type criteria
        const U16 nodeType = 1 << to_U16(objPtr->getNode<>().type());
        if (BitCompare(typeFilterMask, nodeType)) {
            continue;
        }

        //test for intersection
        IntersectionRecord ir;
        if (getIntersection(objPtr->get<BoundsComponent>(), frustum, ir)) {
            ret.push_back(ir);
        }
    }

    //test each object in the list for intersection
    for (const auto& child : _childNodes) {
        I8 frustPlaneCache = -1;
        if (child &&
            child->active() &&
            frustum.ContainsBoundingBox(child->_region, frustPlaneCache) != FrustumCollision::FRUSTUM_OUT)
        {
            vector<IntersectionRecord> hitList = child->getIntersection(frustum, typeFilterMask);
            ret.insert(cend(ret), cbegin(hitList), cend(hitList));
        }
    }
    return ret;
}

/// Gives you a list of intersection records for all objects which intersect with the given ray
vector<IntersectionRecord> Octree::getIntersection(const Ray& intersectRay, const F32 start, const F32 end, const U16 typeFilterMask) const {
    SharedLock<SharedMutex> w_lock(_objectLock);

    vector<IntersectionRecord> ret{};

    //terminator for any recursion
    if (_objects.empty() == 0 && activeNodes() == 0) {
        return ret;
    }


    //the ray is intersecting this region, so we have to check for intersection with all of our contained objects and child regions.

    //test each object in the list for intersection
    for (const SceneGraphNode* objPtr : _objects) {
        assert(objPtr);
        //skip any objects which don't meet our type criteria
        const U16 nodeType = 1 << to_U16(objPtr->getNode<>().type());
        if (BitCompare(typeFilterMask, nodeType)) {
            continue;
        }

        if (objPtr->get<BoundsComponent>()->getBoundingBox().intersect(intersectRay, start, end).hit) {
            IntersectionRecord ir;
            if (getIntersection(objPtr->get<BoundsComponent>(), intersectRay, start, end, ir)) {
                ret.push_back(ir);
            }
        }
    }

    // test each child octant for intersection
    for (const auto& child : _childNodes) {
        if (child &&
            child->active() &&
            child->_region.intersect(intersectRay, start, end).hit) 
        {
            vector<IntersectionRecord> hitList = child->getIntersection(intersectRay, start, end, typeFilterMask);
            ret.insert(cend(ret), cbegin(hitList), cend(hitList));
        }
    }

    return ret;
}

void Octree::updateIntersectionCache(vector<const SceneGraphNode*>& parentObjects) {
    SharedLock<SharedMutex> w_lock(_objectLock);

    _intersectionsCache.resize(0);
    //assume all parent objects have already been processed for collisions against each other.
    //check all parent objects against all objects in our local node
    for (const SceneGraphNode* pObjPtr : parentObjects) {
        assert(pObjPtr);

        for (const SceneGraphNode* objPtr : _objects) {
            assert(objPtr);
            //We let the two objects check for collision against each other. They can figure out how to do the coarse and granular checks.
            //all we're concerned about is whether or not a collision actually happened.
            IntersectionRecord ir;
            if (getIntersection(pObjPtr->get<BoundsComponent>(), objPtr->get<BoundsComponent>(), ir)) {
                bool found = false;
                for (const IntersectionRecord& irTemp : _intersectionsCache) {
                    if (irTemp == ir) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    _intersectionsCache.push_back(ir);
                }
            }
        }
    }

    const auto isStatic = [](const SceneGraphNode * node) noexcept {
        return node->usageContext() == NodeUsageContext::NODE_STATIC;
    };

    //now, check all our local objects against all other local objects in the node
    if (_objects.size() > 1) {
        decltype(_objects) tmp(_objects);
        while (!tmp.empty()) {
            for(const SceneGraphNode* lObj2Ptr : tmp) {
                assert(lObj2Ptr);
                const SceneGraphNode* lObj1 = tmp[tmp.size() - 1];
                assert(lObj1);
                if (lObj1->getGUID() == lObj2Ptr->getGUID() || isStatic(lObj1) && isStatic(lObj2Ptr)) {
                    continue;
                }
                IntersectionRecord ir;
                if (!lObj1->isRelated(lObj2Ptr) && getIntersection(lObj1->get<BoundsComponent>(), lObj2Ptr->get<BoundsComponent>(), ir)) {
                    _intersectionsCache.push_back(ir);
                }
            }

            //remove this object from the temp list so that we can run in O(N(N+1)/2) time instead of O(N*N)
            tmp.pop_back();
        }
    }

    //now, merge our local objects list with the parent objects list, then pass it down to all children.
    for(const SceneGraphNode* lObjPtr : _objects) {
        if (lObjPtr && !isStatic(lObjPtr)) {
            parentObjects.push_back(lObjPtr);
        }
    }

    //each child node will give us a list of intersection records, which we then merge with our own intersection records.
    for (auto& child : _childNodes) {
        if (child &&
            child->active()) {
            child->updateIntersectionCache(parentObjects);
            const vector<IntersectionRecord>& hitList = child->_intersectionsCache;
            _intersectionsCache.insert(cend(_intersectionsCache), cbegin(hitList), cend(hitList));
        }
    }
}

/// This gives you a list of every intersection record created with the intersection ray
vector<IntersectionRecord> Octree::allIntersections(const Ray& intersectionRay, const F32 start, const F32 end) {
    return allIntersections(intersectionRay, start, end, ~_nodeExclusionMask);
}

/// This gives you the first object encountered by the intersection ray
IntersectionRecord Octree::nearestIntersection(const Ray& intersectionRay, const F32 start, const F32 end, const U16 typeFilterMask) {
    if (!s_treeReady) {
        updateTree();
    }

    vector<IntersectionRecord> intersections = getIntersection(intersectionRay, start, end, typeFilterMask);

    IntersectionRecord nearest;

    for(const IntersectionRecord& ir : intersections) {
        if (!nearest._hasHit) {
            nearest = ir;
            continue;
        }

        if (ir._distance < nearest._distance) {
            nearest = ir;
        }
    }

    return nearest;
}

/// This gives you a list of all intersections, filtered by a specific type of object
vector<IntersectionRecord> Octree::allIntersections(const Ray& intersectionRay, const F32 start, const F32 end, const U16 typeFilterMask) {
    if (!s_treeReady) {
        updateTree();
    }

    return getIntersection(intersectionRay, start, end, typeFilterMask);
}

/// This gives you a list of all objects which [intersect or are contained within] the given frustum and meet the given object type
vector<IntersectionRecord> Octree::allIntersections(const Frustum& region, const U16 typeFilterMask) {
    if (!s_treeReady) {
        updateTree();
    }

    return getIntersection(region, typeFilterMask);
}

void Octree::handleIntersection(const IntersectionRecord& intersection) const {
    const BoundsComponent* obj1 = intersection._intersectedObject1;
    const BoundsComponent* obj2 = intersection._intersectedObject2;
    if (obj1 != nullptr && obj2 != nullptr) {
        // Check for child / parent relation
        if(obj1->parentSGN()->isRelated(obj2->parentSGN())) {
            return;
        }

        RigidBodyComponent* comp1 = obj1->parentSGN()->get<RigidBodyComponent>();
        RigidBodyComponent* comp2 = obj2->parentSGN()->get<RigidBodyComponent>();

        if (comp1 && comp2) {
            comp1->onCollision(*comp2);
            comp2->onCollision(*comp1);
        }
    }
}

bool Octree::getIntersection(BoundsComponent* bComp, const Frustum& frustum, IntersectionRecord& irOut) const noexcept {
    _frustPlaneCache = -1; //ToDo: Fix this caching system to work with multithreading -Ionut
    if (frustum.ContainsBoundingBox(bComp->getBoundingBox(), _frustPlaneCache) != FrustumCollision::FRUSTUM_OUT) {
        irOut.reset();
        irOut._intersectedObject1 = bComp;
        irOut._treeNode = this;
        irOut._hasHit = true;
        return true;
    }

    return false;
}

bool Octree::getIntersection(BoundsComponent* boundsNode1, BoundsComponent* boundsNode2, IntersectionRecord& irOut) const noexcept {
    if (boundsNode1->parentSGN()->getGUID() != boundsNode2->parentSGN()->getGUID()) {
        if (boundsNode1->getBoundingSphere().collision(boundsNode2->getBoundingSphere())) {
            if (boundsNode1->getBoundingBox().collision(boundsNode2->getBoundingBox())) {
                irOut.reset();
                irOut._intersectedObject1 = boundsNode1;
                irOut._intersectedObject2 = boundsNode2;
                irOut._treeNode = this;
                irOut._hasHit = true;
                return true;
            }
        }
    }

    return false;
}

bool Octree::getIntersection(BoundsComponent* bComp, const Ray& intersectRay, const F32 start, const F32 end, IntersectionRecord& irOut) const noexcept {
    if (bComp->getBoundingBox().intersect(intersectRay, start, end).hit) {
        irOut.reset();
        irOut._intersectedObject1 = bComp;
        irOut._ray = intersectRay;
        irOut._treeNode = this;
        irOut._hasHit = true;
        return true;
    }

    return false;
}

}; //namespace Divide
