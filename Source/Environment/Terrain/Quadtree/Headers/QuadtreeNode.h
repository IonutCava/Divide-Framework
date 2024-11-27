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
#ifndef DVD_QUAD_TREE_NODE
#define DVD_QUAD_TREE_NODE

#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Environment/Terrain/Headers/TerrainChunk.h"

namespace Divide {

enum class ChildPosition :U32 { 
    CHILD_NW = 0, 
    CHILD_NE = 1, 
    CHILD_SW = 2, 
    CHILD_SE = 3
};

class Terrain;
class GFXDevice;
class SceneState;
class IMPrimitive;
class VertexBuffer;
class ShaderProgram;
class SceneGraphNode;
class SceneRenderState;

struct RenderPackage;

class Quadtree;
class QuadtreeChildren;

FWD_DECLARE_MANAGED_CLASS(TerrainChunk);
FWD_DECLARE_MANAGED_CLASS(QuadtreeNode);

class QuadtreeNode
{
   public:
     QuadtreeNode(Quadtree* parent) noexcept;

    /// recursive node building function
    void build(U8 depth,
               vec2<U16> pos,
               vec2<U16> HMsize,
               U32 targetChunkDimension,
               Terrain* terrain,
               U32& chunkCount);

    [[nodiscard]] bool computeBoundingBox(BoundingBox& parentBB);
    
    void drawBBox(GFXDevice& context);
    void toggleBoundingBoxes();

    [[nodiscard]] bool isALeaf() const noexcept { return _children[0] == nullptr; }
    [[nodiscard]] U8 LoD() const noexcept { return _LoD; }

    [[nodiscard]] const BoundingBox& getBoundingBox() const noexcept { return _boundingBox; }
    void setBoundingBox(const BoundingBox& bbox) noexcept { _boundingBox = bbox; }

    [[nodiscard]] TerrainChunk* getChunk() const noexcept;

    [[nodiscard]] QuadtreeNode& getChild(const ChildPosition pos) const noexcept { return *_children[to_base(pos)]; }
    [[nodiscard]] QuadtreeNode& getChild(const U32 index) const noexcept { return *_children[index]; }

    PROPERTY_R_IW(U32, targetChunkDimension, 0u);

   private:
    BoundingBox _boundingBox;                   ///< Node BoundingBox
    BoundingSphere _boundingSphere;             ///< Node BoundingSphere
    Quadtree* _parent = nullptr;
    std::array<QuadtreeNode_uptr, 4> _children; ///< Node children
    TerrainChunk_uptr _terrainChunk;            ///< Terrain Chunk contained in node
    U8 _LoD = 0u;
    bool _drawBBoxes = false;
};

}  // namespace Divide

#endif //DVD_QUAD_TREE_NODE
