#include "stdafx.h"

#include "Headers/SGNRelationshipCache.h"
#include "Headers/SceneGraphNode.h"

namespace Divide {

SGNRelationshipCache::SGNRelationshipCache(SceneGraphNode* parent)
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
    OPTICK_EVENT();

    ScopedLock<SharedMutex> w_lock(_updateMutex);
    updateChildren(0, _childrenRecursiveCache);
    updateParents(0, _parentRecursiveCache);
    _isValid = true;
    return true;
}

SGNRelationshipCache::RelationshipType SGNRelationshipCache::classifyNode(const I64 GUID) const {
    assert(isValid());

    if (GUID != _parentNode->getGUID()) {
        SharedLock<SharedMutex> r_lock(_updateMutex);
        for (const auto& [childGUID, childDepth] : _childrenRecursiveCache) {
            if (childGUID == GUID) {
                return childDepth > 0 
                                    ? RelationshipType::GRANDCHILD
                                    : RelationshipType::CHILD;
            }
        }
        for (const auto& [parentGUID, parentDepth] : _parentRecursiveCache) {
            if (parentGUID == GUID) {
                return parentDepth > 0
                                   ? RelationshipType::GRANDPARENT
                                   : RelationshipType::PARENT;
            }
        }
        SceneGraphNode* parent = _parentNode->parent();
        if (parent != nullptr && parent->findChild(GUID) != nullptr) {
            return RelationshipType::SIBLING;
        }
    }

    return RelationshipType::COUNT;
}


void SGNRelationshipCache::updateChildren(U8 level, vectorEASTL<std::pair<I64, U8>>& cache) const {
    _parentNode->forEachChild([level, &cache](const SceneGraphNode* child, I32 /*childIdx*/) {
        cache.emplace_back(child->getGUID(), level);
        Attorney::SceneGraphNodeRelationshipCache::relationshipCache(child).updateChildren(level + 1, cache);
        return true;
    });
}

void SGNRelationshipCache::updateParents(U8 level, vectorEASTL<std::pair<I64, U8>>& cache) const {
    SceneGraphNode* parent = _parentNode->parent();
    // We ignore the root note when considering grandparent status
    if (parent && parent->parent()) {
        cache.emplace_back(parent->getGUID(), level);
        Attorney::SceneGraphNodeRelationshipCache::relationshipCache(parent).updateParents(level + 1, cache);
    }
}

}; //namespace Divide