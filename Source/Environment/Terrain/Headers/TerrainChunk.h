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
#ifndef DVD_TERRAIN_CHUNK_H
#define DVD_TERRAIN_CHUNK_H

#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Environment/Vegetation/Headers/Vegetation.h"

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

namespace Attorney
{
    class TerrainChunkVegetation;
}

class TerrainChunk {
    static U32 _chunkID;
    friend class Attorney::TerrainChunkVegetation;

   public:
    TerrainChunk(Terrain* parentTerrain, QuadtreeNode& parentNode) noexcept;

    void load(U8 depth, const uint2 pos, U32 targetChunkDimension, uint2 HMSize, BoundingBox& bbInOut);
    

    [[nodiscard]] float4 getOffsetAndSize() const noexcept {
        return float4(_xOffset, _yOffset, _sizeX, _sizeY);
    }

    [[nodiscard]] const Terrain& parent() const noexcept { return *_parentTerrain; }
    [[nodiscard]] const QuadtreeNode& quadtreeNode() const noexcept { return _quadtreeNode; }

    [[nodiscard]] const BoundingBox& bounds() const noexcept;
    void drawBBox(GFXDevice& context) const;

    [[nodiscard]] U8 LoD() const noexcept;

    PROPERTY_R(U32, id, 0u);


   private:
    void initVegetation( PlatformContext& context, Handle<Vegetation> handle );

   private:
    QuadtreeNode& _quadtreeNode;

    F32 _xOffset{0.f};
    F32 _yOffset{0.f};
    F32 _sizeX{0.f};
    F32 _sizeY{0.f};
    Terrain* _parentTerrain{nullptr};
    VegetationInstance_uptr _vegetation;
};

FWD_DECLARE_MANAGED_CLASS(TerrainChunk);

namespace Attorney
{

class TerrainChunkVegetation
{
    static void initVegetation(Divide::TerrainChunk& chunk, PlatformContext& context, const Handle<Vegetation> handle) noexcept
    {
        chunk.initVegetation(context, handle);
    }

    static VegetationInstance* getVegetation(const Divide::TerrainChunk& chunk) noexcept
    {
        return chunk._vegetation.get();
    }

    friend class Divide::Terrain;
    friend class Divide::Vegetation;
};
}  // namespace Attorney

}  // namespace Divide

#endif //DVD_TERRAIN_CHUNK_H
