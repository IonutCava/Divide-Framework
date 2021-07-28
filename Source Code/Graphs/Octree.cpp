#include "stdafx.h"

#include "Headers/Octree.h"
#include "ECS/Components//Headers/BoundsComponent.h"
#include "ECS/Components//Headers/RigidBodyComponent.h"

namespace Divide {


bool Octree::s_treeReady = false;
bool Octree::s_treeBuilt = false;

Mutex Octree::s_pendingInsertLock;
eastl::queue<SceneGraphNode*> Octree::s_pendingInsertion;
vectorEASTL<SceneGraphNode*> Octree::s_intersectionsObjectCache;

Octree::Octree(Octree* parent, const U16 nodeExclusionMask)
    : _parent(parent),
      _nodeExclusionMask(nodeExclusionMask)
{
    _childNodes.resize(8);
    _region.set(VECTOR3_ZERO, VECTOR3_ZERO);
}

Octree::Octree(Octree* parent, const U16 nodeMask, const BoundingBox& rootAABB)
    : Octree(parent, nodeMask)
{
    _region.set(rootAABB);
}

Octree::Octree(Octree* parent,
               const U16 nodeExclusionMask,
               const BoundingBox& rootAABB,
               const vectorEASTL<SceneGraphNode*>& nodes)
    :  Octree(parent, nodeExclusionMask, rootAABB)
{
    _objects.reserve(nodes.size());
    _objects.insert(cend(_objects), cbegin(nodes), cend(nodes));
}

void Octree::update(const U64 deltaTimeUS) {
    if (!s_treeBuilt) {
        buildTree();
        return;
    }
    if (_objects.empty()) {
        if (activeNodes() == 0) {
            if (_curLife == -1) {
                _curLife = _maxLifespan;
            } else if (_curLife > 0) {
                if (_maxLifespan <= MAX_LIFE_SPAN_LIMIT) {
                    _maxLifespan *= 2;
                }
                _curLife--;
            }
        }
    } else {
        if (_curLife != -1) {
            _curLife = -1;
        }
    }

    // prune any dead objects from the tree
    erase_if(_objects,
             [](SceneGraphNode* crtNode) -> bool {
                 SceneGraphNode* node = crtNode;
                 return !node || !node->hasFlag(SceneGraphNode::Flags::ACTIVE);
             });

    //recursively update any child nodes.
    for (U8 i = 0; i < 8; ++i) {
        if (_activeNodes[i]) {
            _childNodes[i].update(deltaTimeUS);
        }
    }

    //If an object moved, we can insert it into the parent and that will insert it into the correct tree node.
    //note that we have to do this last so that we don't accidentally update the same object more than once per frame.
    SceneGraphNode* movedObjPtr = nullptr;
    while(_movedObjects.try_dequeue(movedObjPtr)) {
        Octree*  current = this;

        //figure out how far up the tree we need to go to reinsert our moved object
        //try to move the object into an enclosing parent node until we've got full containment
        SceneGraphNode* movedObj = movedObjPtr;
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
        const I64 guid = movedObj->getGUID();
        erase_if(_objects,
                 [guid](SceneGraphNode* updatedNode) -> bool {
                     SceneGraphNode* node = updatedNode;
                     return node && node->getGUID() == guid;
                 });

        //this will try to insert the object as deep into the tree as we can go.
        current->insert(movedObj);
    }

    //prune out any dead branches in the tree
    for (U8 i = 0; i < 8; ++i) {
        if (_activeNodes[i] && _childNodes[i]._curLife == 0) {
            _activeNodes[i] = false;
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
    if (node && !BitCompare(_nodeExclusionMask, nodeType)) {
        UniqueLock<Mutex> w_lock(s_pendingInsertLock);
        s_pendingInsertion.push(node);
        s_treeReady = false;
        return true;
    }

    return false;
}

bool Octree::addNodes(const vectorEASTL<SceneGraphNode*>& nodes) {
    bool changed = false;
    for (SceneGraphNode* node : nodes) {
        if (addNode(node)) {
            changed = true;
        }
    }

    return changed;
}

/// A tree has already been created, so we're going to try to insert an item into the tree without rebuilding the whole thing
void Octree::insert(SceneGraphNode* object) {
    /*make sure we're not inserting an object any deeper into the tree than we have to.
    -if the current node is an empty leaf node, just insert and leave it.*/
    if (_objects.size() <= 1 && activeNodes() == 0) {
        _objects.push_back(object);
        return;
    }

    const vec3<F32> dimensions(_region.getExtent());
    //Check to see if the dimensions of the box are greater than the minimum dimensions
    if (dimensions.x <= MIN_SIZE && dimensions.y <= MIN_SIZE && dimensions.z <= MIN_SIZE) {
        _objects.push_back(object);
        return;
    }

    const vec3<F32> half(dimensions * 0.5f);
    const vec3<F32> center(_region.getMin() + half);

    //Find or create subdivided regions for each octant in the current region
    BoundingBox childOctant[8];
    const vec3<F32>& regMin = _region.getMin();
    const vec3<F32>& regMax = _region.getMax();
    childOctant[0].set(_activeNodes[0] ? _childNodes[0]._region : BoundingBox(regMin,                                  center));
    childOctant[1].set(_activeNodes[1] ? _childNodes[1]._region : BoundingBox(vec3<F32>(center.x, regMin.y, regMin.z), vec3<F32>(regMax.x, center.y, center.z)));
    childOctant[2].set(_activeNodes[2] ? _childNodes[2]._region : BoundingBox(vec3<F32>(center.x, regMin.y, center.z), vec3<F32>(regMax.x, center.y, regMax.z)));
    childOctant[3].set(_activeNodes[3] ? _childNodes[3]._region : BoundingBox(vec3<F32>(regMin.x, regMin.y, center.z), vec3<F32>(center.x, center.y, regMax.z)));
    childOctant[4].set(_activeNodes[4] ? _childNodes[4]._region : BoundingBox(vec3<F32>(regMin.x, center.y, regMin.z), vec3<F32>(center.x, regMax.y, center.z)));
    childOctant[5].set(_activeNodes[5] ? _childNodes[5]._region : BoundingBox(vec3<F32>(center.x, center.y, regMin.z), vec3<F32>(regMax.x, regMax.y, center.z)));
    childOctant[6].set(_activeNodes[6] ? _childNodes[6]._region : BoundingBox(center,                                   regMax));
    childOctant[7].set(_activeNodes[7] ? _childNodes[7]._region : BoundingBox(vec3<F32>(regMin.x, center.y, center.z), vec3<F32>(center.x, regMax.y, regMax.z)));

    const BoundingBox& bb = object->get<BoundsComponent>()->getBoundingBox();

    //First, is the item completely contained within the root bounding box?
    //note2: I shouldn't actually have to compensate for this. If an object is out of our predefined bounds, then we have a problem/error.
    //          Wrong. Our initial bounding box for the terrain is constricting its height to the highest peak. Flying units will be above that.
    //             Fix: I resized the enclosing box to 256x256x256. This should be sufficient.
    if (_region.containsBox(bb)) {
        bool found = false;
        //we will try to place the object into a child node. If we can't fit it in a child node, then we insert it into the current node object list.
        for (U8 i = 0; i < 8; ++i)  {
            //is the object fully contained within a quadrant?
            if (childOctant[i].containsBox(bb)) {
                if (_activeNodes[i]) {
                    _childNodes[i].insert(object);   //Add the item into that tree and let the child tree figure out what to do with it
                } else {
                    _activeNodes[i] = createNode(_childNodes[i], childOctant[i], object);   //create a new tree node with the item
                }
                found = true;
            }
        }
        if (!found) {
            _objects.push_back(object);
        }
    } else {
        //either the item lies outside of the enclosed bounding box or it is intersecting it. Either way, we need to rebuild
        //the entire tree by enlarging the containing bounding box
        buildTree();
    }
}

void Octree::onNodeMoved(const SceneGraphNode& node) {
    //go through and update every object in the current tree node
    const I64 targetGUID = node.getGUID();
    for (SceneGraphNode* crtNode : _objects) {
        if (crtNode->getGUID() != targetGUID) {
            continue;
        }

        _movedObjects.enqueue(crtNode);
        return;
    }

    //recursively update any child nodes.
    for (U8 i = 0u; i < 8u; ++i) {
        if (_activeNodes[i]) {
            _childNodes[i].onNodeMoved(node);
        }
    }
}

/// Naively builds an oct tree from scratch.
void Octree::buildTree() {
    // terminate the recursion if we're a leaf node
    if (_objects.size() <= 1) {
        return;
    }

    vec3<F32> dimensions(_region.getExtent());

    if (dimensions == VECTOR3_ZERO) {
        findEnclosingCube();
        dimensions.set(_region.getExtent());
    }

    if (dimensions.x <= MIN_SIZE || dimensions.y <= MIN_SIZE || dimensions.z <= MIN_SIZE) {
        return;
    }

    vec3<F32> half(dimensions * 0.5f);
    vec3<F32> regionMin(_region.getMin());
    vec3<F32> regionMax(_region.getMax());
    vec3<F32> center(regionMin + half);

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
    vectorEASTL<SceneGraphNode*> octList[8];
    for (auto& list : octList) {
        list.reserve(8);
    }

    //this list contains all of the objects which got moved down the tree and can be delisted from this node.
    vectorEASTL<I64> delist;
    delist.reserve(8);

    for (SceneGraphNode* obj : _objects) {
        if (obj) {
            const BoundingBox& bb = obj->get<BoundsComponent>()->getBoundingBox();
            for (U8 i = 0; i < 8; ++i) {
                if (octant[i].containsBox(bb)) {
                    octList[i].push_back(obj);
                    delist.push_back(obj->getGUID());
                    break;
                }
            }
            
        }
    }

    //delist every moved object from this node.
    erase_if(_objects,
             [&delist](SceneGraphNode* movedNode) -> bool {
                 if (movedNode) {
                     for (I64 guid : delist) {
                         if (guid == movedNode->getGUID()) {
                             return true;
                         }
                     }
                 }
                 return false;
             });

    //Create child nodes where there are items contained in the bounding region
    for (U8 i = 0; i < 8; ++i) {
        if (createNode(_childNodes[i], octant[i], octList[i])) {
            _activeNodes[i] = true;
            _childNodes[i].buildTree();
        }
    }

    s_treeBuilt = true;
    s_treeReady = true;
}

bool Octree::createNode(Octree& newNode, const BoundingBox& region, const vectorEASTL<SceneGraphNode*>& objects) {
    if (!objects.empty()) {
        newNode = Octree(this, _nodeExclusionMask, region, objects);
        return true;
    }
    return false;
}

bool Octree::createNode(Octree& newNode, const BoundingBox& region, SceneGraphNode* object) {
    if (object != nullptr) {
        vectorEASTL<SceneGraphNode*> objList;
        objList.push_back(object);
        return createNode(newNode, region, objList);
    }

    return false;
}

void Octree::findEnclosingBox()
{
    vec3<F32> globalMin(_region.getMin());
    vec3<F32> globalMax(_region.getMax());

    //go through all the objects in the list and find the extremes for their bounding areas.
    for (SceneGraphNode* obj : _objects) {
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
void Octree::findEnclosingCube() {
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
    UniqueLock<Mutex> w_lock(s_pendingInsertLock);
    if (!s_treeBuilt) {
        while (!s_pendingInsertion.empty()) {
            _objects.push_back(s_pendingInsertion.front());
            s_pendingInsertion.pop();
        }
        buildTree();

    } else {
        while (!s_pendingInsertion.empty()) {
            insert(s_pendingInsertion.front());
            s_pendingInsertion.pop();
        }
    }
    s_treeReady = true;
}

void Octree::getAllRegions(vectorEASTL<BoundingBox>& regionsOut) const {
    for (U8 i = 0u; i < 8u; ++i) {
        if (_activeNodes[i]) {
            _childNodes[i].getAllRegions(regionsOut);
        }
    }
    
    regionsOut.emplace_back(getRegion().getMin(), getRegion().getMax());
}

U8 Octree::activeNodes() const {
    U8 ret = 0u;
    for (const bool state : _activeNodes) {
        ret += (state ? 1u : 0u);
    }
    return ret;
}

size_t Octree::getTotalObjectCount() const {
    size_t count = _objects.size();
    for (U8 i = 0u; i < 8u; ++i) {
        if (_activeNodes[i]) {
            count += _childNodes[i].getTotalObjectCount();
        }
    }
    return count;
}

/// Gives you a list of all intersection records which intersect or are contained within the given frustum area
vectorEASTL<IntersectionRecord> Octree::getIntersection(const Frustum& frustum, const U16 typeFilterMask) const {

    vectorEASTL<IntersectionRecord> ret{};

    //terminator for any recursion
    if (_objects.empty() == 0 && activeNodes() == 0) {   
        return ret;
    }

    //test each object in the list for intersection
    for(SceneGraphNode* objPtr : _objects) {
        assert(objPtr);
        //skip any objects which don't meet our type criteria
        const U16 nodeType = 1 << to_U16(objPtr->getNode<>().type());
        if (BitCompare(typeFilterMask, nodeType)) {
            continue;
        }

        //test for intersection
        IntersectionRecord ir;
        if (getIntersection(objPtr, frustum, ir)) {
            ret.push_back(ir);
        }
    }

    //test each object in the list for intersection
    for (I32 i = 0; i < 8; ++i) {
        I8 frustPlaneCache = -1;
        if (_activeNodes[i] &&
            frustum.ContainsBoundingBox(_childNodes[i]._region, frustPlaneCache) != FrustumCollision::FRUSTUM_OUT)
        {
            vectorEASTL<IntersectionRecord> hitList = _childNodes[i].getIntersection(frustum, typeFilterMask);
            ret.insert(cend(ret), cbegin(hitList), cend(hitList));
        }
    }
    return ret;
}

/// Gives you a list of intersection records for all objects which intersect with the given ray
vectorEASTL<IntersectionRecord> Octree::getIntersection(const Ray& intersectRay, const F32 start, const F32 end, const U16 typeFilterMask) const {

    vectorEASTL<IntersectionRecord> ret{};

    //terminator for any recursion
    if (_objects.empty() == 0 && activeNodes() == 0) {
        return ret;
    }


    //the ray is intersecting this region, so we have to check for intersection with all of our contained objects and child regions.

    //test each object in the list for intersection
    for (SceneGraphNode* objPtr : _objects) {
        assert(objPtr);
        //skip any objects which don't meet our type criteria
        const U16 nodeType = 1 << to_U16(objPtr->getNode<>().type());
        if (BitCompare(typeFilterMask, nodeType)) {
            continue;
        }

        if (objPtr->get<BoundsComponent>()->getBoundingBox().intersect(intersectRay, start, end).hit) {
            IntersectionRecord ir;
            if (getIntersection(objPtr, intersectRay, start, end, ir)) {
                ret.push_back(ir);
            }
        }
    }

    // test each child octant for intersection
    for (I32 i = 0; i < 8; ++i) {
        if (_activeNodes[i] &&
            _childNodes[i]._region.intersect(intersectRay, start, end).hit) 
        {
            vectorEASTL<IntersectionRecord> hitList = _childNodes[i].getIntersection(intersectRay, start, end, typeFilterMask);
            ret.insert(cend(ret), cbegin(hitList), cend(hitList));
        }
    }

    return ret;
}

void Octree::updateIntersectionCache(vectorEASTL<SceneGraphNode*>& parentObjects)
{
    _intersectionsCache.resize(0);
    //assume all parent objects have already been processed for collisions against each other.
    //check all parent objects against all objects in our local node
    for(SceneGraphNode* pObjPtr : parentObjects)
    {
        assert(pObjPtr);

        for (SceneGraphNode* objPtr : _objects) {
            assert(objPtr);
            //We let the two objects check for collision against each other. They can figure out how to do the coarse and granular checks.
            //all we're concerned about is whether or not a collision actually happened.
            IntersectionRecord ir;
            if (getIntersection(pObjPtr, objPtr, ir)) {
                bool found = false;
                for (IntersectionRecord& irTemp : _intersectionsCache) {
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

    const auto isStatic = [](const SceneGraphNode * node) {
        return node->usageContext() == NodeUsageContext::NODE_STATIC;
    };

    //now, check all our local objects against all other local objects in the node
    if (_objects.size() > 1) {
        vectorEASTL<SceneGraphNode*> tmp(_objects);
        while (!tmp.empty()) {
            for(SceneGraphNode* lObj2Ptr : tmp) {
                assert(lObj2Ptr);
                SceneGraphNode* lObj1 = tmp[tmp.size() - 1];
                assert(lObj1);
                if (lObj1->getGUID() == lObj2Ptr->getGUID() || isStatic(lObj1) && isStatic(lObj2Ptr)) {
                    continue;
                }
                IntersectionRecord ir;
                if (!lObj1->isRelated(lObj2Ptr) && getIntersection(lObj1, lObj2Ptr, ir)) {
                    _intersectionsCache.push_back(ir);
                }
            }

            //remove this object from the temp list so that we can run in O(N(N+1)/2) time instead of O(N*N)
            tmp.pop_back();
        }
    }

    //now, merge our local objects list with the parent objects list, then pass it down to all children.
    for(SceneGraphNode* lObjPtr : _objects) {
        if (lObjPtr && !isStatic(lObjPtr)) {
            parentObjects.push_back(lObjPtr);
        }
    }

    //each child node will give us a list of intersection records, which we then merge with our own intersection records.
    for (U8 i = 0; i < 8; ++i) {
        if (_activeNodes[i]) {
            _childNodes[i].updateIntersectionCache(parentObjects);
            const vectorEASTL<IntersectionRecord>& hitList = _childNodes[i]._intersectionsCache;
            _intersectionsCache.insert(cend(_intersectionsCache), cbegin(hitList), cend(hitList));
        }
    }
}

/// This gives you a list of every intersection record created with the intersection ray
vectorEASTL<IntersectionRecord> Octree::allIntersections(const Ray& intersectionRay, const F32 start, const F32 end)
{
    return allIntersections(intersectionRay, start, end, ~_nodeExclusionMask);
}

/// This gives you the first object encountered by the intersection ray
IntersectionRecord Octree::nearestIntersection(const Ray& intersectionRay, const F32 start, const F32 end, const U16 typeFilterMask)
{
    if (!s_treeReady) {
        updateTree();
    }

    vectorEASTL<IntersectionRecord> intersections = getIntersection(intersectionRay, start, end, typeFilterMask);

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
vectorEASTL<IntersectionRecord> Octree::allIntersections(const Ray& intersectionRay, const F32 start, const F32 end, const U16 typeFilterMask)
{
    if (!s_treeReady) {
        updateTree();
    }

    return getIntersection(intersectionRay, start, end, typeFilterMask);
}

/// This gives you a list of all objects which [intersect or are contained within] the given frustum and meet the given object type
vectorEASTL<IntersectionRecord> Octree::allIntersections(const Frustum& region, const U16 typeFilterMask)
{
    if (!s_treeReady) {
        updateTree();
    }

    return getIntersection(region, typeFilterMask);
}

void Octree::handleIntersection(const IntersectionRecord& intersection) const {
    SceneGraphNode* obj1 = intersection._intersectedObject1;
    SceneGraphNode* obj2 = intersection._intersectedObject2;
    if (obj1 != nullptr && obj2 != nullptr) {
        // Check for child / parent relation
        if(obj1->isRelated(obj2)) {
            return;
        }
        RigidBodyComponent* comp1 = obj1->get<RigidBodyComponent>();
        RigidBodyComponent* comp2 = obj2->get<RigidBodyComponent>();

        if (comp1 && comp2) {
            comp1->onCollision(*comp2);
            comp2->onCollision(*comp1);
        }
    }
}

bool Octree::getIntersection(SceneGraphNode* node, const Frustum& frustum, IntersectionRecord& irOut) const {
    const BoundingBox& bb = node->get<BoundsComponent>()->getBoundingBox();

    STUBBED("ToDo: make this work in a multi-threaded environment -Ionut");
    _frustPlaneCache = -1;
    if (frustum.ContainsBoundingBox(bb, _frustPlaneCache) != FrustumCollision::FRUSTUM_OUT) {
        irOut.reset();
        irOut._intersectedObject1 = node;
        irOut._treeNode = this;
        irOut._hasHit = true;
        return true;
    }

    return false;
}

bool Octree::getIntersection(SceneGraphNode* node1, SceneGraphNode* node2, IntersectionRecord& irOut) const {
    if (node1->getGUID() != node2->getGUID()) {
        BoundsComponent* bComp1 = node1->get<BoundsComponent>();
        BoundsComponent* bComp2 = node2->get<BoundsComponent>();
        if (bComp1->getBoundingSphere().collision(bComp2->getBoundingSphere())) {
            if (bComp1->getBoundingBox().collision(bComp2->getBoundingBox())) {
                irOut.reset();
                irOut._intersectedObject1 = node1;
                irOut._intersectedObject2 = node2;
                irOut._treeNode = this;
                irOut._hasHit = true;
                return true;
            }
        }
    }

    return false;
}

bool Octree::getIntersection(SceneGraphNode* node, const Ray& intersectRay, const F32 start, const F32 end, IntersectionRecord& irOut) const {
    const BoundingBox& bb = node->get<BoundsComponent>()->getBoundingBox();
    if (bb.intersect(intersectRay, start, end).hit) {
        irOut.reset();
        irOut._intersectedObject1 = node;
        irOut._ray = intersectRay;
        irOut._treeNode = this;
        irOut._hasHit = true;
        return true;
    }

    return false;
}

}; //namespace Divide
