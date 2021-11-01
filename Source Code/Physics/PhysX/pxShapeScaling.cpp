#include "stdafx.h"

#include "Headers/pxShapeScaling.h"

using namespace physx;

namespace Divide {

// Scales the given shape as precisely as possible.
void Scale(PxShape&shape, const PxVec3&scaling) {
    PxTransform pose = shape.getLocalPose();

    switch (shape.getGeometryType()) {
        case PxGeometryType::eBOX: {
            PxBoxGeometry geom;
            shape.getBoxGeometry(geom);
            Scale(geom, pose, scaling);
            shape.setGeometry(geom);
        } break;
        case PxGeometryType::eSPHERE: {
            PxSphereGeometry geom;
            shape.getSphereGeometry(geom);
            Scale(geom, pose, scaling);
            shape.setGeometry(geom);
        } break;
        case PxGeometryType::ePLANE: {
            PxPlaneGeometry geom;
            shape.getPlaneGeometry(geom);
            Scale(geom, pose, scaling);
            shape.setGeometry(geom);
        } break;
        case PxGeometryType::eCAPSULE: {
            PxCapsuleGeometry geom;
            shape.getCapsuleGeometry(geom);
            Scale(geom, pose, scaling);
            shape.setGeometry(geom);
        } break;
        case PxGeometryType::eCONVEXMESH: {
            PxConvexMeshGeometry geom;
            shape.getConvexMeshGeometry(geom);
            Scale(geom, pose, scaling);
            shape.setGeometry(geom);
        } break;
        case PxGeometryType::eTRIANGLEMESH: {
            PxTriangleMeshGeometry geom;
            shape.getTriangleMeshGeometry(geom);
            Scale(geom, pose, scaling);
            shape.setGeometry(geom);
        } break;
        default:
        case PxGeometryType::eHEIGHTFIELD:
        case PxGeometryType::eGEOMETRY_COUNT: break;
    }

    shape.setLocalPose(pose);
}

// Scales the given shape as precisely as possible.
void Scale(PxGeometry& geometry, PxTransform& pose, const PxVec3& scaling) {
    switch (geometry.getType()) {
        case PxGeometryType::eBOX:
            Scale(static_cast<PxBoxGeometry&>(geometry), pose, scaling);
            break;
        case PxGeometryType::eSPHERE:
            Scale(static_cast<PxSphereGeometry&>(geometry), pose,
                  scaling);
            break;
        case PxGeometryType::ePLANE:
            Scale(static_cast<PxPlaneGeometry&>(geometry), pose,
                  scaling);
            break;
        case PxGeometryType::eCAPSULE:
            Scale(static_cast<PxCapsuleGeometry&>(geometry), pose,
                  scaling);
            break;
        case PxGeometryType::eCONVEXMESH:
            Scale(static_cast<PxConvexMeshGeometry&>(geometry), pose,
                  scaling);
            break;
        case PxGeometryType::eTRIANGLEMESH:
            Scale(static_cast<PxTriangleMeshGeometry&>(geometry), pose,
                  scaling);
            break;
        default: break;
    }
}

// Scales the given shape as precisely as possible.
void Scale(PxBoxGeometry& geometry, PxTransform& pose, const PxVec3& scaling) {
    // Shape-space approximation
    const PxMat33 scaleMat =
        PxMat33::createDiagonal(scaling) *
        PxMat33(pose.q);  // == (pose^-1 * scaling)^T
    geometry.halfExtents.x *= scaleMat.column0.magnitude();
    geometry.halfExtents.y *= scaleMat.column1.magnitude();
    geometry.halfExtents.z *= scaleMat.column2.magnitude();

    pose.p = pose.p.multiply(scaling);
}

// Scales the given shape as precisely as possible.
void Scale(PxSphereGeometry& geometry, PxTransform& pose, const PxVec3& scaling) {
    // Omnidirectional approximation
    geometry.radius *=
        std::pow(std::abs(scaling.x * scaling.y * scaling.z), 1.0f / 3.0f);

    pose.p = pose.p.multiply(scaling);
}

// Scales the given shape as precisely as possible.
void Scale([[maybe_unused]] PxPlaneGeometry& geometry, PxTransform& pose, const PxVec3& scaling) {
    pose.p = pose.p.multiply(scaling);
}

// Scales the given shape as precisely as possible.
void Scale(PxCapsuleGeometry& geometry, PxTransform& pose, const PxVec3& scaling) {
    // Shape-space approximation
    const PxMat33 scaleMat =
        PxMat33::createDiagonal(scaling) *
        PxMat33(pose.q);  // == (pose^-1 * scaling)^T
    geometry.halfHeight *= scaleMat.column0.magnitude();
    // Bi-directional shape-space approximation
    geometry.radius *=
        Sqrt(scaleMat.column1.magnitude() * scaleMat.column2.magnitude());

    pose.p = pose.p.multiply(scaling);
}

// Scales the given shape as precisely as possible.
void Scale(PxConvexMeshGeometry& geometry, PxTransform& pose, const PxVec3& scaling) {
    // TODO: Remove WORKAROUND as soon as possible

    // Shape-space approximation
    const PxMat33 scaleMat =
        PxMat33::createDiagonal(scaling) *
        PxMat33(pose.q);  // == (pose^-1 * scaling)^T
    geometry.scale.scale.x *= scaleMat.column0.magnitude();
    geometry.scale.scale.y *= scaleMat.column1.magnitude();
    geometry.scale.scale.z *= scaleMat.column2.magnitude();

    /*    // MONITOR: only allows for one kind of scaling
        geometry.scale.rotation = pose.q.getConjugate();
        geometry.scale.scale = geometry.scale.scale.multiply(scaling);
    */
    pose.p = pose.p.multiply(scaling);
}

// Scales the given shape as precisely as possible.
void Scale(PxTriangleMeshGeometry&geometry, PxTransform&pose, const PxVec3&scaling) {
    // TODO: Remove WORKAROUND as soon as possible

    // Shape-space approximation
    const PxMat33 scaleMat =
        PxMat33::createDiagonal(scaling) *
        PxMat33(pose.q);  // == (pose^-1 * scaling)^T
    geometry.scale.scale.x *= scaleMat.column0.magnitude();
    geometry.scale.scale.y *= scaleMat.column1.magnitude();
    geometry.scale.scale.z *= scaleMat.column2.magnitude();

    /*    // MONITOR: only allows for one kind of scaling
        geometry.scale.rotation = pose.q.getConjugate();
        geometry.scale.scale = geometry.scale.scale.multiply(scaling);
    */
    pose.p = pose.p.multiply(scaling);
}
};
