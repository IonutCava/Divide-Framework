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
#ifndef _PHYSX_ACTOR_H_
#define _PHYSX_ACTOR_H_

// PhysX includes

#include <PxPhysicsAPI.h>

#include "Physics/Headers/PhysicsAsset.h"

namespace Divide {

namespace Util {
    vec3<F32> toVec3(const physx::PxVec3& vec);
    physx::PxVec3 toVec3(const vec3<F32>& vec);
} //namespace Util

class PhysXActor final : public PhysicsAsset {
public:
    explicit PhysXActor(RigidBodyComponent& parent);
    ~PhysXActor();

    void setPosition(const vec3<F32>& position) override;
    void setPosition(F32 x, F32 y, F32 z) override;
    void setPositionX(F32 positionX) override;
    void setPositionY(F32 positionY) override;
    void setPositionZ(F32 positionZ) override;
    void translate(const vec3<F32>& axisFactors) override;
    using ITransform::setPosition;

    void setScale(const vec3<F32>& scale) override;
    void setScaleX(F32 amount) override;
    void setScaleY(F32 amount) override;
    void setScaleZ(F32 amount) override;
    void scale(const vec3<F32>& axisFactors) override;
    void scaleX(F32 amount) override;
    void scaleY(F32 amount) override;
    void scaleZ(F32 amount) override;
    using ITransform::setScale;

    void setRotation(const vec3<F32>& axis, Angle::DEGREES<F32> degrees) override;
    void setRotation(Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> roll) override;
    void setRotation(const Quaternion<F32>& quat) override;
    void setRotationX(Angle::DEGREES<F32> angle) override;
    void setRotationY(Angle::DEGREES<F32> angle) override;
    void setRotationZ(Angle::DEGREES<F32> angle) override;
    using ITransform::setRotation;

    void rotate(const vec3<F32>& axis, Angle::DEGREES<F32> degrees) override;
    void rotate(Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> roll) override;
    void rotate(const Quaternion<F32>& quat) override;
    void rotateSlerp(const Quaternion<F32>& quat, D64 deltaTime) override;
    void rotateX(Angle::DEGREES<F32> angle) override;
    void rotateY(Angle::DEGREES<F32> angle) override;
    void rotateZ(Angle::DEGREES<F32> angle) override;
    using ITransform::rotate;

    void getScale(vec3<F32>& scaleOut) const override;
    void getPosition(vec3<F32>& posOut) const override;
    void getOrientation(Quaternion<F32>& quatOut) const override;

    void getMatrix(mat4<F32>& matrixOut) override;

    [[nodiscard]] TransformValues getValues() const override;

    void physicsCollisionGroup(PhysicsGroup group) override;

protected:
    friend class PhysX;
    friend class PhysXSceneInterface;

    physx::PxRigidActor* _actor = nullptr;
    physx::PxGeometryType::Enum _type = physx::PxGeometryType::eINVALID;
    string _actorName;
    F32 _userData = 0.0f;

    mat4<F32> _cachedLocalMatrix;
};
}; //namespace Divide

#endif //_PHYSX_ACTOR_H_