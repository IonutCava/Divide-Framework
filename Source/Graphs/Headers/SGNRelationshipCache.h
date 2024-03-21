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
#ifndef DVD_SCENE_GRAPH_NODE_RELATIONSHIP_CACHE_H_
#define DVD_SCENE_GRAPH_NODE_RELATIONSHIP_CACHE_H_

namespace Divide {

class SceneGraphNode;
class SGNRelationshipCache {
public:
    enum class RelationshipType : U8 {
        GRANDPARENT = 0, ///<applies for all levels above 0
        PARENT,
        CHILD,
        GRANDCHILD,
        SIBLING,
        COUNT
    };

    struct CacheEntry {
        I64 _guid = -1;
        U16 _level = 0u; // 0 - child, 1 - grandchild etc or 0 - parent, 1 - grandparent, etc
    };

    using Cache = vector_fast<CacheEntry>;
public:
    SGNRelationshipCache(SceneGraphNode* parent) noexcept;

    [[nodiscard]] bool isValid() const noexcept;
    void invalidate() noexcept;
    bool rebuild();

    // this will issue a rebuild if the cache is invalid
    [[nodiscard]] RelationshipType classifyNode(I64 GUID) const noexcept;
    [[nodiscard]] bool validateRelationship(I64 guid, RelationshipType type) const noexcept;

protected:
    void updateChildren(U16 level, Cache& cache) const;
    void updateParents(U16 level, Cache& cache) const;
    void updateSiblings(U16 level, Cache& cache) const;

protected:
    /// We need a way to accelerate relationship testing
    /// We can cache a full recursive list of children
    /// pair: GUID ... child level (0 = child, 1 = grandchild, ...)
    Cache _childrenRecursiveCache;
    /// pair: GUID ... parent level (0 = parent, 1 = grandparent, ...)
    Cache _parentRecursiveCache;
    /// pair: GUID ... level unused
    Cache _siblingCache;

    std::atomic_bool _isValid;
    SceneGraphNode* _parentNode = nullptr;

    mutable SharedMutex _updateMutex;
};

}; //namespace Divide

#endif //DVD_SCENE_GRAPH_NODE_RELATIONSHIP_CACHE_H_
