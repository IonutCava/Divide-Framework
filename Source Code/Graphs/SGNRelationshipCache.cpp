#include "stdafx.h"

#include "Headers/SGNRelationshipCache.h"
#include "Headers/SceneGraphNode.h"

namespace Divide {

SGNRelationshipCache::SGNRelationshipCache(SceneGraphNode* parent) noexcept
    : _parentNode(parent)
{
    std::atomic_init(&_isValid, false);
}

bool SGNRelationshipCache::isValid() const noexcept {
    return _isValid;
}

void SGNRelationshipCache::invalidate() noexcept {
    _isValid = false;
}

bool SGNRelationshipCache::rebuild() {
    OPTICK_EVENT();

    updateChildren(0u, _childrenRecursiveCache);
    updateParents(0u, _parentRecursiveCache);

    _isValid = true;
    return true;
}

SGNRelationshipCache::RelationshipType SGNRelationshipCache::classifyNode(const I64 GUID) const {
    assert(isValid());

    if (GUID != _parentNode->getGUID()) {
        SharedLock<SharedMutex> r_lock(_updateMutex);
        for (const CacheEntry& it : _childrenRecursiveCache) {
            if (it._guid == GUID) {
                return it._level > 0u
                                    ? RelationshipType::GRANDCHILD
                                    : RelationshipType::CHILD;
            }
        }

        for (const CacheEntry& it : _parentRecursiveCache) {
            if (it._guid == GUID) {
                return it._level > 0u
                                   ? RelationshipType::GRANDPARENT
                                   : RelationshipType::PARENT;
            }
        }

        const SceneGraphNode* parent = _parentNode->parent();
        if (parent != nullptr && parent->findChild(GUID) != nullptr) {
            return RelationshipType::SIBLING;
        }
    }

    return RelationshipType::COUNT;
}

void SGNRelationshipCache::updateChildren(U16 level, Cache& cache) const {
    ScopedLock<SharedMutex> w_lock(_updateMutex);
    updateChildrenLocked(level, cache);
}

void SGNRelationshipCache::updateParents(U16 level, Cache& cache) const {
    ScopedLock<SharedMutex> w_lock(_updateMutex);
    updateParentsLocked(level, cache);
}

void SGNRelationshipCache::updateChildrenLocked(const U16 level, Cache& cache) const {
    const SceneGraphNode::ChildContainer& children = _parentNode->getChildren();
    SharedLock<SharedMutex> w_lock(children._lock);
    const U32 childCount = children._count;
    for (U32 i = 0u; i < childCount; ++i) {
        SceneGraphNode* child = children._data[i];
        cache.emplace_back(CacheEntry{ child->getGUID(), level });
        Attorney::SceneGraphNodeRelationshipCache::relationshipCache(child).updateChildrenLocked(level + 1u, cache);
    }
}

void SGNRelationshipCache::updateParentsLocked(const U16 level, Cache& cache) const {
    const SceneGraphNode* parent = _parentNode->parent();
    // We ignore the root note when considering grandparent status
    if (parent && parent->parent()) {
        cache.emplace_back(CacheEntry{ parent->getGUID(), level });
        Attorney::SceneGraphNodeRelationshipCache::relationshipCache(parent).updateParentsLocked(level + 1u, cache);
    }
}

}; //namespace Divide