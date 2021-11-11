#include "stdafx.h"

#include "Headers/Box3D.h"

namespace Divide {

Box3D::Box3D(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, const vec3<F32>& size)
    : Object3D(context, parentCache, descriptorHash, name, {}, {}, ObjectType::BOX_3D, 0u)
{
    _halfExtent.set(size / 2);

    static const vec3<F32> vertices[] = {
        vec3<F32>(-1.0f, -1.0f,  1.0f),
        vec3<F32>(1.0f, -1.0f,  1.0f),
        vec3<F32>(-1.0f,  1.0f,  1.0f),
        vec3<F32>(1.0f,  1.0f,  1.0f),
        vec3<F32>(-1.0f, -1.0f, -1.0f),
        vec3<F32>(1.0f, -1.0f, -1.0f),
        vec3<F32>(-1.0f,  1.0f, -1.0f),
        vec3<F32>(1.0f,  1.0f, -1.0f)
    };

    constexpr U16 indices[] = {
        0, 1, 2,
        3, 7, 1,
        5, 4, 7,
        6, 2, 4,
        0, 1
    };

    VertexBuffer* vb = getGeometryVB();

    vb->setVertexCount(8);

    for (U8 i = 0; i < 8; i++) {
        vb->modifyPositionValue(i, vertices[i] * _halfExtent);
        vb->modifyNormalValue(i, vertices[i]);
    }

    for (const U16 idx : indices) {
        vb->addIndex(idx);
    }

    vb->create(false);
    setBounds(BoundingBox(-_halfExtent * 0.5f, _halfExtent * 0.5f));
}

void Box3D::setHalfExtent(const vec3<F32>& halfExtent) {
    static const vec3<F32> vertices[] = {
        vec3<F32>(-1.0f, -1.0f,  1.0f),
        vec3<F32>(1.0f, -1.0f,  1.0f),
        vec3<F32>(-1.0f,  1.0f,  1.0f),
        vec3<F32>(1.0f,  1.0f,  1.0f),
        vec3<F32>(-1.0f, -1.0f, -1.0f),
        vec3<F32>(1.0f, -1.0f, -1.0f),
        vec3<F32>(-1.0f,  1.0f, -1.0f),
        vec3<F32>(1.0f,  1.0f, -1.0f)
    };

    _halfExtent = halfExtent;

    VertexBuffer* vb = getGeometryVB();
    for (U8 i = 0; i < 8; ++i) {
        vb->modifyPositionValue(i, vertices[i] * _halfExtent);
    }

    vb->queueRefresh();
    setBounds(BoundingBox(-_halfExtent * 0.5f, _halfExtent * 0.5f));
}

void Box3D::fromPoints(const std::initializer_list<vec3<F32>>& points,
                        const vec3<F32>& halfExtent) {

    VertexBuffer* vb = getGeometryVB();
    vb->modifyPositionValues(0, points);
    vb->queueRefresh();
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