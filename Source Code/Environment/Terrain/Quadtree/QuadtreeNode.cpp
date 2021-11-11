#include "stdafx.h"

#include "Headers/QuadtreeNode.h"

#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Terrain/Headers/TerrainChunk.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"

namespace Divide {

QuadtreeNode::QuadtreeNode(GFXDevice& context, Quadtree* parent) noexcept
    : _parent(parent),
      _context(context)
{
}

QuadtreeNode::~QuadtreeNode()
{
    for (U8 i = 0; i < 4; ++i) {
        MemoryManager::SAFE_DELETE(_children[i]);
    }
    if (_bbPrimitive) {
        _context.destroyIMP(_bbPrimitive);
    }
}

void QuadtreeNode::build(const U8 depth,
                         const vec2<U16>& pos,
                         const vec2<U16>& HMsize,
                         const U32 targetChunkDimension,
                         Terrain* const terrain,
                         U32& chunkCount)
{
    _targetChunkDimension = std::min(targetChunkDimension, to_U32(HMsize.width));

    const U32 div = to_U32(std::pow(2.0f, to_F32(depth)));
    vec2<U16> nodesize = HMsize / div;
    if (nodesize.x % 2 == 0) {
        nodesize.x++;
    }
    if (nodesize.y % 2 == 0) {
        nodesize.y++;
    }
    const vec2<U16> newsize = nodesize / 2;

    if (std::max(newsize.x, newsize.y) < _targetChunkDimension) {
        _terrainChunk = eastl::make_unique<TerrainChunk>(_context, terrain, *this);
        _terrainChunk->load(depth, pos, _targetChunkDimension, HMsize, _boundingBox);
        chunkCount++;
    } else {
        // Create 4 children

        // Compute children bounding boxes
        const vec3<F32>& center = _boundingBox.getCenter();
        _children[to_base(ChildPosition::CHILD_NW)] = MemoryManager_NEW QuadtreeNode(_context, _parent);
        _children[to_base(ChildPosition::CHILD_NW)]->setBoundingBox(BoundingBox(_boundingBox.getMin(), center));

        _children[to_base(ChildPosition::CHILD_NE)] = MemoryManager_NEW QuadtreeNode(_context, _parent);
        _children[to_base(ChildPosition::CHILD_NE)]->setBoundingBox(BoundingBox(vec3<F32>(center.x, 0.0f, _boundingBox.getMin().z), vec3<F32>(_boundingBox.getMax().x, 0.0f, center.z)));

        _children[to_base(ChildPosition::CHILD_SW)] = MemoryManager_NEW QuadtreeNode(_context, _parent);
        _children[to_base(ChildPosition::CHILD_SW)]->setBoundingBox(BoundingBox(vec3<F32>(_boundingBox.getMin().x, 0.0f, center.z), vec3<F32>(center.x, 0.0f, _boundingBox.getMax().z)));

        _children[to_base(ChildPosition::CHILD_SE)] = MemoryManager_NEW QuadtreeNode(_context, _parent);
        _children[to_base(ChildPosition::CHILD_SE)]->setBoundingBox(BoundingBox(center, _boundingBox.getMax()));

        // Compute children positions
        vec2<U16> tNewHMpos[4];
        tNewHMpos[to_base(ChildPosition::CHILD_NW)] = pos + vec2<U16>(0, 0);
        tNewHMpos[to_base(ChildPosition::CHILD_NE)] = pos + vec2<U16>(newsize.x, 0);
        tNewHMpos[to_base(ChildPosition::CHILD_SW)] = pos + vec2<U16>(0, newsize.y);
        tNewHMpos[to_base(ChildPosition::CHILD_SE)] = pos + vec2<U16>(newsize.x, newsize.y);

        _children[to_base(ChildPosition::CHILD_NW)]->build(depth + 1, tNewHMpos[to_base(ChildPosition::CHILD_NW)], HMsize, _targetChunkDimension, terrain, chunkCount);
        _children[to_base(ChildPosition::CHILD_NE)]->build(depth + 1, tNewHMpos[to_base(ChildPosition::CHILD_NE)], HMsize, _targetChunkDimension, terrain, chunkCount);
        _children[to_base(ChildPosition::CHILD_SW)]->build(depth + 1, tNewHMpos[to_base(ChildPosition::CHILD_SW)], HMsize, _targetChunkDimension, terrain, chunkCount);
        _children[to_base(ChildPosition::CHILD_SE)]->build(depth + 1, tNewHMpos[to_base(ChildPosition::CHILD_SE)], HMsize, _targetChunkDimension, terrain, chunkCount);
    }
}

bool QuadtreeNode::computeBoundingBox(BoundingBox& parentBB) {
    if (!isALeaf()) {
        for (I8 i = 0; i < 4; ++i) {
            if (!_children[i]->computeBoundingBox(_boundingBox)) {
                return false;
            }
        }
    }

    parentBB.add(_boundingBox);
    _boundingSphere.fromBoundingBox(_boundingBox);

    return true;
}

void QuadtreeNode::toggleBoundingBoxes() {
    _drawBBoxes = !_drawBBoxes;
    if (!_drawBBoxes) {
        ScopedLock<Mutex> w_lock(_bbPrimitiveLock);
        _context.destroyIMP(_bbPrimitive);
        _bbPrimitive = nullptr;
    }
}

void QuadtreeNode::drawBBox(RenderPackage& packageOut) {
    {
        ScopedLock<Mutex> w_lock(_bbPrimitiveLock);
        if (_bbPrimitive == nullptr) {
            _bbPrimitive = _context.newIMP();
            _bbPrimitive->name("QuadtreeBoundingBox");
            _bbPrimitive->pipeline(*_parent->bbPipeline());
        }

        _bbPrimitive->fromBox(_boundingBox.getMin(),
                              _boundingBox.getMax(),
                              UColour4(0, 128, 255, 255));

        packageOut.appendCommandBuffer(_bbPrimitive->toCommandBuffer());
    }

    if (!isALeaf()) {
        getChild(ChildPosition::CHILD_NW).drawBBox(packageOut);
        getChild(ChildPosition::CHILD_NE).drawBBox(packageOut);
        getChild(ChildPosition::CHILD_SW).drawBBox(packageOut);
        getChild(ChildPosition::CHILD_SE).drawBBox(packageOut);
    }
}
}