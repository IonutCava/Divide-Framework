#include "stdafx.h"

#include "Headers/Quad3D.h"

namespace Divide {

Quad3D::Quad3D(GFXDevice& context,
               ResourceCache* parentCache,
               const size_t descriptorHash,
               const Str256& name,
               const bool doubleSided,
               const vec3<F32>& sideLength)
    : Object3D(context,
                parentCache,
                descriptorHash,
                name,
                {},
                {},
                ObjectType::QUAD_3D,
                0u)
{
    const U16 indices[] = { 2, 0, 1, 1, 2, 3, 1, 0, 2, 2, 1, 3 };

    const F32 halfExtentX = sideLength.x * 0.5f;
    const F32 halfExtentY = sideLength.y * 0.5f;
    const F32 halfExtentZ = sideLength.z * 0.5f;
    
    getGeometryVB()->setVertexCount(4);
    getGeometryVB()->modifyPositionValue(0, -halfExtentX,  halfExtentY, -halfExtentZ); // TOP LEFT
    getGeometryVB()->modifyPositionValue(1,  halfExtentX,  halfExtentY, -halfExtentZ); // TOP RIGHT
    getGeometryVB()->modifyPositionValue(2, -halfExtentX, -halfExtentY,  halfExtentZ); // BOTTOM LEFT
    getGeometryVB()->modifyPositionValue(3,  halfExtentX, -halfExtentY,  halfExtentZ); // BOTTOM RIGHT

    getGeometryVB()->modifyNormalValue(0, 0, 0, -1);
    getGeometryVB()->modifyNormalValue(1, 0, 0, -1);
    getGeometryVB()->modifyNormalValue(2, 0, 0, -1);
    getGeometryVB()->modifyNormalValue(3, 0, 0, -1);

    getGeometryVB()->modifyTexCoordValue(0, 0, 1);
    getGeometryVB()->modifyTexCoordValue(1, 1, 1);
    getGeometryVB()->modifyTexCoordValue(2, 0, 0);
    getGeometryVB()->modifyTexCoordValue(3, 1, 0);

    const U8 indicesCount = doubleSided ? 12 : 6;
    for (U8 i = 0; i < indicesCount; i++) {
        // CCW draw order
        getGeometryVB()->addIndex(indices[i]);
        //  v0----v1
        //   |    |
        //   |    |
        //  v2----v3
    }

    getGeometryVB()->computeTangents();
    getGeometryVB()->create(true, true);

    recomputeBounds();
}

vec3<F32> Quad3D::getCorner(const CornerLocation corner) const
{
    switch (corner) {
        case CornerLocation::TOP_LEFT:
            return getGeometryVB()->getPosition(0);
        case CornerLocation::TOP_RIGHT:
            return getGeometryVB()->getPosition(1);
        case CornerLocation::BOTTOM_LEFT:
            return getGeometryVB()->getPosition(2);
        case CornerLocation::BOTTOM_RIGHT:
            return getGeometryVB()->getPosition(3);
        default:
            break;
    }
    // Default returns top left corner. Why? Don't care ... seems like a
    // good idea. - Ionut
    return getGeometryVB()->getPosition(0);
}

void Quad3D::setNormal(const CornerLocation corner, const vec3<F32>& normal) const {
    switch (corner) {
        case CornerLocation::TOP_LEFT:
            getGeometryVB()->modifyNormalValue(0, normal);
            break;
        case CornerLocation::TOP_RIGHT:
            getGeometryVB()->modifyNormalValue(1, normal);
            break;
        case CornerLocation::BOTTOM_LEFT:
            getGeometryVB()->modifyNormalValue(2, normal);
            break;
        case CornerLocation::BOTTOM_RIGHT:
            getGeometryVB()->modifyNormalValue(3, normal);
            break;
        case CornerLocation::CORNER_ALL: {
            getGeometryVB()->modifyNormalValue(0, normal);
            getGeometryVB()->modifyNormalValue(1, normal);
            getGeometryVB()->modifyNormalValue(2, normal);
            getGeometryVB()->modifyNormalValue(3, normal);
        } break;
    }
}

void Quad3D::setCorner(const CornerLocation corner, const vec3<F32>& value) {
    switch (corner) {
        case CornerLocation::TOP_LEFT:
            getGeometryVB()->modifyPositionValue(0, value);
            break;
        case CornerLocation::TOP_RIGHT:
            getGeometryVB()->modifyPositionValue(1, value);
            break;
        case CornerLocation::BOTTOM_LEFT:
            getGeometryVB()->modifyPositionValue(2, value);
            break;
        case CornerLocation::BOTTOM_RIGHT:
            getGeometryVB()->modifyPositionValue(3, value);
            break;
        default:
            break;
    }

    recomputeBounds();
}

// rect.xy = Top Left; rect.zw = Bottom right
// Remember to invert for 2D mode
void Quad3D::setDimensions(const vec4<F32>& rect) {
    getGeometryVB()->modifyPositionValue(0, rect.x, rect.w, 0);
    getGeometryVB()->modifyPositionValue(1, rect.z, rect.w, 0);
    getGeometryVB()->modifyPositionValue(2, rect.x, rect.y, 0);
    getGeometryVB()->modifyPositionValue(3, rect.z, rect.y, 0);

    recomputeBounds();
}

void Quad3D::recomputeBounds() {
    setBounds(
        BoundingBox
        {
        getGeometryVB()->getPosition(1),
        getGeometryVB()->getPosition(2) + vec3<F32>{ 0.0f, 0.0f, 0.0025f }
        }
    );
}

}; //namespace Divide