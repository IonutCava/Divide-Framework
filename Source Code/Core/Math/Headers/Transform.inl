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

#ifndef _TRANSFORM_INL_
#define _TRANSFORM_INL_

namespace Divide {

inline bool operator==(const TransformValues& lhs, const TransformValues& rhs) {
    return lhs._scale.compare(rhs._scale) &&
           lhs._orientation.compare(rhs._orientation) &&
           lhs._translation.compare(rhs._translation);
}

inline bool operator!=(const TransformValues& lhs, const TransformValues& rhs) {
    return !lhs._scale.compare(rhs._scale) ||
           !lhs._orientation.compare(rhs._orientation) ||
           !lhs._translation.compare(rhs._translation);
}

/// Set the local X,Y and Z position
inline void Transform::setPosition(const vec3<F32>& position) noexcept {
    setPosition(position.x, position.y, position.z);
}

/// Set the local X,Y and Z position
inline void Transform::setPosition(const F32 x, const F32 y, const F32 z) noexcept {
    _dirty = true;
    _transformValues._translation.set(x, y, z);
}

/// Set the object's position on the X axis
inline void Transform::setPositionX(const F32 positionX) noexcept {
    setPosition(positionX,
                _transformValues._translation.y,
                _transformValues._translation.z);
}
/// Set the object's position on the Y axis
inline void Transform::setPositionY(const F32 positionY) noexcept {
    setPosition(_transformValues._translation.x,
                positionY,
                _transformValues._translation.z);
}

/// Set the object's position on the Z axis
inline void Transform::setPositionZ(const F32 positionZ) noexcept {
    setPosition(_transformValues._translation.x,
                _transformValues._translation.y,
                positionZ);
}

/// Set the local X,Y and Z amount factors
inline void Transform::setScale(const vec3<F32>& amount) noexcept {
    _dirty = true;
    _rebuild = true;

    _transformValues._scale.set(amount);
}

/// Set the local orientation using the Axis-Angle system.
/// The angle can be in either degrees(default) or radians
inline void Transform::setRotation(const vec3<F32>& axis, const Angle::DEGREES<F32> degrees) noexcept {
    _dirty = true;
    _rebuild = true;

    _transformValues._orientation.fromAxisAngle(axis, degrees);
    _transformValues._orientation.normalize();
}

/// Set the local orientation using the Euler system.
/// The angles can be in either degrees(default) or radians
inline void Transform::setRotation(const Angle::DEGREES<F32> pitch, const Angle::DEGREES<F32> yaw, const Angle::DEGREES<F32> roll) noexcept {
    _dirty = true;
    _rebuild = true;

    _transformValues._orientation.fromEuler(pitch, yaw, roll);
    _transformValues._orientation.normalize();
}

/// Set the local orientation so that it matches the specified quaternion.
inline void Transform::setRotation(const Quaternion<F32>& quat) noexcept {
    _dirty = true;
    _rebuild = true;

    _transformValues._orientation.set(quat);
    _transformValues._orientation.normalize();
}

/// Add the specified translation factors to the current local position
inline void Transform::translate(const vec3<F32>& axisFactors) noexcept {
    _dirty = true;
    _transformValues._translation += axisFactors;
}

/// Add the specified scale factors to the current local position
inline void Transform::scale(const vec3<F32>& axisFactors) noexcept {
    _dirty = true;
    _rebuild = true;

    _transformValues._scale *= axisFactors;
}

/// Apply the specified Axis-Angle rotation starting from the current
/// orientation.
/// The angles can be in either degrees(default) or radians
inline void Transform::rotate(const vec3<F32>& axis, const Angle::DEGREES<F32> degrees) noexcept {
    rotate(Quaternion<F32>(axis, degrees));
}

/// Apply the specified Euler rotation starting from the current
/// orientation.
/// The angles can be in either degrees(default) or radians
inline void Transform::rotate(const Angle::DEGREES<F32> pitch, const Angle::DEGREES<F32> yaw, const Angle::DEGREES<F32> roll) noexcept {
    rotate(Quaternion<F32>(pitch, yaw, roll));
}

/// Apply the specified Quaternion rotation starting from the current orientation.
inline void Transform::rotate(const Quaternion<F32>& quat) noexcept {
    setRotation(quat * _transformValues._orientation);
}

/// Perform a SLERP rotation towards the specified quaternion
inline void Transform::rotateSlerp(const Quaternion<F32>& quat, const D64 deltaTime) {
    _dirty = true;
    _rebuild = true;

    _transformValues._orientation.slerp(quat, to_F32(deltaTime));
    _transformValues._orientation.normalize();
}

/// Set the scaling factor on the X axis
inline void Transform::setScaleX(const F32 amount) noexcept {
    setScale(vec3<F32>(amount, _transformValues._scale.y,
                                _transformValues._scale.z));
}
/// Set the scaling factor on the Y axis
inline void Transform::setScaleY(const F32 amount) noexcept {
    setScale(vec3<F32>(_transformValues._scale.x, amount,
                                _transformValues._scale.z));
}
/// Set the scaling factor on the Z axis
inline void Transform::setScaleZ(const F32 amount) noexcept {
    setScale(vec3<F32>(_transformValues._scale.x,
                                _transformValues._scale.y, amount));
}

/// Increase the scaling factor on the X axis by the specified factor
inline void Transform::scaleX(const F32 amount) noexcept {
    scale(vec3<F32>(amount,
                    _transformValues._scale.y,
                    _transformValues._scale.z));
}
/// Increase the scaling factor on the Y axis by the specified factor
inline void Transform::scaleY(const F32 amount) noexcept {
    scale(vec3<F32>(_transformValues._scale.x,
                    amount,
                    _transformValues._scale.z));
}
/// Increase the scaling factor on the Z axis by the specified factor
inline void Transform::scaleZ(const F32 amount) noexcept {
    scale(vec3<F32>(_transformValues._scale.x,
                    _transformValues._scale.y,
                    amount));
}
/// Rotate on the X axis (Axis-Angle used) by the specified angle (either
/// degrees or radians)
inline void Transform::rotateX(const Angle::DEGREES<F32> angle) noexcept {
    rotate(vec3<F32>(1, 0, 0), angle);
}
/// Rotate on the Y axis (Axis-Angle used) by the specified angle (either
/// degrees or radians)
inline void Transform::rotateY(const Angle::DEGREES<F32> angle) noexcept {
    rotate(vec3<F32>(0, 1, 0), angle);
}
/// Rotate on the Z axis (Axis-Angle used) by the specified angle (either
/// degrees or radians)
inline void Transform::rotateZ(const Angle::DEGREES<F32> angle) noexcept {
    rotate(vec3<F32>(0, 0, 1), angle);
}
/// Set the rotation on the X axis (Axis-Angle used) by the specified angle
/// (either degrees or radians)
inline void Transform::setRotationX(const Angle::DEGREES<F32> angle) noexcept {
    setRotation(vec3<F32>(1, 0, 0), angle);
}
/// Set the rotation on the Y axis (Axis-Angle used) by the specified angle
/// (either degrees or radians)
inline void Transform::setRotationY(const Angle::DEGREES<F32> angle) noexcept {
    setRotation(vec3<F32>(0, 1, 0), angle);
}
/// Set the rotation on the Z axis (Axis-Angle used) by the specified angle
/// (either degrees or radians)
inline void Transform::setRotationZ(const Angle::DEGREES<F32> angle) noexcept {
    setRotation(vec3<F32>(0, 0, 1), angle);
}

/// Return the scale factor
inline void Transform::getScale(vec3<F32>& scaleOut) const noexcept {
    scaleOut.set(_transformValues._scale);
}

/// Return the position
inline void Transform::getPosition(vec3<F32>& posOut) const noexcept {
    posOut.set(_transformValues._translation);
}

/// Return the orientation quaternion
inline void Transform::getOrientation(Quaternion<F32>& quatOut) const noexcept {
    quatOut.set(_transformValues._orientation);
}

inline bool Transform::isUniformScale(const F32 tolerance) const noexcept {
    return _transformValues._scale.isUniform(tolerance);
}

inline void Transform::clone(const Transform* const transform) noexcept {
    _dirty = true;
    _rebuild = true;

    _transformValues = transform->getValues();
}

inline TransformValues Transform::getValues() const noexcept {
    return _transformValues;
}

inline const TransformValues& Transform::getValuesRef() const noexcept {
    return _transformValues;
}

/// Set position, scale and rotation based on the specified transform values
inline void Transform::setValues(const TransformValues& values) noexcept {
    _dirty = true;
    _rebuild = true;

    std::memcpy(&_transformValues, &values, sizeof(TransformValues));
}

/// Compares 2 transforms
inline bool Transform::operator==(const Transform& other) const {
    return _transformValues == other._transformValues;
}

inline bool Transform::operator!=(const Transform& other) const {
    return _transformValues != other._transformValues;
}
}

#endif //_TRANSFORM_INL_
