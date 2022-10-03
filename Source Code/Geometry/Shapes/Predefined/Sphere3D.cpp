#include "stdafx.h"

#include "Headers/Sphere3D.h"

namespace Divide {

Sphere3D::Sphere3D(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, const F32 radius, const U32 resolution)
    : Object3D(context, parentCache, descriptorHash, name, {}, {}, ObjectType::SPHERE_3D, Object3D::ObjectFlag::OBJECT_FLAG_NONE),
    _radius(radius),
    _resolution(resolution)
{
    const U32 vertexCount = SQUARED(resolution);
    geometryBuffer()->setVertexCount(vertexCount);
    geometryBuffer()->reserveIndexCount(vertexCount);
    geometryBuffer()->useLargeIndices(vertexCount + 1 > U16_MAX);
    geometryDirty(true);
}

void Sphere3D::setRadius(const F32 radius) noexcept {
    _radius = radius;
    geometryDirty(true);
}

void Sphere3D::setResolution(const U32 resolution) noexcept {
    _resolution = resolution;
    geometryDirty(true);
}

// SuperBible stuff
void Sphere3D::rebuildInternal() {
    const U32 slices = _resolution;
    const U32 stacks = _resolution;

    geometryBuffer()->reset();
    const F32 drho = M_PI_f / stacks;
    const F32 dtheta = 2.0f * M_PI_f / slices;
    const F32 ds = 1.0f / slices;
    const F32 dt = 1.0f / stacks;
    F32 t = 1.0f;
    U32 index = 0;  /// for the index buffer

    geometryBuffer()->setVertexCount(stacks * ((slices + 1) * 2));

    for (U32 i = 0u; i < stacks; i++) {
        const F32 rho = i * drho;
        const F32 srho = std::sin(rho);
        const F32 crho = std::cos(rho);
        const F32 srhodrho = std::sin(rho + drho);
        const F32 crhodrho = std::cos(rho + drho);

        // Many sources of OpenGL sphere drawing code uses a triangle fan
        // for the caps of the sphere. This however introduces texturing
        // artifacts at the poles on some OpenGL implementations
        F32 s = 0.0f;
        for (U32 j = 0; j <= slices; j++) {
            const F32 theta = j == slices ? 0.0f : j * dtheta;
            const F32 stheta = -std::sin(theta);
            const F32 ctheta =  std::cos(theta);

            F32 x = stheta * srho;
            F32 y = ctheta * srho;
            F32 z = crho;

            geometryBuffer()->modifyPositionValue(index, x * _radius, y * _radius, z * _radius);
            geometryBuffer()->modifyTexCoordValue(index, s, t);
            geometryBuffer()->modifyNormalValue(index, x, y, z);
            geometryBuffer()->addIndex(index++);

            x = stheta * srhodrho;
            y = ctheta * srhodrho;
            z = crhodrho;
            s += ds;

            geometryBuffer()->modifyPositionValue(index, x * _radius, y * _radius, z * _radius);
            geometryBuffer()->modifyTexCoordValue(index, s, t - dt);
            geometryBuffer()->modifyNormalValue(index, x, y, z);
            geometryBuffer()->addIndex(index++);
        }
        t -= dt;
    }

    geometryBuffer()->create(true, true);
    efficient_clear( _geometryTriangles );

    // ToDo: add some depth padding for collision and nav meshes
    setBounds(BoundingBox(vec3<F32>(-_radius), vec3<F32>(_radius)));

    Object3D::rebuildInternal();
}

void Sphere3D::saveToXML(boost::property_tree::ptree& pt) const {
    pt.put("radius", _radius);
    pt.put("resolution", _resolution);

    Object3D::saveToXML(pt);
}

void Sphere3D::loadFromXML(const boost::property_tree::ptree& pt) {
    setRadius(pt.get("radius", 1.0f));
    setResolution(pt.get("resolution", 16u));

    Object3D::loadFromXML(pt);
}

}; //namespace Divide