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
#ifndef _TRANSFORM_H_
#define _TRANSFORM_H_

#include "TransformInterface.h"

namespace Divide {

//Not thread safe!
class Transform final : public ITransform, public GUIDWrapper, NonCopyable {
   public:

    Transform() noexcept = default;
    explicit Transform(const Quaternion<F32>& orientation,
                       const vec3<F32>& translation,
                       const vec3<F32>& scale);

    void setPosition(const vec3<F32>& position) override;
    void setPosition(F32 x, F32 y, F32 z) override;
    void setPositionX(F32 positionX) override;
    void setPositionY(F32 positionY) override;
    void setPositionZ(F32 positionZ) override;
    void translate(const vec3<F32>& axisFactors) override;

    void setScale(const vec3<F32>& amount) override;
    void setScaleX(F32 amount) override;
    void setScaleY(F32 amount) override;
    void setScaleZ(F32 amount) override;
    void scale(const vec3<F32>& axisFactors) override;
    void scaleX(F32 amount) override;
    void scaleY(F32 amount) override;
    void scaleZ(F32 amount) override;

    void setRotation(const vec3<F32>& axis, Angle::DEGREES<F32> degrees) override;
    void setRotation(Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> roll) override;
    void setRotation(const Quaternion<F32>& quat) override;
    void setRotationX(Angle::DEGREES<F32> angle) override;
    void setRotationY(Angle::DEGREES<F32> angle) override;
    void setRotationZ(Angle::DEGREES<F32> angle) override;
    void rotate(const vec3<F32>& axis, Angle::DEGREES<F32> degrees) override;
    void rotate(Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> roll) override;
    void rotate(const Quaternion<F32>& quat) override;
    void rotateSlerp(const Quaternion<F32>& quat, D64 deltaTime) override;
    void rotateX(Angle::DEGREES<F32> angle) override;
    void rotateY(Angle::DEGREES<F32> angle) override;
    void rotateZ(Angle::DEGREES<F32> angle) override;

    void getScale(vec3<F32>& scaleOut) const override;
    void getPosition(vec3<F32>& posOut) const override;
    void getOrientation(Quaternion<F32>& quatOut) const override;

    [[nodiscard]] bool isUniformScale() const;

    void getMatrix(mat4<F32>& matrix) override;

    /// Sets the transform to match a certain transformation matrix.
    /// Scale, orientation and translation are extracted from the specified matrix
    void setTransforms(const mat4<F32>& transform);

    /// Set all of the internal values to match those of the specified transform
    void clone(const Transform* transform);

    /// Extract the 3 transform values (position, scale, rotation) from the current instance
    [[nodiscard]] TransformValues getValues() const override;
    /// Set position, scale and rotation based on the specified transform values
    void setValues(const TransformValues& values);

    /// Compares 2 transforms
    bool operator==(const Transform& other) const;
    bool operator!=(const Transform& other) const;

    /// Reset transform to identity
    void identity();

   private:
    /// The actual scale, rotation and translation values
    TransformValues _transformValues {};
    /// This is the actual model matrix, but it will not convert to world space as it depends on it's parent in graph
    mat4<F32> _worldMatrix {};
    /// _dirty is set to true whenever a translation, rotation or scale is applied
    bool _dirty = false;
    /// _rebuild is true when a rotation or scale is applied to avoid rebuilding matrices on translation
    bool _rebuild = false;
};

}  // namespace Divide

#endif

#include "Transform.inl"