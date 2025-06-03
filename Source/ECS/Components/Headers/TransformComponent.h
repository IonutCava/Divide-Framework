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

namespace Divide 
{
namespace Attorney {
    class TransformComponentSGN;
}

enum class NodeUsageContext : U8;

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
        enum class ScalingMode : U8
        {
            UNIFORM = 0u,
            NON_UNIFORM
            //PROPAGATE_TO_LEAFS_ALL,
            //PROPAGATE_TO_LEAFS_NON_UNIFORM
        };

        enum class RotationMode: U8
        {
            LOCAL,
            RELATIVE_TO_PARENT
        };

        struct Values
        {

            mat4<F32> _matrix{ MAT4_IDENTITY };
            TransformValues _previousValues;
            TransformValues _values;
            bool _computed = false;
        };

    public:
     TransformComponent(SceneGraphNode* parentSGN, PlatformContext& context);

     void reset();

                   void             getWorldMatrix(mat4<F32>& matOut) const;
     [[nodiscard]] const mat4<F32>& getWorldMatrix() const;

     /// Component <-> Transform interface
     void setPosition(const float3& position) override;
     void setPosition(F32 x, F32 y, F32 z) override;
     void setPositionX(F32 positionX) override;
     void setPositionY(F32 positionY) override;
     void setPositionZ(F32 positionZ) override;
     void translate(const float3& axisFactors) override;
     using ITransform::setPosition;

     void setScale(F32 amount) override;
     void setScale(const float3& amount) override;
     void setScale(F32 X, F32 Y, F32 Z) override;
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
     void setRotation(const quatf& quat) override;
     void setRotationX(Angle::DEGREES_F angle) override;
     void setRotationY(Angle::DEGREES_F angle) override;
     void setRotationZ(Angle::DEGREES_F angle) override;
     using ITransform::setRotation;

     void rotate(const float3& axis, Angle::DEGREES_F degrees) override;
     void rotate(Angle::DEGREES_F pitch, Angle::DEGREES_F yaw, Angle::DEGREES_F roll) override;
     void rotate(const quatf& quat) override;
     void rotateSlerp(const quatf& quat, D64 deltaTime) override;
     void rotateX(Angle::DEGREES_F angle) override;
     void rotateY(Angle::DEGREES_F angle) override;
     void rotateZ(Angle::DEGREES_F angle) override;
     using ITransform::rotate;

     [[nodiscard]] const float3 getLocalDirection(const float3& worldForward = WORLD_Z_NEG_AXIS) const;
     [[nodiscard]] const float3 getWorldDirection(const float3& worldForward = WORLD_Z_NEG_AXIS) const;

     /// Sets a new, local only, direction for the current component based on the specified world forward direction
     void setDirection(const float3& fwdDirection, const float3& upDirection = WORLD_Y_AXIS);
     void setTransform(const TransformValues& values);

     /// Return the position
     [[nodiscard]] const float3& getWorldPosition() const;
     /// Return the local position
     [[nodiscard]] const float3& getLocalPosition() const;

     /// Return the derived forward direction
     [[nodiscard]] float3 getFwdVector() const;
     /// Return the derived up direction
     [[nodiscard]] float3 getUpVector() const;
     /// Return the derived right direction
     [[nodiscard]] float3 getRightVector() const;

     /// Return the scale factor
     [[nodiscard]] const float3& getWorldScale() const;
     /// Return the local scale factor
     [[nodiscard]] const float3& getLocalScale() const;

     /// Return the orientation quaternion
     [[nodiscard]] const quatf& getWorldOrientation() const;
     /// Return the local orientation quaternion
     [[nodiscard]] const quatf& getLocalOrientation() const;

     void setTransforms(const mat4<F32>& transform);

     [[nodiscard]] TransformValues getLocalValues() const;

     void pushTransforms();
     bool popTransforms();

     [[nodiscard]] bool saveCache(ByteBuffer& outputBuffer) const override;
     [[nodiscard]] bool loadCache(ByteBuffer& inputBuffer) override;


     PROPERTY_RW(bool, editorLockPosition, false);
     PROPERTY_RW(bool, editorLockRotation, false);
     PROPERTY_RW(bool, editorLockScale, false);

     PROPERTY_RW(ScalingMode, scalingMode, ScalingMode::UNIFORM);
     PROPERTY_RW(RotationMode, rotationMode, RotationMode::RELATIVE_TO_PARENT);

     PROPERTY_R_IW(Values, local);
     PROPERTY_R_IW(Values, world);

  protected:
     friend class TransformSystem;
     template<typename T, typename U>
     friend class ECSSystem;


     void setTransformDirty(TransformType type) noexcept;
     void setTransformDirty(U32 typeMask) noexcept;

     void onParentTransformDirty(U32 transformMask) noexcept;
     void onParentUsageChanged(NodeUsageContext context) noexcept;

     void onParentChanged(const SceneGraphNode* oldParent, const SceneGraphNode* newParent);

     void getScale(float3& scaleOut) const override;
     void getPosition(float3& posOut) const override;
     void getOrientation(quatf& quatOut) const override;

     //Derived = World
     [[nodiscard]] quatf  getDerivedOrientation() const;
     [[nodiscard]] float3 getDerivedPosition()    const;
     [[nodiscard]] float3 getDerivedScale()       const;


  private:
    using TransformStack = std::stack<TransformValues>;

    std::atomic_uint _transformUpdatedMask{};
    TransformStack   _transformStack{};
    Transform        _transformInterface;

    NodeUsageContext _parentUsageContext;

    U32 _broadcastMask = 0u;
    bool _uniformScaled = true;

    mutable SharedMutex _lock{};


END_COMPONENT(Transform);

namespace Attorney
{
    class TransformComponentSGN
    {
        static void onParentTransformDirty(TransformComponent& comp, const U32 transformMask) noexcept
        {
            comp.onParentTransformDirty(transformMask);
        }

        static void onParentUsageChanged(TransformComponent& comp, const NodeUsageContext context) noexcept
        {
            comp.onParentUsageChanged(context);
        }
        
        static void onParentChanged(TransformComponent& comp, const SceneGraphNode* oldParent, const SceneGraphNode* newParent) noexcept
        {
            comp.onParentChanged(oldParent, newParent);
        }
        friend class Divide::SceneGraphNode;
    };

} //namespace Attorney

} //namespace Divide

#endif //DVD_TRANSFORM_COMPONENT_H_
