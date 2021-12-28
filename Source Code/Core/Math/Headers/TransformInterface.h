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
#ifndef _TRANSFORM_INTERFACE_H_
#define _TRANSFORM_INTERFACE_H_

namespace Divide {

struct TransformValues {
    /// All orientation/rotation info is stored in a Quaternion
    /// (because they are awesome and also have an internal mat4 if needed)
    Quaternion<F32> _orientation = {};
    /// The object's position in the world as a 3 component vector
    vec3<F32> _translation = VECTOR3_ZERO;
    /// Scaling is stored as a 3 component vector.
    /// This helps us check more easily if it's an uniform scale or not
    vec3<F32> _scale = VECTOR3_UNIT;
};

bool operator==(const TransformValues& lhs, const TransformValues& rhs);
bool operator!=(const TransformValues& lhs, const TransformValues& rhs);

class ITransform {
public:
	virtual ~ITransform() = default;
	/// Set the local X,Y and Z position
    virtual void setPosition(const vec3<F32>& position) = 0;
    /// Set the local X,Y and Z position
    virtual void setPosition(F32 x, F32 y, F32 z) = 0;
    /// Set the object's position on the X axis
    virtual void setPositionX(F32 positionX) = 0;
    /// Set the object's position on the Y axis
    virtual void setPositionY(F32 positionY) = 0;
    /// Set the object's position on the Z axis
    virtual void setPositionZ(F32 positionZ) = 0;
    /// Add the specified translation factors to the current local position
    virtual void translate(const vec3<F32>& axisFactors) = 0;

    /// Set the local X,Y and Z scale factors
    virtual void setScale(const vec3<F32>& amount) = 0;
    /// Set the scaling factor on the X axis
    virtual void setScaleX(F32 amount) = 0;
    /// Set the scaling factor on the Y axis
    virtual void setScaleY(F32 amount) = 0;
    /// Set the scaling factor on the Z axis
    virtual void setScaleZ(F32 amount) = 0;
    /// Add the specified scale factors to the current local position
    virtual void scale(const vec3<F32>& axisFactors) = 0;
    /// Increase the scaling factor on the X axis by the specified factor
    virtual void scaleX(F32 amount) = 0;
    /// Increase the scaling factor on the Y axis by the specified factor
    virtual void scaleY(F32 amount) = 0;
    /// Increase the scaling factor on the Z axis by the specified factor
    virtual void scaleZ(F32 amount) = 0;

    /// Set the local orientation using the Axis-Angle system.
    /// The angle can be in either degrees(default) or radians
    virtual void setRotation(const vec3<F32>& axis, Angle::DEGREES<F32> degrees) = 0;
    /// Set the local orientation using the Euler system.
    /// The angles can be in either degrees(default) or radians
    virtual void setRotation(Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> roll) = 0;
    /// Set the local orientation so that it matches the specified quaternion.
    virtual void setRotation(const Quaternion<F32>& quat) = 0;
    /// Set the rotation on the X axis (Axis-Angle used) by the specified angle
    /// (either degrees or radians)
    virtual void setRotationX(Angle::DEGREES<F32> angle) = 0;
    /// Set the rotation on the Y axis (Axis-Angle used) by the specified angle
    /// (either degrees or radians)
    virtual void setRotationY(Angle::DEGREES<F32> angle) = 0;
    /// Set the rotation on the Z axis (Axis-Angle used) by the specified angle
    /// (either degrees or radians)
    virtual void setRotationZ(Angle::DEGREES<F32> angle) = 0;
    /// Apply the specified Axis-Angle rotation starting from the current orientation.
    /// The angles can be in either degrees(default) or radians
    virtual void rotate(const vec3<F32>& axis, Angle::DEGREES<F32> degrees) = 0;
    /// Apply the specified Euler rotation starting from the current orientation.
    /// The angles can be in either degrees(default) or radians
    virtual void rotate(Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> roll) = 0;
    /// Apply the specified Quaternion rotation starting from the current orientation.
    virtual void rotate(const Quaternion<F32>& quat) = 0;
    /// Perform a SLERP rotation towards the specified quaternion
    virtual void rotateSlerp(const Quaternion<F32>& quat, D64 deltaTime) = 0;
    /// Rotate on the X axis (Axis-Angle used) by the specified angle (either degrees or radians)
    virtual void rotateX(Angle::DEGREES<F32> angle) = 0;
    /// Rotate on the Y axis (Axis-Angle used) by the specified angle (either degrees or radians)
    virtual void rotateY(Angle::DEGREES<F32> angle) = 0;
    /// Rotate on the Z axis (Axis-Angle used) by the specified angle (either degrees or radians)
    virtual void rotateZ(Angle::DEGREES<F32> angle) = 0;

    /// Set an uniform scale on all three axis
    void setScale(const F32 amount) {
        setScale(vec3<F32>(amount));
    }

    void setScale(const F32 x, const F32 y, const F32 z) {
        setScale(vec3<F32>(x, y, z));
    }

    /// Set the euler rotation in degrees
    void setRotationEuler(const vec3<F32>& euler) {
        setRotation(euler.pitch, euler.yaw, euler.roll);
    }

    void translate(const F32 x, const F32 y, const F32 z) {
        translate(vec3<F32>(x, y, z));
    }
    
    /// Translate the object on the X axis by the specified amount
    void translateX(const F32 positionX) {
        translate(vec3<F32>(positionX, 0.0f, 0.0f));
    }

    /// Translate the object on the Y axis by the specified amount
    void translateY(const F32 positionY) {
        translate(vec3<F32>(0.0f, positionY, 0.0f));
    }

    /// Translate the object on the Z axis by the specified amount
    void translateZ(const F32 positionZ) {
        translate(vec3<F32>(0.0f, 0.0f, positionZ));
    }
    /// Increase the scaling factor on all three axis by an uniform factor
    void scale(const F32 amount) {
        scale(vec3<F32>(amount));
    }

    void scale(const F32 x, const F32 y, const F32 z) {
        scale(vec3<F32>(x, y, z));
    }

    void rotate(const F32 xAxis, const F32 yAxis, const F32 zAxis, const Angle::DEGREES<F32> degrees) {
        rotate(vec3<F32>(xAxis, yAxis, zAxis), degrees);
    }

    void rotate(const vec3<F32>& euler) {
        rotate(euler.pitch, euler.yaw, euler.roll);
    }
    
    /// Return the scale factor
    virtual void getScale(vec3<F32>& scaleOut) const = 0;
    /// Return the position
    virtual void getPosition(vec3<F32>& posOut) const = 0;
    /// Return the orientation quaternion
    virtual void getOrientation(Quaternion<F32>& quatOut) const = 0;
};


}
#endif //_TRANSFORM_INTERFACE_H_
