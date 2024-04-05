

#include "Headers/Box3D.h"

namespace Divide {

namespace {
    static const U16 indices[] = {
         0,  1,  3,  0,  3,  2,
         4,  5,  7,  4,  7,  6,
         8,  9, 11,  8, 11, 10,
        12, 13, 15, 12, 15, 14,
        16, 17, 19, 16, 19, 18,
        20, 21, 23, 20, 23, 22,
    };

    static const vec3<F32> vertices[4 * 6] = {
        {-1.0f, -1.0f,  1.0f},
        { 1.0f, -1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},

        { 1.0f, -1.0f,  1.0f},
        { 1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f, -1.0f},

        { 1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f},

        {-1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f},
        {-1.0f,  1.0f, -1.0f},
        {-1.0f,  1.0f,  1.0f},

        {-1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f},
        { 1.0f, -1.0f,  1.0f},

        {-1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},
        {-1.0f,  1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f}
    };
};
Box3D::Box3D(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const std::string_view name, const vec3<F32>& size)
    : Object3D(context, parentCache, descriptorHash, name, {}, {}, SceneNodeType::TYPE_BOX_3D, Object3D::ObjectFlag::OBJECT_FLAG_NONE)
{
    static const vec2<F32> texCoords[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f}
    };

    static const vec3<F32> normals[] = {
        { 0.f,  0.f,  1.f},
        { 1.f,  0.f,  0.f},
        { 0.f,  0.f, -1.f},
        {-1.f,  0.f,  0.f},
        { 0.f,  1.f,  0.f},
        { 0.f, -1.f,  0.f}
    };


    _halfExtent.set(size / 2);

    geometryBuffer()->setVertexCount(std::size(vertices));

    for (const U16 idx : indices) {
        geometryBuffer()->addIndex(idx);
    }

    for (U32 i = 0u; i < std::size(vertices); ++i) {
        geometryBuffer()->modifyPositionValue(i, vertices[i] * _halfExtent);
        geometryBuffer()->modifyTexCoordValue(i, texCoords[i % 4]);
        geometryBuffer()->modifyNormalValue(i, normals[i / 4]);
    }
    geometryBuffer()->create(false, true);
    setBounds(BoundingBox(-_halfExtent, _halfExtent));
}

void Box3D::setHalfExtent(const vec3<F32>& halfExtent) {
    _halfExtent = halfExtent;

    for (U32 i = 0u; i < std::size(vertices); ++i) {
        geometryBuffer()->modifyPositionValue(i, vertices[i] * _halfExtent);
    }
    setBounds(BoundingBox(-_halfExtent, _halfExtent));
}

void Box3D::fromPoints(const std::initializer_list<vec3<F32>>& points,
                        const vec3<F32>& halfExtent) {

    geometryBuffer()->modifyPositionValues(0, points);
    _halfExtent = halfExtent;
    setBounds(BoundingBox(-_halfExtent * 0.5f, _halfExtent * 0.5f));
}

const vec3<F32>& Box3D::getHalfExtent() const noexcept {
    return _halfExtent;
}

void Box3D::saveToXML(boost::property_tree::ptree& pt) const {
    pt.put("halfExtent.<xmlattr>.x", _halfExtent.x);
    pt.put("halfExtent.<xmlattr>.y", _halfExtent.y);
    pt.put("halfExtent.<xmlattr>.z", _halfExtent.z);

    Object3D::saveToXML(pt);
}

void Box3D::loadFromXML(const boost::property_tree::ptree& pt) {
    setHalfExtent(vec3<F32>(pt.get("halfExtent.<xmlattr>.x", 1.0f),
                            pt.get("halfExtent.<xmlattr>.y", 1.0f),
                            pt.get("halfExtent.<xmlattr>.z", 1.0f)));

    Object3D::loadFromXML(pt);
}

}; //namespace Divide