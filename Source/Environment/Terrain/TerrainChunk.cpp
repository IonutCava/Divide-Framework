

#include "Headers/TerrainChunk.h"
#include "Headers/Terrain.h"
#include "Quadtree/Headers/QuadtreeNode.h"

#include "Managers/Headers/ProjectManager.h"
#include "Geometry/Material/Headers/Material.h"
#include "Environment/Vegetation/Headers/Vegetation.h"

namespace Divide {

U32 TerrainChunk::_chunkID = 0;

TerrainChunk::TerrainChunk(Terrain* const parentTerrain,
                           QuadtreeNode& parentNode) noexcept
    : _id(_chunkID++)
    , _quadtreeNode(parentNode)
    , _parentTerrain(parentTerrain)
{
}

void TerrainChunk::load(const U8 depth, const vec2<U32> pos, const U32 targetChunkDimension, const vec2<U32> HMSize, BoundingBox& bbInOut)
{
    _xOffset = to_F32(pos.x) - HMSize.x * 0.5f;
    _yOffset = to_F32(pos.y) - HMSize.y * 0.5f;
    _sizeX = _sizeY = to_F32(targetChunkDimension);

    F32 tempMin = F32_MAX;
    F32 tempMax = F32_LOWEST;

    const U32 offset = to_U32(std::pow(2.0f, to_F32(0.0f)));
    const U32 div = to_U32(std::pow(2.0f, to_F32(depth)));

    const vec2<U32> heightmapDataSize = HMSize / div;
    const U32 nHMWidth = heightmapDataSize.x;
    const U32 nHMHeight = heightmapDataSize.y;

    const vector<VertexBuffer::Vertex>& verts = _parentTerrain->_physicsVerts;

    for (U16 j = 0; j < nHMHeight - 1; ++j) {
        const U32 jOffset = j * offset+pos.y;
        for (U16 i = 0; i < nHMWidth; ++i) {
            const U32 iOffset = i * offset+pos.x;
            U32 idx = iOffset + jOffset * HMSize.x;
            F32 height = verts[idx]._position.y;

            if (height > tempMax) {
                tempMax = height;
            }
            if (height < tempMin) {
                tempMin = height;
            }


            idx = iOffset + (jOffset + offset) * HMSize.x;
            height = verts[idx]._position.y;

            if (height > tempMax) {
                tempMax = height;
            }
            if (height < tempMin) {
                tempMin = height;
            }
        }
    }

    bbInOut.setMin(bbInOut.getMin().x, tempMin, bbInOut.getMin().z);
    bbInOut.setMax(bbInOut.getMax().x, tempMax, bbInOut.getMax().z);

    Attorney::TerrainChunk::registerTerrainChunk(*_parentTerrain, this);
}

const BoundingBox& TerrainChunk::bounds() const noexcept
{
    return _quadtreeNode.getBoundingBox();
}

U8 TerrainChunk::LoD() const noexcept
{
    return _quadtreeNode.LoD();
}

void TerrainChunk::drawBBox(GFXDevice& context) const
{
    _quadtreeNode.drawBBox(context);
}

void TerrainChunk::initVegetation( PlatformContext& context, const Handle<Vegetation> handle )
{
    _vegetation = std::make_unique<VegetationInstance>( context, handle, this );
    _vegetation->computeTransforms();
}
} //namespace Divide
