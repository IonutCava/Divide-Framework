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
#ifndef DVD_TRANSFORM_COMPONENT_H_
#define DVD_TRANSFORM_COMPONENT_H_

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

    public:
        enum class ScalingMode : U8 {
            UNIFORM = 0u,
            NON_UNIFORM
            //PROPAGATE_TO_LEAFS_ALL,
            //PROPAGATE_TO_LEAFS_NON_UNIFORM
        };
    public:
     TransformComponent(SceneGraphNode* parentSGN, PlatformContext& context);

     void reset();

                   void      getWorldMatrix(mat4<F32>& matOut) const;
                   void      getWorldMatrixInterpolated( mat4<F32>& matrixOut) const;
     [[nodiscard]] mat4<F32> getWorldMatrix() const;
     [[nodiscard]] mat4<F32> getWorldMatrixInterpolated() const;

     /// This returns a "normal matrix". If we have uniform scaling, this is just the upper 3x3 part of our world matrix
     /// If we have non-uniform scaling, that 3x3 mat goes through an inverse-transpose step to get rid of any scaling factors but keep rotations
     /// Padded to fit into a mat4 for convenient. Can cast to a mat3 afterwards as row 3 and column 3 are just 0,0,0,1
     void getLocalRotationMatrix( mat4<F32>& matOut ) const;
     void getLocalRotationMatrixInterpolated( mat4<F32>& matOut ) const;

     /// This returns a "normal matrix". If we have uniform scaling, this is just the upper 3x3 part of our local matrix
     /// If we have non-uniform scaling, that 3x3 mat goes through an inverse-transpose step to get rid of any scaling factors but keep rotations
     /// Padded to fit into a mat4 for convenient. Can cast to a mat3 afterwards as row 3 and column 3 are just 0,0,0,1
     void getWorldRotationMatrix( mat4<F32>& matOut ) const;
     void getWorldRotationMatrixInterpolated( mat4<F32>& matOut ) const;

     /// Component <-> Transform interface
     void setPosition(const float3& position) override;
     void setPosition(F32 x, F32 y, F32 z) override;
     void setPositionX(F32 positionX) override;
     void setPositionY(F32 positionY) override;
     void setPositionZ(F32 positionZ) override;
     void translate(const float3& axisFactors) override;
     using ITransform::setPosition;

     void setScale(const float3& amount) override;
     void setScaleX(F32 amount) override;
     void setScaleY(F32 amount) override;
     void setScaleZ(F32 amount) override;
     void scale(const float3& axisFactors) override;
     void scaleX(F32 amount) override;
     void scaleY(F32 amount) override;
     void scaleZ(F32 amount) override;
     using ITransform::setScale;

     void setRotation(const float3& axis, Angle::DEGREES_F degrees) override;
     void setRotation(Angle::DEGREES_F pitch, Angle::DEGREES_F yaw, Angle::DEGREES_F roll) override;
     void setRotation(const Quaternion<F32>& quat) override;
     void setRotationX(Angle::DEGREES_F angle) override;
     void setRotationY(Angle::DEGREES_F angle) override;
     void setRotationZ(Angle::DEGREES_F angle) override;
     using ITransform::setRotation;

     void rotate(const float3& axis, Angle::DEGREES_F degrees) override;
     void rotate(Angle::DEGREES_F pitch, Angle::DEGREES_F yaw, Angle::DEGREES_F roll) override;
     void rotate(const Quaternion<F32>& quat) override;
     void rotateSlerp(const Quaternion<F32>& quat, D64 deltaTime) override;
     void rotateX(Angle::DEGREES_F angle) override;
     void rotateY(Angle::DEGREES_F angle) override;
     void rotateZ(Angle::DEGREES_F angle) override;
     using ITransform::rotate;

     [[nodiscard]] const float3 getLocalDirection(const float3& worldForward = WORLD_Z_NEG_AXIS) const;
     [[nodiscard]] const float3 getWorldDirection(const float3& worldForward = WORLD_Z_NEG_AXIS) const;

     /// Sets a new, local only, direction for the current component based on the specified world forward direction
     void setDirection(const float3& fwdDirection, const float3& upDirection = WORLD_Y_AXIS);
     void setTransform(const TransformValues& values);

     [[nodiscard]] bool isUniformScaled() const noexcept;

     /// Return the position
     [[nodiscard]] float3 getWorldPosition() const;
     /// Return the local position
     [[nodiscard]] float3 getLocalPosition() const;
     /// Return the position
     [[nodiscard]] float3 getWorldPositionInterpolated() const;
     /// Return the local position
     [[nodiscard]] float3 getLocalPositionInterpolated() const;

     /// Return the derived forward direction
     [[nodiscard]] float3 getFwdVector() const;
     /// Return the derived up direction
     [[nodiscard]] float3 getUpVector() const;
     /// Return the derived right direction
     [[nodiscard]] float3 getRightVector() const;

     /// Return the scale factor
     [[nodiscard]] float3 getWorldScale() const;
     /// Return the local scale factor
     [[nodiscard]] float3 getLocalScale() const;
     /// Return the scale factor
     [[nodiscard]] float3 getWorldScaleInterpolated() const;
     /// Return the local scale factor
     [[nodiscard]] float3 getLocalScaleInterpolated() const;

     /// Return the orientation quaternion
     [[nodiscard]] Quaternion<F32> getWorldOrientation() const;
     /// Return the local orientation quaternion
     [[nodiscard]] Quaternion<F32> getLocalOrientation() const;
     /// Return the orientation quaternion
     [[nodiscard]] Quaternion<F32> getWorldOrientationInterpolated() const;
     /// Return the local orientation quaternion
     [[nodiscard]] Quaternion<F32> getLocalOrientationInterpolated() const;

     void getWorldTransforms(float3& positionOut, float3& scaleOut, Quaternion<F32>& rotationOut);
     void getWorldTransformsInterpolated(float3& positionOut, float3& scaleOut, Quaternion<F32>& rotationOut);

     void setTransforms(const mat4<F32>& transform);

     [[nodiscard]] TransformValues getLocalValues() const;

     void pushTransforms();
     bool popTransforms();

     void resetCache();
     void setOffset(bool state, const mat4<F32>& offset = mat4<F32>()) noexcept;

     [[nodiscard]] bool saveCache(ByteBuffer& outputBuffer) const override;
     [[nodiscard]] bool loadCache(ByteBuffer& inputBuffer) override;

     PROPERTY_R_IW(TransformValues, cachedDerivedTransform);

     PROPERTY_RW(bool, editorLockPosition, false);
     PROPERTY_RW(bool, editorLockRotation, false);
     PROPERTY_RW(bool, editorLockScale, false);
     PROPERTY_RW(ScalingMode, scalingMode, ScalingMode::UNIFORM);

     PROPERTY_R_IW(mat4<F32>, localMatrix, MAT4_IDENTITY);
     PROPERTY_R_IW(mat4<F32>, localMatrixInterpolated, MAT4_IDENTITY);

  protected:
     friend class TransformSystem;
     template<typename T, typename U>
     friend class ECSSystem;


     void setTransformDirty(TransformType type) noexcept;
     void setTransformDirty(U32 typeMask) noexcept;

     void updateCachedValues();

     void onParentTransformDirty(U32 transformMask) noexcept;
     void onParentUsageChanged(NodeUsageContext context) noexcept;

     void onParentChanged(const SceneGraphNode* oldParent, const SceneGraphNode* newParent);

     // Local transform interface access (all are in local space)
     void getScale(float3& scaleOut) const override;
     void getPosition(float3& posOut) const override;
     void getOrientation(Quaternion<F32>& quatOut) const override;

     //Derived = World
     [[nodiscard]] Quaternion<F32> getDerivedOrientation() const;
     [[nodiscard]] float3       getDerivedPosition()    const;
     [[nodiscard]] float3       getDerivedScale()       const;

     //Called only when then transform changed in the main update loop!
     void updateLocalMatrix( D64 interpolationFactor );
  private:
     void updateLocalMatrixLocked();
     void updateLocalMatrixInterpolated( D64 interpolationFactor );

  private:
    std::pair<bool, mat4<F32>> _transformOffset;

    using TransformStack = std::stack<TransformValues>;

    std::atomic_uint _transformUpdatedMask{};
    TransformValues  _prevTransformValues;
    TransformValues  _transformValuesInterpolated;
    TransformStack   _transformStack{};
    Transform        _transformInterface;

    NodeUsageContext _parentUsageContext;

    bool _cacheDirty = true;
    bool _uniformScaled = true;

    mutable SharedMutex _localMatrixLock{};
    mutable SharedMutex _lock{};


END_COMPONENT(Transform);

namespace Attorney {
    class TransformComponentSGN {
        static void onParentTransformDirty(TransformComponent& comp, const U32 transformMask) noexcept {
            comp.onParentTransformDirty(transformMask);
        }

        static void onParentUsageChanged(TransformComponent& comp, const NodeUsageContext context) noexcept {
            comp.onParentUsageChanged(context);
        }
        
        static void onParentChanged(TransformComponent& comp, const SceneGraphNode* oldParent, const SceneGraphNode* newParent) noexcept {
            comp.onParentChanged(oldParent, newParent);
        }
        friend class Divide::SceneGraphNode;
    };

} //namespace Attorney

} //namespace Divide

#endif //DVD_TRANSFORM_COMPONENT_H_
