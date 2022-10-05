#include "stdafx.h"

#include "Headers/Quadtree.h"
#include "Headers/QuadtreeNode.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

namespace Divide {

Quadtree::Quadtree()
    : _root(eastl::make_unique<QuadtreeNode>(this))
{
}

Quadtree::~Quadtree()
{
}

void Quadtree::toggleBoundingBoxes() {
    _drawBBoxes = !_drawBBoxes;
    _root->toggleBoundingBoxes();
}

void Quadtree::drawBBox(GFXDevice& context) const {
    if (!_drawBBoxes) {
        return;
    }

    _root->drawBBox(context);
 
    IM::BoxDescriptor descriptor;
    descriptor.min = _root->getBoundingBox().getMin();
    descriptor.max = _root->getBoundingBox().getMax();
    descriptor.colour = UColour4(0, 64, 255, 255);
    context.debugDrawBox(-1, descriptor);
}

QuadtreeNode* Quadtree::findLeaf(const vec2<F32> pos) const noexcept
{
    assert(_root);

    QuadtreeNode* node = _root.get();
    while (!node->isALeaf()) {
        U32 i;
        for (i = 0; i < 4; i++) {
            QuadtreeNode* child = &node->getChild(i);
            const BoundingBox& bb = child->getBoundingBox();
            if (bb.containsPoint(vec3<F32>(pos.x, bb.getCenter().y, pos.y))) {
                node = child;
                break;
            }
        }

        if (i >= 4) {
            return nullptr;
        }
    }

    return node;
}

void Quadtree::build(const BoundingBox& terrainBBox,
                     const vec2<U16> HMSize,
                     Terrain* const terrain) {

    _targetChunkDimension = std::max(HMSize.maxComponent() / 8u, 8u);

    _root->setBoundingBox(terrainBBox);
    _root->build(0, vec2<U16>(0u), HMSize, _targetChunkDimension, terrain, _chunkCount);
}

const BoundingBox& Quadtree::computeBoundingBox() const {
    assert(_root);
    BoundingBox rootBB = _root->getBoundingBox();
    if (!_root->computeBoundingBox(rootBB)) {
        DIVIDE_UNEXPECTED_CALL();
    }

    return _root->getBoundingBox();
}
}