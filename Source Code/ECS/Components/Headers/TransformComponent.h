/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _TRANSFORM_COMPONENT_H_
#define _TRANSFORM_COMPONENT_H_

#include "SGNComponent.h"
#include "Core/Math/Headers/Transform.h"

namespace Divide {
namespace Attorney {
    class TransformComponentSGN;
}

enum class TransformType : U8 {
    NONE = 0,
    TRANSLATION = toBit(1),
    SCALE = toBit(2),
    ROTATION = toBit(3),
    ALL = TRANSLATION | SCALE | ROTATION,
    COUNT = 4
};

BEGIN_COMPONENT_EXT1(Transform, ComponentType::TRANSFORM, ITransform)
    friend class Attorney::TransformComponentSGN;

    enum class WorldMatrixType : U8 {
        CURRENT = 0u,
        PREVIOUS,
        COUNT
    };

    public:
     TransformComponent(SceneGraphNode* parentSGN, PlatformContext& context);

     void reset();

     void getPreviousWorldMatrix(mat4<F32>& matOut) const;
     void getWorldMatrix(mat4<F32>& matOut) const;
     [[nodiscard]] mat4<F32> getPreviousWorldMatrix() const;
     [[nodiscard]] mat4<F32> getWorldMatrix() const;

     void getWorldMatrix(D64 interpolationFactor, mat4<F32>& matrixOut) const;

     /// Component <-> Transform interface
     void setPosition(const vec3<F32>& position) override;
     void setPosition(F32 x, F32 y, F32 z) override;
     void setPositionX(F32 positionX) override;
     void setPositionY(F32 positionY) override;
     void setPositionZ(F32 positionZ) override;
     void translate(const vec3<F32>& axisFactors) override;
     using ITransform::setPosition;

     void setScale(const vec3<F32>& amount) override;
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

     void setTransform(const TransformValues& values);

     [[nodiscard]] bool isUniformScaled() const noexcept;

     /// Return the position
     [[nodiscard]] vec3<F32> getPosition() const noexcept;
     /// Return the local position
     [[nodiscard]] vec3<F32> getLocalPosition() const;
     /// Return the position
     [[nodiscard]] vec3<F32> getPosition(D64 interpolationFactor) const;
     /// Return the local position
     [[nodiscard]] vec3<F32> getLocalPosition(D64 interpolationFactor) const;

     [[nodiscard]] vec3<F32> getFwdVector() const noexcept;
     [[nodiscard]] vec3<F32> getUpVector() const noexcept;
     [[nodiscard]] vec3<F32> getRightVector() const noexcept;

     /// Return the scale factor
     [[nodiscard]] vec3<F32> getScale() const noexcept;
     /// Return the local scale factor
     [[nodiscard]] vec3<F32> getLocalScale() const;
     /// Return the scale factor
     [[nodiscard]] vec3<F32> getScale(D64 interpolationFactor) const;
     /// Return the local scale factor
     [[nodiscard]] vec3<F32> getLocalScale(D64 interpolationFactor) const;

     /// Return the orientation quaternion
     [[nodiscard]] Quaternion<F32> getOrientation() const noexcept;
     /// Return the local orientation quaternion
     [[nodiscard]] Quaternion<F32> getLocalOrientation() const;
     /// Return the orientation quaternion
     [[nodiscard]] Quaternion<F32> getOrientation(D64 interpolationFactor) const;
     /// Return the local orientation quaternion
     [[nodiscard]] Quaternion<F32> getLocalOrientation(D64 interpolationFactor) const;

     void setTransforms(const mat4<F32>& transform);

     [[nodiscard]] TransformValues getValues() const override;

     void pushTransforms();
     bool popTransforms();

     void resetCache();
     void setOffset(bool state, const mat4<F32>& offset = mat4<F32>()) noexcept;

     [[nodiscard]] bool saveCache(ByteBuffer& outputBuffer) const override;
     [[nodiscard]] bool loadCache(ByteBuffer& inputBuffer) override;

     void getLocalMatrix(mat4<F32>& matOut) { getMatrix(matOut); }
     void getLocalMatrix(const D64 interpolationFactor, mat4<F32>& matOut) const { getMatrix(interpolationFactor, matOut); }

     PROPERTY_R_IW(TransformValues, cachedTransform);

  protected:
     friend class TransformSystem;
     template<typename T, typename U>
     friend class ECSSystem;

     void setTransformDirty(TransformType type) noexcept;
     void setTransformDirty(U32 typeMask) noexcept;

     void updateCachedValues();

     void onParentTransformDirty(U32 transformMask) noexcept;
     void onParentUsageChanged(NodeUsageContext context) noexcept;

     void getMatrix(mat4<F32>& matOut) override;

     void getMatrix(D64 interpolationFactor, mat4<F32>& matOut) const;

     //A simple lock-unlock and mutex-free matrix calculation system //
     [[nodiscard]] vec3<F32> getLocalPositionLocked(D64 interpolationFactor) const;
     [[nodiscard]] vec3<F32> getLocalScaleLocked(D64 interpolationFactor) const;
     [[nodiscard]] Quaternion<F32> getLocalOrientationLocked(D64 interpolationFactor) const;

     //Called only when transformed changed in the main update loop!
     void updateWorldMatrix();

     // Local transform interface access (all are in local space)
     void getScale(vec3<F32>& scaleOut) const override;
     void getPosition(vec3<F32>& posOut) const override;
     void getOrientation(Quaternion<F32>& quatOut) const override;

     [[nodiscard]] Quaternion<F32> getOrientationInternal() const;
     [[nodiscard]] vec3<F32> getPositionInternal() const;
     [[nodiscard]] vec3<F32> getScaleInternal() const;
  private:
    std::pair<bool, mat4<F32>> _transformOffset;

    using TransformStack = std::stack<TransformValues>;

    std::atomic_uint _transformUpdatedMask{};
    TransformValues  _prevTransformValues;
    TransformStack   _transformStack{};
    Transform        _transformInterface;

    NodeUsageContext _parentUsageContext;

    bool _cacheDirty = true;
    bool _uniformScaled = true;
    bool _prevWorldMatrixDirty = true;

    mutable SharedMutex _worldMatrixLock{};
    mutable SharedMutex _lock{};

    eastl::array<mat4<F32>, to_base(WorldMatrixType::COUNT)> _worldMatrix;
END_COMPONENT(Transform);

namespace Attorney {
    class TransformComponentSGN {
        static void onParentTransformDirty(TransformComponent& comp, const U32 transformMask) noexcept {
            comp.onParentTransformDirty(transformMask);
        }

        static void onParentUsageChanged(TransformComponent& comp, const NodeUsageContext context) noexcept {
            comp.onParentUsageChanged(context);
        }
        friend class Divide::SceneGraphNode;
    };

} //namespace Attorney

} //namespace Divide

#endif //_TRANSFORM_COMPONENT_H_