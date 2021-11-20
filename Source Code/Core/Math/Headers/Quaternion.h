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
#ifndef _QUATERNION_H_
#define _QUATERNION_H_

/*
http://gpwiki.org/index.php/OpenGL:Tutorials:Using_Quaternions_to_represent_rotation
Quaternion class based on code from " OpenGL:Tutorials:Using Quaternions to
represent rotation "
*/

namespace Divide {

template <typename T>
class Quaternion {
    static_assert(std::is_arithmetic<T>::value &&
                  !std::is_same<T, bool>::value,
                  "non-arithmetic quaternion type");
   public:
    Quaternion() noexcept;
    Quaternion(T x, T y, T z, T w) noexcept;
    explicit Quaternion(const vec4<T>& values) noexcept;

    template<typename U = T, class = typename std::enable_if<std::is_same<U, F32>::value>::type>
    explicit Quaternion(__m128 reg) noexcept : _elements(reg) {}

    explicit Quaternion(const mat3<T>& rotationMatrix) noexcept;
    Quaternion(const vec3<T>& axis, Angle::DEGREES<T> angle) noexcept;
    Quaternion(Angle::DEGREES<T> pitch, Angle::DEGREES<T> yaw, Angle::DEGREES<T> roll) noexcept;
    Quaternion(const Quaternion& q) noexcept;

    Quaternion& operator=(const Quaternion& q) noexcept;

    [[nodiscard]] T dot(const Quaternion& rq) const noexcept;
    [[nodiscard]] T magnitude() const;
    [[nodiscard]] T magnituteSQ() const;

    [[nodiscard]] bool compare(const Quaternion& rq, Angle::DEGREES<T> tolerance = 1e-3f) const;

    void set(const vec4<T>& values) noexcept;
    void set(T x, T y, T z, T w) noexcept;
    void set(const Quaternion& q) noexcept;

    //! normalizing a quaternion works similar to a vector. This method will not
    //do anything
    //! if the quaternion is close enough to being unit-length.
    void normalize() noexcept;
    [[nodiscard]] Quaternion inverse() const;

    //! We need to get the inverse of a quaternion to properly apply a
    //quaternion-rotation to a vector
    //! The conjugate of a quaternion is the same as the inverse, as long as the
    //quaternion is unit-length
    [[nodiscard]] Quaternion getConjugate() const;

    //! Multiplying q1 with q2 applies the rotation q2 to q1
    //! the constructor takes its arguments as (x, y, z, w)
    Quaternion operator*(const Quaternion& rq) const noexcept;

    //! Multiply so that rotations are applied in a left to right order.
    Quaternion& operator*=(const Quaternion& rq) noexcept;

    //! Dividing q1 by q2
    Quaternion operator/(const Quaternion& rq) const;
    Quaternion& operator/=(const Quaternion& rq);

    //! Multiplying a quaternion q with a vector v applies the q-rotation to v
    vec3<T> operator*(const vec3<T>& vec) const noexcept;

    bool operator==(const Quaternion& rq) const;
    bool operator!=(const Quaternion& rq) const;

    Quaternion& operator+=(const Quaternion& rq);

    Quaternion& operator-=(const Quaternion& rq);

    Quaternion& operator*=(T scalar);

    Quaternion& operator/=(T scalar);

    Quaternion operator+(const Quaternion& rq) const;

    Quaternion operator-(const Quaternion& rq) const;

    Quaternion operator*(T scalar) const;

    Quaternion operator/(T scalar) const;

    void slerp(const Quaternion& q, F32 t) noexcept;

    void slerp(const Quaternion& q0, const Quaternion& q1, F32 t) noexcept;

    //! Convert from Axis Angle
    void fromAxisAngle(const vec3<T>& v, Angle::DEGREES<T> angle) noexcept;

    void fromEuler(const vec3<Angle::DEGREES<T>>& v) noexcept;

    //! Convert from Euler Angles
    void fromEuler(Angle::DEGREES<T> pitch, Angle::DEGREES<T> yaw, Angle::DEGREES<T> roll) noexcept;

    void fromRotation(const vec3<T>& sourceDirection, const vec3<T>& destinationDirection, const vec3<T>& up);

    // a la Ogre3D
    void fromMatrix(const mat3<T>& rotationMatrix) noexcept;

    void fromMatrix(const mat4<T>& viewMatrix) noexcept;

    //! Convert to Matrix
    void getMatrix(mat3<T>& outMatrix) const noexcept;

    //! Convert to Axis/Angles
    void getAxisAngle(vec3<T>& axis, Angle::DEGREES<T>& angle) const;

    vec3<Angle::RADIANS<T>> getEuler() const noexcept;


    /// X/Y/Z Axis get/set a la Ogre: OgreQuaternion.cpp
    void fromAxes(const vec3<T>* axis);
    void fromAxes(const vec3<T>& xAxis, const vec3<T>& yAxis, const vec3<T>& zAxis);
    void toAxes(vec3<T>* axis) const;
    void toAxes(vec3<T>& xAxis, vec3<T>& yAxis, vec3<T>& zAxis) const;
    [[nodiscard]] vec3<T> xAxis() const noexcept;
    [[nodiscard]] vec3<T> yAxis() const noexcept;
    [[nodiscard]] vec3<T> zAxis() const noexcept;

    [[nodiscard]] T X() const noexcept;
    [[nodiscard]] T Y() const noexcept;
    [[nodiscard]] T Z() const noexcept;
    [[nodiscard]] T W() const noexcept;

    [[nodiscard]] vec3<T> XYZ() const noexcept;

    template<typename U>
    void X(U x) noexcept;
    template<typename U>
    void Y(U y) noexcept;
    template<typename U>
    void Z(U z) noexcept;
    template<typename U>
    void W(U w) noexcept;

    void identity() noexcept;

    [[nodiscard]] const vec4<T>& asVec4() const noexcept;

   private:
    vec4<T> _elements;
};

/// get the shortest arc quaternion to rotate vector 'v' to the target vector 'u'
/// (from Ogre3D!)
template <typename T>
Quaternion<T> RotationFromVToU(const vec3<T>& v, const vec3<T>& u, const vec3<T>& fallbackAxis = VECTOR3_ZERO) noexcept;

template <typename T>
Quaternion<T> Slerp(const Quaternion<T>& q0, const Quaternion<T>& q1, F32 t) noexcept;

template <typename T>
mat3<T> GetMatrix(const Quaternion<T>& q) noexcept;

template <typename T>
vec3<Angle::RADIANS<T>> GetEuler(const Quaternion<T>& q);

template <typename T>
vec3<T> operator*(vec3<T> const & v, Quaternion<T> const & q);

template <typename T>
vec3<T> Rotate(vec3<T> const & v, Quaternion<T> const & q) noexcept;

template <typename T>
vec3<T> DirectionFromAxis(const Quaternion<T>& q, const vec3<T>& AXIS) noexcept;

template <typename T>
vec3<T> DirectionFromEuler(vec3<Angle::DEGREES<T>> const & euler, const vec3<T>& FORWARD_DIRECTION);
}  // namespace Divide

#endif

#include "Quaternion.inl"