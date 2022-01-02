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
#ifndef _TERRAIN_CHUNK_H
#define _TERRAIN_CHUNK_H

#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"

namespace Divide {

class Mesh;
class Terrain;
class BoundingBox;
class QuadtreeNode;
class ShaderProgram;
class SceneGraphNode;
class SceneRenderState;
struct FileData;
struct RenderPackage;
struct VegetationDetails;

FWD_DECLARE_MANAGED_CLASS(Vegetation);

namespace Attorney {
    class TerrainChunkTerrain;
}

class TerrainChunk {
    static U32 _chunkID;
    friend class Attorney::TerrainChunkTerrain;

   public:
    TerrainChunk(Terrain* parentTerrain, QuadtreeNode& parentNode) noexcept;

    void load(U8 depth, const vec2<U32>& pos, U32 targetChunkDimension, const vec2<U32>& HMSize, BoundingBox& bbInOut);

    [[nodiscard]] U32 ID() const noexcept { return _ID; }

    [[nodiscard]] vec4<F32> getOffsetAndSize() const noexcept {
        return vec4<F32>(_xOffset, _yOffset, _sizeX, _sizeY);
    }

    [[nodiscard]] const Terrain& parent() const noexcept { return *_parentTerrain; }
    [[nodiscard]] const QuadtreeNode& quadtreeNode() const noexcept { return _quadtreeNode; }

    [[nodiscard]] const BoundingBox& bounds() const noexcept;
    void drawBBox(GFXDevice& context) const;

    [[nodiscard]] U8 LoD() const noexcept;

   protected:
    [[nodiscard]] const Vegetation_ptr& getVegetation() const noexcept { return _vegetation; }

    friend class Vegetation;
    void initializeVegetation(GFXDevice& context, const VegetationDetails& vegDetails);

   private:
    QuadtreeNode& _quadtreeNode;

    U32 _ID;
    F32 _xOffset;
    F32 _yOffset;
    F32 _sizeX;
    F32 _sizeY;
    Terrain* _parentTerrain;
    Vegetation_ptr _vegetation;
};

namespace Attorney {
class TerrainChunkTerrain {
    static const Vegetation_ptr& getVegetation(const Divide::TerrainChunk& chunk) noexcept {
        return chunk.getVegetation();
    }
    friend class Divide::Terrain;
};
}  // namespace Attorney

}  // namespace Divide

#endif
