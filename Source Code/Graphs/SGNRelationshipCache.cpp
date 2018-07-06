#include "Headers/SGNRelationshipCache.h"
#include "Headers/SceneGraphNode.h"

namespace Divide {

SGNRelationshipCache::SGNRelationshipCache(SceneGraphNode& parent)
    : _parentNode(parent)
{
    _isValid = false;
}

bool SGNRelationshipCache::isValid() const {
    return _isValid;
}

void SGNRelationshipCache::invalidate() {
    _isValid = false;
}

bool SGNRelationshipCache::rebuild() {
    WriteLock w_lock(_updateMutex);
    updateChildren(0, _childrenRecursiveCache);
    updateParents(0, _parentRecursiveCache);
    _isValid = true;
    return true;
}

SGNRelationshipCache::RelationShipType
SGNRelationshipCache::clasifyNode(I64 GUID) const {
    assert(isValid());

    if (GUID != _parentNode.getGUID()) {
        ReadLock r_lock(_updateMutex);
        for (const std::pair<I64, U8>& entry : _childrenRecursiveCache) {
            if (entry.first == GUID) {
                return entry.second > 0 
                                    ? RelationShipType::GRANDCHILD
                                    : RelationShipType::CHILD;
            }
        }
        for (const std::pair<I64, U8>& entry : _parentRecursiveCache) {
            if (entry.first == GUID) {
                return entry.second > 0
                    ? RelationShipType::GRANDPARENT
                    : RelationShipType::PARENT;
            }
        }
    }

    return RelationShipType::COUNT;
}


void SGNRelationshipCache::updateChildren(U8 level, vectorImpl<std::pair<I64, U8>>& cache) const {
    U32 childCount = _parentNode.getChildCount();
    for (U32 i = 0; i < childCount; ++i) {
        const SceneGraphNode& child = _parentNode.getChild(i, childCount);
        cache.push_back(std::make_pair(child.getGUID(), level));
        child.relationshipCache().updateChildren(level + 1, cache);
    }
}

void SGNRelationshipCache::updateParents(U8 level, vectorImpl<std::pair<I64, U8>>& cache) const {
    SceneGraphNode_ptr parent = _parentNode.getParent().lock();
    // We ignore the root note when considering grandparent status
    if (parent && parent->getParent().lock()) {
        cache.push_back(std::make_pair(parent->getGUID(), level));
        parent->relationshipCache().updateParents(level + 1, cache);
    }
}

}; //namespace Divide