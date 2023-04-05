/***************************************************************************
 * Mathlib
 *
 * Copyright (C) 2003-2004, Alexander Zaprjagaev <frustum@frustum.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 ***************************************************************************
 * Update 2004/08/19
 *
 * added ivec2, ivec3 & ivec4 methods
 * vec2, vec3 & vec4 data : added texture coords (s,t,p,q) and colour enums
 *(r,g,b,a)
 * mat3 & mat4 : added multiple float constructor ad modified methods returning
 *mat3 or mat4
 * optimisations like "x / 2.0f" replaced by faster "x * 0.5f"
 * defines of multiples usefull maths values and radian/degree conversions
 * vec2 : added methods : set, reset, compare, dot, closestPointOnLine,
 *closestPointOnSegment,
 *                        projectionOnLine, lerp, angle
 * vec3 : added methods : set, reset, compare, dot, cross, closestPointOnLine,
 *closestPointOnSegment,
 *                        projectionOnLine, lerp, angle
 * vec4 : added methods : set, reset, compare
 ***************************************************************************
 */
 /*
 *
 * MathLibrary
 * Copyright (c) 2011 NoLimitsDesigns
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.

 * If there are any concerns or questions about the code, please e-mail
smasherprog@gmail.com or visit www.nolimitsdesigns.com
 */

/*
 * Author: Scott Lee
 */

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
#ifndef _MATH_MATRICES_H_
#define _MATH_MATRICES_H_

#include "Plane.h"

namespace Divide {

/*********************************
* mat2
*********************************/
template <typename T>
class mat2 {
    static_assert(ValidMathType<T>, "Invalid base type!");

    // m0  m1
    // m2  m3
public:
    mat2() noexcept;
    template<typename U>
    explicit mat2(U m) noexcept;
    template<typename U>
    explicit mat2(U m0, U m1,
                  U m2, U m3) noexcept;
    template<typename U>
    explicit mat2(const U *values) noexcept;
    mat2(const mat2 &B) noexcept;
    template<typename U>
    explicit mat2(const mat2<U> &B) noexcept;
    template<typename U>
    explicit mat2(const mat3<U> &B) noexcept;
    template<typename U>
    explicit mat2(const mat4<U> &B) noexcept;

    template<typename U>
    [[nodiscard]] vec2<T> operator*(const vec2<U>  v) const noexcept;
    template<typename U>
    [[nodiscard]] vec3<T> operator*(const vec3<U> &v) const noexcept;
    template<typename U>
    [[nodiscard]] vec4<T> operator*(const vec4<U> &v) const noexcept;

    template<typename U>
    [[nodiscard]] mat2 operator*(const mat2<U> &B) const noexcept;
    template<typename U>
    [[nodiscard]] mat2 operator/(const mat2<U> &B) const noexcept;
    template<typename U>
    [[nodiscard]] mat2 operator+(const mat2<U> &B) const noexcept;
    template<typename U>
    [[nodiscard]] mat2 operator-(const mat2<U> &B) const noexcept;

    template<typename U>
    mat2 &operator*=(const mat2<U> &B) noexcept;
    template<typename U>
    mat2 &operator/=(const mat2<U> &B) noexcept;
    template<typename U>
    mat2 &operator+=(const mat2<U> &B) noexcept;
    template<typename U>
    mat2 &operator-=(const mat2<U> &B) noexcept;

