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
#ifndef DVD_TRANSFORM_H_
#define DVD_TRANSFORM_H_

#include "TransformInterface.h"

namespace Divide {

//Not thread safe!
class Transform final : public TransformValues, public ITransform, public GUIDWrapper, NonCopyable {
   public:

    Transform() noexcept = default;
    explicit Transform(const Quaternion<F32>& orientation,
                       const float3& translation,
                       const float3& scale) noexcept;

    void setPosition(const float3& position) noexcept override;
    void setPosition(F32 x, F32 y, F32 z) noexcept override;
    void setPositionX(F32 positionX) noexcept override;
    void setPositionY(F32 positionY) noexcept override;
    void setPositionZ(F32 positionZ) noexcept override;
    void translate(const float3& axisFactors) noexcept override;

    void setScale(const float3& amount) noexcept override;
    void setScaleX(F32 amount) noexcept override;
    void setScaleY(F32 amount) noexcept override;
    void setScaleZ(F32 amount) noexcept override;
    void scale(const float3& axisFactors) noexcept override;
    void scaleX(F32 amount) noexcept override;
    void scaleY(F32 amount) noexcept override;
    void scaleZ(F32 amount) noexcept override;

    void setRotation(const float3& axis, Angle::DEGREES_F degrees) noexcept override;
    void setRotation(Angle::DEGREES_F pitch, Angle::DEGREES_F yaw, Angle::DEGREES_F roll) noexcept override;
    void setRotation(const Quaternion<F32>& quat) noexcept override;
    void setRotationX(Angle::DEGREES_F angle) noexcept override;
    void setRotationY(Angle::DEGREES_F angle) noexcept override;
    void setRotationZ(Angle::DEGREES_F angle) noexcept override;
    void rotate(const float3& axis, Angle::DEGREES_F degrees) noexcept override;
    void rotate(Angle::DEGREES_F pitch, Angle::DEGREES_F yaw, Angle::DEGREES_F roll) noexcept override;
    void rotate(const Quaternion<F32>& quat) noexcept override;
    void rotateSlerp(const Quaternion<F32>& quat, D64 deltaTime) override;
    void rotateX(Angle::DEGREES_F angle) noexcept override;
    void rotateY(Angle::DEGREES_F angle) noexcept override;
    void rotateZ(Angle::DEGREES_F angle) noexcept override;

    void getScale(float3& scaleOut) const noexcept override;
    void getPosition(float3& posOut) const noexcept override;
    void getOrientation(Quaternion<F32>& quatOut) const noexcept override;

    [[nodiscard]] bool isUniformScale(const F32 tolerance = EPSILON_F32) const noexcept;

    /// Sets the transform to match a certain transformation matrix.
    /// Scale, orientation and translation are extracted from the specified matrix
    void setTransforms(const mat4<F32>& transform);

    /// Set all of the internal values to match those of the specified transform
    void clone(const Transform* transform) noexcept;

    /// Set position, scale and rotation based on the specified transform values
    void setValues(const TransformValues& values) noexcept;

    /// Compares 2 transforms
    bool operator==(const Transform& other) const;
    bool operator!=(const Transform& other) const;

    /// Reset transform to identity
    void identity() noexcept;
};

}  // namespace Divide

#endif //DVD_TRANSFORM_H_

#include "Transform.inl"