    template<typename U>
    [[nodiscard]] mat2 operator*(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat2 operator/(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat2 operator+(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat2 operator-(U f) const noexcept;

    template<typename U>
    mat2 &operator*=(U f) noexcept;
    template<typename U>
    mat2 &operator/=(U f) noexcept;
    template<typename U>
    mat2 &operator+=(U f) noexcept;
    template<typename U>
    mat2 &operator-=(U f) noexcept;

    [[nodiscard]] bool operator==(const mat2 &B) const noexcept;
    [[nodiscard]] bool operator!=(const mat2 &B) const noexcept;
    template<typename U>
    [[nodiscard]] bool operator==(const mat2<U> &B) const noexcept;
    template<typename U>
    [[nodiscard]] bool operator!=(const mat2<U> &B) const noexcept;

    [[nodiscard]] bool compare(const mat2 &B, F32 epsilon) const noexcept;
    template<typename U>
    [[nodiscard]] bool compare(const mat2<U> &B, F32 epsilon) const noexcept;

    [[nodiscard]] operator T *();
    [[nodiscard]] operator const T *() const;

    [[nodiscard]] T& operator[](I32 i);
    [[nodiscard]] T  operator[](I32 i) const;

    [[nodiscard]] T &element(I8 row, I8 column) noexcept;
    [[nodiscard]] const T &element(I8 row, I8 column) const noexcept;

    template<typename U>
    void set(U m0, U m1, U m2, U m3) noexcept;
    template<typename U>
    void set(const U *matrix) noexcept;
    template<typename U>
    void set(const mat2<U> &matrix) noexcept;
    template<typename U>
    void set(const mat3<U> &matrix) noexcept;
    template<typename U>
    void set(const mat4<U> &matrix) noexcept;

    template<typename U>
    void setRow(I32 index, U value) noexcept;
    template<typename U>
    void setRow(I32 index, const vec2<U> value) noexcept;
    template<typename U>
    void setRow(I32 index, U x, U y) noexcept;
    [[nodiscard]] vec2<T> getRow(I32 index) const noexcept;

    template<typename U>
    void setCol(I32 index, vec2<U> value) noexcept;
    template<typename U>
    void setCol(I32 index, U value) noexcept;
    template<typename U>
    void setCol(I32 index, U x, U y) noexcept;

    [[nodiscard]] vec2<T> getCol(I32 index) const noexcept;

    void zero() noexcept;
    void identity() noexcept;
    [[nodiscard]] bool isIdentity() const noexcept;
    void swap(mat2 &B) noexcept;

    [[nodiscard]] T det() const noexcept;
    [[nodiscard]] T elementSum() const noexcept;
    void inverse() noexcept;
    void transpose() noexcept;
    void inverseTranspose() noexcept;

    [[nodiscard]] mat2 getInverse() const noexcept;
    void getInverse(mat2 &ret) const noexcept;

    [[nodiscard]] mat2 getTranspose() const noexcept;
    void getTranspose(mat2 &ret) const noexcept;

    [[nodiscard]] mat2 getInverseTranspose() const noexcept;
    void getInverseTranspose(mat2 &ret) const noexcept;

    union {
        struct {
            T _11, _12;
            T _21, _22;
        };
        T mat[4];
        T m[2][2];
        vec2<T> _vec[2];
    };
};

/*********************************
 * mat3
 *********************************/
template <typename T>
class mat3 {
    static_assert(ValidMathType<T>, "Invalid base type!");

    // m0 m1 m2
    // m3 m4 m5
    // m7 m8 m9
   public:
    mat3() noexcept;
    template<typename U>
    explicit mat3(U m) noexcept;
    template<typename U>
    explicit  mat3(U m0, U m1, U m2,
                   U m3, U m4, U m5,
                   U m6, U m7, U m8) noexcept;
    template<typename U>
    explicit mat3(const U *values) noexcept;
    template<typename U>
    explicit mat3(const mat2<U> &B, bool zeroFill) noexcept;
    mat3(const mat3 &B) noexcept;
    template<typename U>
    explicit mat3(const mat3<U> &B) noexcept;
    template<typename U>
    explicit mat3(const mat4<U> &B) noexcept;
    template<typename U>
    explicit mat3(const vec3<U>& scale) noexcept; 
    template<typename U>
    explicit mat3(const vec3<U>& rotStart, const vec3<U>& rotEnd) noexcept;

    template<typename U>
    [[nodiscard]] vec2<U> operator*(const vec2<U>  v) const noexcept;
    template<typename U>
    [[nodiscard]] vec3<U> operator*(const vec3<U> &v) const noexcept;
    template<typename U>
    [[nodiscard]] vec4<U> operator*(const vec4<U> &v) const noexcept;

    template<typename U>
    [[nodiscard]] mat3 operator*(const mat3<U> &B) const noexcept;
    template<typename U>
    [[nodiscard]] mat3 operator/(const mat3<U> &B) const noexcept;
    template<typename U>
    [[nodiscard]] mat3 operator+(const mat3<U> &B) const noexcept;
    template<typename U>
    [[nodiscard]] mat3 operator-(const mat3<U> &B) const noexcept;

    template<typename U>
    mat3 &operator*=(const mat3<U> &B) noexcept;
    template<typename U>
    mat3 &operator/=(const mat3<U> &B) noexcept;
    template<typename U>
    mat3 &operator+=(const mat3<U> &B) noexcept;
    template<typename U>
    mat3 &operator-=(const mat3<U> &B) noexcept;

    template<typename U>
    [[nodiscard]] mat3 operator*(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat3 operator/(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat3 operator+(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat3 operator-(U f) const noexcept;

    template<typename U>
    mat3 &operator*=(U f) noexcept;
    template<typename U>
    mat3 &operator/=(U f) noexcept;
    template<typename U>
    mat3 &operator+=(U f) noexcept;
    template<typename U>
    mat3 &operator-=(U f) noexcept;

    [[nodiscard]] bool operator==(const mat3 &B) const noexcept;
    [[nodiscard]] bool operator!=(const mat3 &B) const noexcept;

    template<typename U>
    [[nodiscard]] bool operator==(const mat3<U> &B) const noexcept;
    template<typename U>
    [[nodiscard]] bool operator!=(const mat3<U> &B) const noexcept;

    [[nodiscard]] bool compare(const mat3 &B, F32 epsilon) const noexcept;
    template<typename U>
    [[nodiscard]] bool compare(const mat3<U> &B, F32 epsilon) const noexcept;

    [[nodiscard]] operator T *() noexcept;
    [[nodiscard]] operator const T *() const noexcept;

    [[nodiscard]] T &operator[](I32 i) noexcept;
    [[nodiscard]] T operator[](I32 i) const noexcept;

    [[nodiscard]] T &element(I8 row, I8 column) noexcept;
    [[nodiscard]] const T &element(I8 row, I8 column) const noexcept;

    template<typename U>
    void set(U m0, U m1, U m2, U m3, U m4, U m5, U m6, U m7, U m8) noexcept;
    template<typename U>
    void set(const U *matrix) noexcept;
    template<typename U>
    void set(const mat2<U> &matrix) noexcept;
    template<typename U>
    void set(const mat3<U> &matrix) noexcept;
    template<typename U>
    void set(const mat4<U> &matrix) noexcept;

    template<typename U>
    void setRow(I32 index, U value) noexcept;
    template<typename U>
    void setRow(I32 index, const vec3<U> &value) noexcept;
    template<typename U>
    void setRow(I32 index, U x, U y, U z) noexcept;

    [[nodiscard]] const vec3<T>& getRow(I32 index) const noexcept;

    template<typename U>
    void setCol(I32 index, const vec3<U> &value) noexcept;
    template<typename U>
    void setCol(I32 index, U value) noexcept;
    template<typename U>
    void setCol(I32 index, U x, U y, U z) noexcept;

    [[nodiscard]] vec3<T> getCol(I32 index) const noexcept;

    void zero() noexcept;
    void identity() noexcept;
    [[nodiscard]] bool isIdentity() const noexcept;
    [[nodiscard]] bool isUniformScale(F32 tolerance = 0.0001f) const noexcept;
    [[nodiscard]] bool isColOrthogonal() const noexcept;
    void swap(mat3 &B) noexcept;

    [[nodiscard]] T det() const noexcept;
    [[nodiscard]] T elementSum() const noexcept;
    void inverse() noexcept;
    void transpose() noexcept;
    void inverseTranspose() noexcept;

    [[nodiscard]] mat3 getInverse() const noexcept;
    void getInverse(mat3 &ret) const noexcept;

    [[nodiscard]] mat3 getTranspose() const noexcept;
    void getTranspose(mat3 &ret) const noexcept;

    [[nodiscard]] mat3 getInverseTranspose() const noexcept;
    void getInverseTranspose(mat3 &ret) const noexcept;

    template<typename U>
    void fromRotation(const vec3<U> &v, Angle::RADIANS<U> angle);
    template<typename U>
    void fromRotation(U x, U y, U z, Angle::RADIANS<U> angle);
    template<typename U>
    void fromXRotation(Angle::RADIANS<U> angle);
    template<typename U>
    void fromYRotation(Angle::RADIANS<U> angle);
    template<typename U>
    void fromZRotation(Angle::RADIANS<U> angle);

    // setScale replaces the main diagonal!
    template<typename U>
    void setScale(U x, U y, U z) noexcept;
    template<typename U>
    void setScale(const vec3<U> &v) noexcept;

    [[nodiscard]] vec3<T> getScale() const noexcept;
    [[nodiscard]] vec3<T> getScaleSq() const noexcept;

    /// Alias for getCol(0)
    [[nodiscard]] vec3<T> getRightVec( ) const noexcept;
    /// Alias for getCol(1)
    [[nodiscard]] vec3<T> getUpVec() const noexcept;
    /// Alias for -getCol(2). Assumes -Z fwd
    [[nodiscard]] vec3<T> getForwardVec() const noexcept;

    /// Returns normalized(getRightVec())
    [[nodiscard]] vec3<T> getRightDirection( ) const noexcept;
    /// Returns normalized(getUpVec())
    [[nodiscard]] vec3<T> getUpDirection() const noexcept;
    /// Returns normalized(getForwardVec())
    [[nodiscard]] vec3<T> getForwardDirection() const noexcept;

    void orthoNormalize();

    union {
        struct {
            T _11, _12, _13;  // standard names for components
            T _21, _22, _23;  // standard names for components
            T _31, _32, _33;  // standard names for components
        };
        T mat[9];
        T m[3][3];
        vec3<T> _vec[3];
    };
};

/***************
 * mat4
 ***************/
#pragma pack(push)
#pragma pack(1)
template <typename T>
class mat4 {
    static_assert(ValidMathType<T>, "Invalid base type!");

    // m0  m1  m2  m3
    // m4  m5  m6  m7
    // m8  m9  m10 m11
    // m12 m13 m14 m15
   public:
    mat4() noexcept;
    mat4(std::initializer_list<T> matrix) noexcept;
    template<typename U>
    explicit mat4(U value) noexcept;
    template<typename U>
    explicit mat4(const U *values) noexcept;
    template<typename U>
    explicit mat4(const mat2<U> &B, bool zeroFill) noexcept;
    template<typename U>
    explicit mat4(const mat3<U> &B, bool zeroFill) noexcept;
    mat4(const mat4 &B) noexcept;
    template<typename U>
    explicit mat4(const mat4<U> &B) noexcept;
    template<typename U>
    explicit mat4(const vec3<U> &translation, const vec3<U> &scale) noexcept;
    template<typename U>
    explicit mat4(const vec3<U> &translation, const vec3<U> &scale, const mat3<U>& rotation) noexcept;
    template<typename U>
    explicit mat4(const vec3<U> &translation) noexcept;
    template<typename U>
    explicit mat4(U translationX, U translationY, U translationZ) noexcept;
    template<typename U>
    explicit mat4(const vec3<U> &axis, Angle::RADIANS<U> angle) noexcept;
    template<typename U>
    explicit mat4(U x, U y, U z, Angle::RADIANS<U> angle) noexcept;
    template<typename U>
    explicit mat4(const vec3<U> &eye, const vec3<U> &target, const vec3<U> &up) noexcept;
    template<typename U>
    explicit mat4(const Rect<U> &orthoRect, vec2<U> clip) noexcept;
    template<typename U>
    explicit mat4(const Plane<U>& reflectionPlane) noexcept;

    template<typename U>
    [[nodiscard]] vec2<U> operator*(const vec2<U>  v) const noexcept;
    template<typename U>
    [[nodiscard]] vec3<U> operator*(const vec3<U> &v) const noexcept;
    template<typename U>
    [[nodiscard]] vec4<U> operator*(const vec4<U> &v) const noexcept;

    template<typename U>
    [[nodiscard]] mat4 operator*(const mat4<U>& matrix) const noexcept;
    template<typename U>
    [[nodiscard]] mat4 operator/(const mat4<U>& matrix) const noexcept;
    template<typename U>
    [[nodiscard]] mat4 operator+(const mat4<U> &matrix) const noexcept;
    template<typename U>
    [[nodiscard]] mat4 operator-(const mat4<U> &matrix) const noexcept;

    template<typename U>
    mat4 &operator*=(const mat4<U> &matrix) noexcept;
    template<typename U>
    mat4 &operator/=(const mat4<U> &matrix) noexcept;
    template<typename U>
    mat4 &operator+=(const mat4<U> &matrix) noexcept;
    template<typename U>
    mat4 &operator-=(const mat4<U> &matrix) noexcept;

    template<typename U>
    [[nodiscard]] mat4 operator*(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat4 operator/(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat4 operator+(U f) const noexcept;
    template<typename U>
    [[nodiscard]] mat4 operator-(U f) const noexcept;

    template<typename U>
    mat4 &operator*=(U f) noexcept;
    template<typename U>
    mat4 &operator/=(U f) noexcept;
    template<typename U>
    mat4 &operator+=(U f) noexcept;
    template<typename U>
    mat4 &operator-=(U f) noexcept;

    [[nodiscard]] bool operator==(const mat4& B) const noexcept;
    [[nodiscard]] bool operator!=(const mat4 &B) const noexcept;
    template<typename U>
    [[nodiscard]] bool operator==(const mat4<U>& B) const noexcept;
    template<typename U>
    [[nodiscard]] bool operator!=(const mat4<U> &B) const noexcept;

    [[nodiscard]] bool compare(const mat4 &B, F32 epsilon) const noexcept;
    template<typename U>
    [[nodiscard]] bool compare(const mat4<U> &B, F32 epsilon) const noexcept;

    [[nodiscard]] operator T *() noexcept;
    [[nodiscard]] operator const T *() const noexcept;

    [[nodiscard]] T &operator[](I32 i) noexcept;
    [[nodiscard]] const T &operator[](I32 i) const noexcept;

    [[nodiscard]] T &element(I8 row, I8 column) noexcept;
    [[nodiscard]] const T &element(I8 row, I8 column) const noexcept;

    void set(std::initializer_list<T> matrix) noexcept;

    template<typename U>
    void set(U const *matrix) noexcept;
    template<typename U>
    void set(const mat2<U> &matrix) noexcept;
    template<typename U>
    void set(const mat3<U> &matrix) noexcept;
    template<typename U>
    void set(const mat4<U> &matrix) noexcept;
    template<typename U>
    void set(const vec3<U> &translation, const vec3<U> &scale, const mat4<U>& rotation) noexcept;

    template<typename U>
    void setRow(I32 index, U value) noexcept;
    template<typename U>
    void setRow(I32 index, const vec4<U> &value) noexcept;
    template<typename U>
    void setRow(I32 index, U x, U y, U z, U w) noexcept;

    [[nodiscard]] const vec4<T>& getRow(I32 index) const noexcept;

    template<typename U>
    void setCol(I32 index, const vec4<U> &value) noexcept;
    template<typename U>
    void setCol(I32 index, U value) noexcept;
    template<typename U>
    void setCol(I32 index, U x, U y, U z, U w) noexcept;

    [[nodiscard]] vec4<T> getCol(I32 index) const noexcept;

    void zero() noexcept;
    void identity() noexcept;
    [[nodiscard]] bool isIdentity() const noexcept;
    [[nodiscard]] bool isUniformScale(F32 tolerance = 0.0001f) const noexcept;
    [[nodiscard]] bool isColOrthogonal() const noexcept;
    void swap(mat4 &B) noexcept;

    [[nodiscard]] T det() const noexcept;
    [[nodiscard]] T elementSum() const noexcept;
    void orthoNormalize() noexcept;
    void inverse() noexcept;
    void transpose() noexcept;
    void inverseTranspose() noexcept;
    [[nodiscard]] mat4 transposeRotation() const noexcept;

    [[nodiscard]] mat4 getInverse() const noexcept;
    void getInverse(mat4 &ret) const noexcept;

    [[nodiscard]] mat4 getTranspose() const noexcept;
    void getTranspose(mat4 &out) const noexcept;

    [[nodiscard]] mat4 getInverseTranspose() const noexcept;
    void getInverseTranspose(mat4 &ret) const noexcept;

    [[nodiscard]] mat4 getTransposeRotation() const noexcept;
    void getTransposeRotation(mat4 &ret) const noexcept;

    template<typename U>
    void fromRotation(U x, U y, U z, Angle::RADIANS<U> angle) noexcept;
    template<typename U>
    void fromXRotation(Angle::RADIANS<U> angle) noexcept;
    template<typename U>
    void fromYRotation(Angle::RADIANS<U> angle) noexcept;
    template<typename U>
    void fromZRotation(Angle::RADIANS<U> angle) noexcept;

    template<typename U>
    void setTranslation(const vec3<U> &v) noexcept;
    template<typename U>
    void setTranslation(U x, U y, U z) noexcept;

    template<typename U>
    void setScale(U x, U y, U z) noexcept;
    template<typename U>
    void setScale(const vec3<U> &v) noexcept;

    [[nodiscard]] vec3<T> getScale() const noexcept;
    [[nodiscard]] vec3<T> getScaleSq() const noexcept;

    /// Alias for getCol(0)
    [[nodiscard]] vec3<T> getRightVec( ) const noexcept;
    /// Alias for getCol(1)
    [[nodiscard]] vec3<T> getUpVec( ) const noexcept;
    /// Alias for -getCol(2). Assumes -Z fwd
    [[nodiscard]] vec3<T> getForwardVec( ) const noexcept;

    /// Returns normalized(getRightVec())
    [[nodiscard]] vec3<T> getRightDirection( ) const noexcept;
    /// Returns normalized(getUpVec())
    [[nodiscard]] vec3<T> getUpDirection( ) const noexcept;
    /// Returns normalized(getForwardVec())
    [[nodiscard]] vec3<T> getForwardDirection( ) const noexcept;

    template<typename U>
    [[nodiscard]] vec3<U> transform(const vec3<U> &v, bool homogeneous) const;
    template<typename U>
    [[nodiscard]] vec3<U> transformHomogeneous(const vec3<U> &v) const;
    template<typename U>
    [[nodiscard]] vec3<U> transformNonHomogeneous(const vec3<U> &v) const noexcept;
       
    template<typename U>
    void translate(const vec3<U> &v) noexcept;
    template<typename U>
    void translate(U x, U y, U z) noexcept;

    template<typename U>
    void scale(const vec3<U> &v) noexcept;
    template<typename U>
    void scale(U x, U y, U z) noexcept;

    [[nodiscard]] vec3<T> getTranslation() const noexcept;
    [[nodiscard]] mat4 getRotation() const;

    template<typename U>
    const mat4& reflect(U x, U y, U z, U w) noexcept;
    template<typename U>
    const mat4& reflect(const Plane<U> &plane) noexcept;

    template<typename U>
    void lookAt(const vec3<U> &eye, const vec3<U> &target, const vec3<U> &up) noexcept;
    template<typename U>
    void ortho(U left, U right, U bottom, U top, U zNear, U zFar) noexcept;
    template<typename U>
    void perspective(Angle::DEGREES<U> fovyRad, U aspect, U zNear, U zFar) noexcept;
    template<typename U>
    void frustum(U left, U right, U bottom, U top, U nearVal, U farVal) noexcept;

    template<typename U>
    void extractMat3(mat3<U> &matrix3) const noexcept;

    static mat4<T> Multiply(const mat4<T>& matrixA, const mat4<T>& matrixB) noexcept;

    static void Multiply(const mat4<T>& matrixA, const mat4<T>& matrixB, mat4<T>& ret) noexcept;

    // Copyright 2011 The Closure Library Authors. All Rights Reserved.
    static void Inverse(const T* in, T* out) noexcept;

    union {
        struct {
            T _11, _12, _13, _14;
            T _21, _22, _23, _24;
            T _31, _32, _33, _34;
            T _41, _42, _43, _44;
        };
        T mat[16];
        T m[4][4];
        vec4<T> _vec[4];
        SimdVector<T> _reg[4];
    };
};
#pragma pack(pop)

static const mat2<F32> MAT2_BIAS{ 0.5, 0.0,
                                  0.0, 0.5 };
static const mat3<F32> MAT3_BIAS{ 0.5, 0.0, 0.0,
                                  0.0, 0.5, 0.0,
                                  0.0, 0.0, 0.5 };
static const mat4<F32> MAT4_BIAS{ 0.5, 0.0, 0.0, 0.0,
                                  0.0, 0.5, 0.0, 0.0,
                                  0.0, 0.0, 0.5, 0.0,
                                  0.5, 0.5, 0.5, 1.0 };

static const mat2<F32> MAT2_ZERO{ 0.0f };
static const mat3<F32> MAT3_ZERO{ 0.0f };
static const mat4<F32> MAT4_ZERO{ 0.0f };

static const mat2<F32> MAT2_IDENTITY{};
static const mat3<F32> MAT3_IDENTITY{};
static const mat4<F32> MAT4_IDENTITY{};

//MAT4_INITIAL_TRANSFORM is a special transform matrix that has the Y position and all of the scale axis set to a really low values
//This avoids object popping up at (0,0,0) with whatever scale they were exported at while loading for MAX_FRAMES_IN_FLIGHT - 1u frames.
static const mat4<F32> MAT4_INITIAL_TRANSFORM {
    vec3<F32>(0.f, -65535.f, 0.f),
    vec3<F32>(1.f / U8_MAX)
};

}  // namespace Divide

#endif //_MATH_MATRICES_H_

#include "MathMatrices.inl"