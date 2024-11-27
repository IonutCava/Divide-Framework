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
 * Author: Scott Lee
*/

/* Copyright (c) 2018 DIVIDE-Studio
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
#ifndef DVD_MATH_VECTORS_H_
#define DVD_MATH_VECTORS_H_

#include "MathHelper.h"

namespace Divide
{
#if defined(HAS_SSE42)
    namespace SSE
    {
        bool Fneq128( __m128 a, __m128 b ) noexcept;
        bool Fneq128( __m128 a, __m128 b, F32 epsilon ) noexcept;
    }; // namespace SSE
#endif //HAS_SSE42

    template<typename T, typename = void>
    struct SimdVector
    {
        public:
        SimdVector()  noexcept : SimdVector( 0 )
        {
        }
        explicit SimdVector( T reg0, T reg1, T reg2, T reg3 )  noexcept : _reg{ reg0, reg1, reg2, reg3 }
        {
        }
        explicit SimdVector( T val ) noexcept : _reg{ val, val, val, val }
        {
        }
        explicit SimdVector( T reg[4] )  noexcept : _reg( reg )
        {
        }

        T _reg[4];

        bool operator==( const SimdVector& ) const = default;
    };

#if defined(HAS_SSE42)
    template<ValidMathType T>
    struct alignas(16) SimdVector<T, std::enable_if_t<Is_Float_Angle<T> || std::is_same_v<F32, T>>>
    {
        public:
        SimdVector() noexcept : SimdVector( 0.f )
        {
        }
        explicit SimdVector( F32 reg0, F32 reg1, F32 reg2, F32 reg3 )  noexcept : _reg( _mm_set_ps( reg3, reg2, reg1, reg0 ) )
        {
        }
        explicit SimdVector( F32 reg )  noexcept : _reg( _mm_set_ps( reg, reg, reg, reg ) )
        {
        }
        explicit SimdVector( F32 reg[4] )  noexcept : _reg( _mm_set_ps( reg[3], reg[2], reg[1], reg[0] ) )
        {
        }
        explicit SimdVector( const __m128 reg )  noexcept : _reg( reg )
        {
        }

        bool operator==( const SimdVector& other ) const noexcept
        {
            return !SSE::Fneq128( _reg, other._reg, EPSILON_F32 );
        }

        bool operator!=( const SimdVector& other ) const noexcept
        {
            return SSE::Fneq128( _reg, other._reg, EPSILON_F32 );
        }

        __m128 _reg;
    };
#endif //HAS_SSE42

    /***********************************************************************
     * vec2 -  A 2-tuple used to represent things like a vector in 2D space,
     * a point in 2D space or just 2 values linked together
     ***********************************************************************/
    template <typename T>
    class vec2
    {
        static_assert(ValidMathType<T>, "Invalid base type!");

        public:
        vec2() noexcept : vec2( T{0} ) {}

        vec2( T value ) noexcept : vec2( value, value )
        {
        }
        template<ValidMathType U>
        vec2( U value ) noexcept : vec2( value, value )
        {
        }
        vec2( T xIn, T yIn ) noexcept : x( xIn ), y( yIn )
        {
        }
        template <ValidMathType U>
        vec2( U xIn, U yIn ) noexcept : x( static_cast<T>(xIn) ), y( static_cast<T>(yIn) )
        {
        }
        template <ValidMathType U, ValidMathType V>
        vec2( U xIn, V yIn ) noexcept : x( static_cast<T>(xIn) ), y( static_cast<T>(yIn) )
        {
        }
        vec2( const T* _v ) noexcept : vec2( _v[0], _v[1] )
        {
        }
        vec2( const vec3<T>& _v ) noexcept : vec2( _v.xy() )
        {
        }
        vec2( const vec4<T>& _v ) noexcept : vec2( _v.xy() )
        {
        }
        template<ValidMathType U>
        vec2( const vec2<U>& v ) noexcept : vec2( v.x, v.y )
        {
        }
        template<ValidMathType U>
        vec2( const vec3<U>& v ) noexcept : vec2( v.x, v.y )
        {
        }
        template<ValidMathType U>
        vec2( const vec4<U>& v ) noexcept : vec2( v.x, v.y )
        {
        }

        bool operator> ( const vec2& v ) const noexcept
        {
            return x > v.x && y > v.y;
        }
        bool operator< ( const vec2& v ) const noexcept
        {
            return x < v.x&& y < v.y;
        }
        bool operator<=( const vec2& v ) const noexcept
        {
            return *this < v || *this == v;
        }
        bool operator>=( const vec2& v ) const noexcept
        {
            return *this > v || *this == v;
        }
        bool operator==( const vec2& v ) const noexcept
        {
            return this->compare( v );
        }
        bool operator!=( const vec2& v ) const noexcept
        {
            return !this->compare( v );
        }
        template<ValidMathType U>
        bool operator!=( const vec2<U>& v ) const noexcept
        {
            return !this->compare( v );
        }
        template<ValidMathType U>
        bool operator==( const vec2<U>& v ) const noexcept
        {
            return this->compare( v );
        }

        template<ValidMathType U>
        [[nodiscard]] vec2 operator-( U _f ) const noexcept
        {
            return vec2( this->x - _f, this->y - _f );
        }
        template<ValidMathType U>
        [[nodiscard]] vec2 operator+( U _f ) const noexcept
        {
            return vec2( this->x + _f, this->y + _f );
        }
        template<ValidMathType U>
        [[nodiscard]] vec2 operator*( U _f ) const noexcept
        {
            return vec2( this->x * _f, this->y * _f );
        }
        template<ValidMathType U>
        [[nodiscard]] vec2 operator/( U _i ) const noexcept
        {
            if ( !IS_ZERO( _i ) )
            {
                return vec2( this->x / _i, this->y / _i );
            } return *this;
        }

        template<ValidMathType U>
        [[nodiscard]] vec2 operator+( const vec2<U> v ) const noexcept
        {
            return vec2( this->x + v.x, this->y + v.y );
        }
        template<ValidMathType U>
        [[nodiscard]] vec2 operator-( const vec2<U> v ) const noexcept
        {
            return vec2( this->x - v.x, this->y - v.y );
        }
        template<ValidMathType U>
        [[nodiscard]] vec2 operator*( const vec2<U> v ) const noexcept
        {
            return vec2( this->x * v.x, this->y * v.y );
        }

        [[nodiscard]] vec2 operator-() const noexcept
        {
            return vec2( -this->x, -this->y );
        }

        template<ValidMathType U>
        vec2& operator+=( U _f ) noexcept
        {
            this->set( *this + _f ); return *this;
        }
        template<ValidMathType U>
        vec2& operator-=( U _f ) noexcept
        {
            this->set( *this - _f ); return *this;
        }
        template<ValidMathType U>
        vec2& operator*=( U _f ) noexcept
        {
            this->set( *this * _f ); return *this;
        }
        template<ValidMathType U>
        vec2& operator/=( U _f ) noexcept
        {
            this->set( *this / _f ); return *this;
        }

        template<ValidMathType U>
        vec2& operator*=( const vec2<U> v ) noexcept
        {
            this->set( *this * v ); return *this;
        }
        template<ValidMathType U>
        vec2& operator+=( const vec2<U> v ) noexcept
        {
            this->set( *this + v ); return *this;
        }
        template<ValidMathType U>
        vec2& operator-=( const vec2<U> v ) noexcept
        {
            this->set( *this - v ); return *this;
        }
        template<ValidMathType U>
        vec2& operator/=( const vec2<U> v ) noexcept
        {
            this->set( *this / v ); return *this;
        }

        template<std::unsigned_integral U>
        [[nodiscard]] T& operator[]( U i )       noexcept
        {
            return this->_v[i];
        }
        template<std::unsigned_integral U>
        [[nodiscard]] const T& operator[]( U i ) const noexcept
        {
            return this->_v[i];
        }

        [[nodiscard]] vec2 operator/( const vec2& v ) const noexcept
        {
            return vec2( IS_ZERO( v.x ) ? this->x : this->x / v.x,
                         IS_ZERO( v.y ) ? this->y : this->y / v.y );
        }

        [[nodiscard]] operator T* ()       noexcept
        {
            return this->_v;
        }
        [[nodiscard]] operator const T* () const noexcept
        {
            return this->_v;
        }

        /// swap the components  of this vector with that of the specified one
        void swap( vec2* iv ) noexcept
        {
            std::swap( this->x, iv->x ); std::swap( this->y, iv->y );
        }
        /// swap the components  of this vector with that of the specified one
        void swap( vec2& iv ) noexcept
        {
            std::swap( this->x, iv.x ); std::swap( this->y, iv.y );
        }
        /// set the 2 components of the vector manually using a source pointer to a (large enough) array
        void set( const T* v ) noexcept
        {
            std::memcpy( &_v[0], &v[0], sizeof( T ) * 2 );
        }
        /// set the 2 components of the vector manually
        void set( T value ) noexcept
        {
            this->set( value, value );
        }
        /// set the 2 components of the vector manually
        void set( T xIn, T yIn ) noexcept
        {
            this->x = xIn; this->y = yIn;
        }
        template<ValidMathType U>
        void set( U xIn, U yIn ) noexcept
        {
            this->x = static_cast<T>(xIn); this->y = static_cast<T>(yIn);
        }
        /// set the 2 components of the vector using a source vector
        void set( const vec2& v ) noexcept
        {
            this->set( &v._v[0] );
        }
        /// set the 2 components of the vector using the first 2 components of the source vector
        void set( const vec3<T>& v ) noexcept
        {
            this->set( v.x, v.y );
        }
        /// set the 2 components of the vector using the first 2 components of the source vector
        void set( const vec4<T>& v ) noexcept
        {
            this->set( v.x, v.y );
        }
        /// set the 2 components of the vector back to 0
        void reset()
        {
            this->set( T{0} );
        }
        /// return the vector's length
        [[nodiscard]] T length()        const noexcept
        {
            return Sqrt<T>( lengthSquared() );
        }
        [[nodiscard]] T lengthSquared() const noexcept;
        /// return the angle defined by the 2 components
        [[nodiscard]] T angle() const
        {
            return static_cast<T>(std::atan2( this->y, this->x ));
        }
        /// return the angle defined by the 2 components
        [[nodiscard]] T angle( const vec2& v ) const
        {
            return static_cast<T>(std::atan2( v.y - this->y, v.x - this->x ));
        }
        /// compute the vector's distance to another specified vector
        [[nodiscard]] T distance( const vec2& v ) const;
        /// compute the vector's squared distance to another specified vector
        [[nodiscard]] T distanceSquared( const vec2& v ) const noexcept;
        /// convert the vector to unit length
        [[nodiscard]] vec2& normalize() noexcept;
        /// get the smallest value of X or Y
        [[nodiscard]] T minComponent() const noexcept;
        /// get the largest value of X or Y
        [[nodiscard]] T maxComponent() const noexcept;
        /// round both values
        void round();
        /// lerp between this and the specified vector by the specified amount
        void lerp( const vec2& v, T factor ) noexcept;
        /// lerp between this and the specified vector by the specified amount for each component
        void lerp( const vec2& v, const vec2& factor ) noexcept;
        /// calculate the dot product between this vector and the specified one
        [[nodiscard]] T dot( const vec2& v ) const noexcept;
        /// project this vector on the line defined by the 2 points(A, B)
        [[nodiscard]] T projectionOnLine( const vec2& vA, const vec2& vB ) const;
        /// return the closest point on the line defined by the 2 points (A, B) and this vector
        [[nodiscard]] vec2 closestPointOnLine( const vec2& vA, const vec2& vB );
        /// return the closest point on the line segment defined between the 2 points (A, B) and this vector
        [[nodiscard]] vec2 closestPointOnSegment( const vec2& vA, const vec2& vB );
        /// compare 2 vectors
        template<ValidMathType U>
        [[nodiscard]] bool compare( vec2<U> v ) const noexcept;
        /// compare 2 vectors within the specified tolerance
        template<ValidMathType U>
        [[nodiscard]] bool compare( vec2<U> v, U epsi ) const noexcept;
        /// export the vector's components in the first 2 positions of the specified array
        [[nodiscard]] void get( T* v ) const;

        union
        {
            struct
            {
                T x, y;
            };
            struct
            {
                T s, t;
            };
            struct
            {
                T width, height;
            };
            struct
            {
                T min, max;
            };
            struct
            {
                T offset, count;
            };

            T _v[2] = {T{0}, T{0} };
        };
    };

    /// lerp between the 2 specified vectors by the specified amount
    template <typename T, ValidMathType U>
    [[nodiscard]] vec2<T> Lerp( vec2<T> u, vec2<T> v, U factor ) noexcept;
    /// lerp between the 2 specified vectors by the specified amount for each component
    template <typename T>
    [[nodiscard]] vec2<T> Lerp( vec2<T> u, vec2<T> v, vec2<T> factor ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec2<T> Cross( vec2<T> v1, vec2<T> v2 ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec2<T> Inverse( vec2<T> v ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec2<T> Normalize( vec2<T>& vector ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec2<T> Normalized( vec2<T> vector ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] T Dot( vec2<T> a, vec2<T> b ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] void OrthoNormalize( vec2<T>& n, vec2<T>& u ) noexcept;
    /// multiply a vector by a value
    template <ValidMathType T>
    [[nodiscard]] vec2<T> operator*( T fl, vec2<T> v ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec2<T> Clamped( vec2<T> v, vec2<T> min, vec2<T> max ) noexcept;

    /***********************************************************************
     * vec3 -  A 3-tuple used to represent things like a vector in 3D space,
     * a point in 3D space or just 3 values linked together
     ***********************************************************************/
    template <typename T>
    class vec3
    {
        static_assert(ValidMathType<T>, "Invalid base type!");

        public:
        vec3() noexcept : vec3( T{0} ) {}

        vec3( T value ) noexcept : vec3( value, value, value )
        {
        }
        template<ValidMathType U>
        vec3( U value ) noexcept : vec3( value, value, value )
        {
        }
        vec3( T xIn, T yIn, T zIn ) noexcept : x( xIn ), y( yIn ), z( zIn )
        {
        }
        template <ValidMathType U> 
        vec3( U xIn, U yIn, U zIn )  noexcept : x( static_cast<T>(xIn) ), y( static_cast<T>(yIn) ), z( static_cast<T>(zIn) )
        {
        }
        template <ValidMathType U, ValidMathType V>
        vec3( U xIn, U yIn, V zIn ) noexcept : x( static_cast<T>(xIn) ), y( static_cast<T>(yIn) ), z( static_cast<T>(zIn) )
        {
        }
        template <ValidMathType U, ValidMathType V>
        vec3( U xIn, V yIn, V zIn ) noexcept : x( static_cast<T>(xIn) ), y( static_cast<T>(yIn) ), z( static_cast<T>(zIn) )
        {
        }
        template <ValidMathType U, ValidMathType V, ValidMathType W>
        vec3( U xIn, V yIn, W zIn ) noexcept : x( static_cast<T>(xIn) ), y( static_cast<T>(yIn) ), z( static_cast<T>(zIn) )
        {
        }
        vec3( const T* v ) noexcept : vec3( v[0], v[1], v[2] )
        {
        }
        vec3( const vec2<T> v ) noexcept : vec3( v, T{0} )
        {
        }
        vec3( const vec2<T> v, T zIn ) noexcept : vec3( v.x, v.y, zIn )
        {
        }
        vec3( const vec4<T>& v ) noexcept : vec3( v.x, v.y, v.z )
        {
        }
        template<ValidMathType U>
        vec3( const vec2<U> v ) noexcept : vec3( v.x, v.y, U{0} )
        {
        }
        template<ValidMathType U>
        vec3( const vec3<U>& v ) noexcept : vec3( v.x, v.y, v.z )
        {
        }
        template<ValidMathType U>
        vec3( const vec4<U>& v ) noexcept : vec3( v.x, v.y, v.z )
        {
        }

        [[nodiscard]] bool operator> ( const vec3& v ) const noexcept
        {
            return x > v.x && y > v.y && z > v.z;
        }
        [[nodiscard]] bool operator< ( const vec3& v ) const noexcept
        {
            return x < v.x&& y < v.y&& z < v.z;
        }
        [[nodiscard]] bool operator<=( const vec3& v ) const noexcept
        {
            return *this < v || *this == v;
        }
        [[nodiscard]] bool operator>=( const vec3& v ) const noexcept
        {
            return *this > v || *this == v;
        }
        [[nodiscard]] bool operator!=( const vec3& v ) const noexcept
        {
            return !this->compare( v );
        }
        [[nodiscard]] bool operator==( const vec3& v ) const noexcept
        {
            return this->compare( v );
        }
        template<ValidMathType U>
        [[nodiscard]] bool operator!=( const vec3<U>& v ) const noexcept
        {
            return !this->compare( v );
        }
        template<ValidMathType U>
        [[nodiscard]] bool operator==( const vec3<U>& v ) const noexcept
        {
            return this->compare( v );
        }

        template <ValidMathType U>
        [[nodiscard]] vec3 operator+( U _f ) const noexcept
        {
            return vec3( this->x + _f, this->y + _f, this->z + _f );
        }
        template <ValidMathType U>
        [[nodiscard]] vec3 operator-( U _f ) const noexcept
        {
            return vec3( this->x - _f, this->y - _f, this->z - _f );
        }
        template <ValidMathType U>
        [[nodiscard]] vec3 operator*( U _f ) const noexcept
        {
            return vec3( this->x * _f, this->y * _f, this->z * _f );
        }
        template <ValidMathType U>
        [[nodiscard]] vec3 operator/( U _f ) const noexcept
        {
            if ( IS_ZERO( _f ) )
            {
                return *this;
            }
            return vec3( this->x / _f, this->y / _f, this->z / _f );
        }

        [[nodiscard]] vec3 operator-() const noexcept
        {
            return vec3( -this->x, -this->y, -this->z );
        }

        template <ValidMathType U>
        [[nodiscard]] vec3 operator+( const vec3<U>& v ) const noexcept
        {
            return vec3( this->x + v.x, this->y + v.y, this->z + v.z );
        }
        template<ValidMathType U>
        [[nodiscard]] vec3 operator-( const vec3<U>& v ) const noexcept
        {
            return vec3( this->x - v.x, this->y - v.y, this->z - v.z );
        }
        template<ValidMathType U>
        [[nodiscard]] vec3 operator*( const vec3<U>& v ) const noexcept
        {
            return vec3( this->x * v.x, this->y * v.y, this->z * v.z );
        }

        template<ValidMathType U>
        vec3& operator+=( U _f ) noexcept
        {
            this->set( *this + _f ); return *this;
        }
        template<ValidMathType U>
        vec3& operator-=( U _f ) noexcept
        {
            this->set( *this - _f ); return *this;
        }
        template<ValidMathType U>
        vec3& operator*=( U _f ) noexcept
        {
            this->set( *this * _f ); return *this;
        }
        template<ValidMathType U>
        vec3& operator/=( U _f ) noexcept
        {
            this->set( *this / _f ); return *this;
        }

        template<ValidMathType U>
        vec3& operator*=( const vec3<U>& v ) noexcept
        {
            this->set( *this * v ); return *this;
        }
        template<ValidMathType U>
        vec3& operator+=( const vec3<U>& v ) noexcept
        {
            this->set( *this + v ); return *this;
        }
        template<ValidMathType U>
        vec3& operator-=( const vec3<U>& v ) noexcept
        {
            this->set( *this - v ); return *this;
        }
        template<ValidMathType U>
        vec3& operator/=( const vec3<U>& v ) noexcept
        {
            this->set( *this / v ); return *this;
        }

        template<std::unsigned_integral U>
        [[nodiscard]] T& operator[]( const U i ) noexcept
        {
            return this->_v[i];
        }
        template<std::unsigned_integral U>
        [[nodiscard]] const T& operator[]( const U i ) const noexcept
        {
            return this->_v[i];
        }

        template<ValidMathType U>
        [[nodiscard]] vec3 operator/( const vec3<U>& v ) const noexcept
        {
            return vec3( IS_ZERO( v.x ) ? this->x : this->x / v.x,
                         IS_ZERO( v.y ) ? this->y : this->y / v.y,
                         IS_ZERO( v.z ) ? this->z : this->z / v.z );
        }

        [[nodiscard]] operator T* ()  noexcept
        {
            return this->_v;
        }
        [[nodiscard]] operator const T* () const  noexcept
        {
            return this->_v;
        }

        /// GLSL like accessors (const to prevent erroneous usage like .xy() += n)
        [[nodiscard]] vec2<T> rb() const noexcept
        {
            return vec2<T>( this->r, this->b );
        }
        [[nodiscard]] vec2<T> xz() const noexcept
        {
            return this->rb();
        }

        void rb( const vec2<T> rb ) noexcept
        {
            this->r = rb.x; this->b = rb.y;
        }
        void xz( const vec2<T> xz ) noexcept
        {
            this->x = xz.x; this->z = xz.y;
        }

        /// set the 3 components of the vector manually using a source pointer to a (large enough) array
        void set( const T* v ) noexcept
        {
            std::memcpy( &_v[0], &v[0], 3 * sizeof( T ) );
        }
        /// set the 3 components of the vector manually
        void set( T value )  noexcept
        {
            x = value; y = value; z = value;
        }
        /// set the 3 components of the vector manually
        void set( T xIn, T yIn, T zIn ) noexcept
        {
            this->x = xIn; this->y = yIn; this->z = zIn;
        }
        template<ValidMathType U>
        void set( U xIn, U yIn, U zIn ) noexcept
        {
            this->x = static_cast<T>(xIn); this->y = static_cast<T>(yIn); this->z = static_cast<T>(zIn);
        }
        /// set the 3 components of the vector using a smaller source vector
        void set( const vec2<T> v )  noexcept
        {
            std::memcpy( _v, v._v, 2 * sizeof( T ) );
        }
        /// set the 3 components of the vector using a source vector
        void set( const vec3& v )  noexcept
        {
            std::memcpy( _v, v._v, 3 * sizeof( T ) );
        }
        /// set the 3 components of the vector using the first 3 components of the source vector
        void set( const vec4<T>& v )  noexcept
        {
            std::memcpy( _v, v._v, 3 * sizeof( T ) );
        }
        /// set all the components back to 0
        void reset()  noexcept
        {
            std::memset( _v, 0, 3 * sizeof( T ) );
        }
        /// return the vector's length
        [[nodiscard]] T length() const  noexcept
        {
            return Divide::Sqrt<T>( lengthSquared() );
        }
        /// return true if length is zero
        [[nodiscard]] bool isZeroLength() const  noexcept
        {
            return lengthSquared() < EPSILON_F32;
        }
        /// compare 2 vectors
        template<ValidMathType U>
        [[nodiscard]] bool compare( const vec3<U>& v ) const noexcept;
        /// compare 2 vectors within the specified tolerance
        template<ValidMathType U>
        [[nodiscard]] bool compare( const vec3<U>& v, U epsi ) const noexcept;
        /// uniform vector: x = y = z
        [[nodiscard]] bool isUniform( F32 tolerance = 0.0001f ) const noexcept;
        /// The current vector is perpendicular to the specified one within epsilon
        template<ValidMathType U>
        [[nodiscard]] bool isPerpendicular( const vec3<U>& other, F32 epsilon = EPSILON_F32 ) const noexcept;
        /// return the squared distance of the vector
        [[nodiscard]] T lengthSquared() const noexcept;
        /// calculate the dot product between this vector and the specified one
        [[nodiscard]] T dot( const vec3& v ) const noexcept;
        /// returns the angle in radians between '*this' and 'v'
        [[nodiscard]] T angle( vec3& v ) const;
        /// compute the vector's distance to another specified vector
        [[nodiscard]] T distance( const vec3& v ) const noexcept;
        /// compute the vector's squared distance to another specified vector
        [[nodiscard]] T distanceSquared( const vec3& v ) const noexcept;
        /// transform the vector to unit length
        vec3& normalize() noexcept;
        /// get the smallest value of X,Y or Z
        [[nodiscard]] T minComponent() const  noexcept;
        /// get the largest value of X,Y or Z
        [[nodiscard]] T maxComponent() const  noexcept;
        /// round all three values
        void round();
        /// project this vector on the line defined by the 2 points(A, B)
        [[nodiscard]] T projectionOnLine( const vec3& vA, const vec3& vB ) const;
        /// return the closest point on the line defined by the 2 points (A, B) and this
        /// vector
        [[nodiscard]] vec3 closestPointOnLine( const vec3& vA, const vec3& vB );
        /// return the closest point on the line segment created between the 2 points
        /// (A, B) and this vector
        [[nodiscard]] vec3 closestPointOnSegment( const vec3& vA, const vec3& vB );
        /// get the direction vector to the specified point
        [[nodiscard]] vec3 direction( const vec3& u ) const noexcept;
        /// project this vector onto the given direction
        [[nodiscard]] vec3 projectToNorm( const vec3<T>& direction ) noexcept;
        /// lerp between this and the specified vector by the specified amount
        void lerp( const vec3& v, T factor ) noexcept;
        /// lerp between this and the specified vector by the specified amount for
        /// each component
        void lerp( const vec3& v, const vec3& factor ) noexcept;
        /// this calculates a vector between the two specified points and returns
        /// the result
        [[nodiscard]] vec3 vector( const vec3& vp1, const vec3& vp2 ) const noexcept;
        /// set this vector to be equal to the cross of the 2 specified vectors
        void cross( const vec3& v1, const vec3& v2 ) noexcept;
        /// set this vector to be equal to the cross between itself and the
        /// specified vector
        void cross( const vec3& v2 ) noexcept;
        /// rotate this vector on the X axis
        void rotateX( D64 radians );
        /// rotate this vector on the Y axis
        void rotateY( D64 radians );
        /// rotate this vector on the Z axis
        void rotateZ( D64 radians );
        /// swap the components  of this vector with that of the specified one
        void swap( vec3& iv ) noexcept;
        /// swap the components  of this vector with that of the specified one
        void swap( vec3* iv ) noexcept;
        /// export the vector's components in the first 3 positions of the specified
        /// array
        void get( T* v ) const noexcept;

        union
        {
            struct
            {
                T x, y, z;
            };
            struct
            {
                T s, t, p;
            };
            struct
            {
                T r, g, b;
            };
            struct
            {
                T pitch, yaw, roll;
            };
            struct
            {
                T turn, move, zoom;
            };
            struct
            {
                T width, height, depth;
            };
            struct
            {
                vec2<T> xy; T _z;
            };
            struct
            {
                vec2<T> rg; T _b;
            };
            struct
            {
                T _r; vec2<T> gb;
            };

            T _v[3] = {T{0}, T{0}, T{0}};
        };
    };

    /// lerp between the 2 specified vectors by the specified amount
    template <typename T, ValidMathType U>
    [[nodiscard]] vec3<T> Lerp( const vec3<T>& u, const vec3<T>& v, U factor ) noexcept;
    /// lerp between the 2 specified vectors by the specified amount for each component
    template <typename T>
    [[nodiscard]] vec3<T> Lerp( const vec3<T>& u, const vec3<T>& v, const vec3<T>& factor ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> Abs( const vec3<T>& vector ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> Min( const vec3<T>& v1, const vec3<T>& v2 ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> Max( const vec3<T>& v1, const vec3<T>& v2 ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> Normalize( vec3<T>& vector ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> Normalized( const vec3<T>& vector ) noexcept;
    /// general vec3 dot product
    template <ValidMathType T>
    [[nodiscard]] T Dot( const vec3<T>& a, const vec3<T>& b ) noexcept;
    /// general vec3 cross function
    template <ValidMathType T>
    [[nodiscard]] vec3<T> Cross( const vec3<T>& v1, const vec3<T>& v2 ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> AreOrthogonal( const vec3<T>& v1, const vec3<T>& v2 ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> Inverse( const vec3<T>& v ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> operator*( T fl, const vec3<T>& v ) noexcept;
    // project 'in' onto the given direction
    template<ValidMathType T>
    [[nodiscard]] vec3<T> ProjectToNorm( const vec3<T>& in, const vec3<T>& direction );
    template <ValidMathType T>
    void OrthoNormalize( vec3<T>& n, vec3<T>& u ) noexcept;
    template <ValidMathType T>
    void OrthoNormalize( vec3<T>& v1, vec3<T>& v2, vec3<T>& v3 ) noexcept;
    template<ValidMathType T>
    [[nodiscard]] vec3<T> Perpendicular( const vec3<T>& v ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec3<T> Clamped( const vec3<T>& v, const vec3<T>& min, const vec3<T>& max ) noexcept;

    /**********************************************************************************************************************
     * vec4 -  A 4-tuple used to represent things like a vector in 4D space (w-component) or just 4 values linked together
     *********************************************************************************************************************/
#pragma pack(push)
#pragma pack(1)
     //__declspec(align(alignment))
    template <typename T>
    class vec4
    {
        static_assert(ValidMathType<T>, "Invalid base type!");

      public:
        vec4() noexcept : vec4( T{0} ) {}

        vec4( T xIn, T yIn, T zIn, T wIn ) noexcept : x( xIn ), y( yIn ), z( zIn ), w( wIn )
        {
        }
        vec4( T xIn, T yIn, T zIn ) noexcept : x( xIn ), y( yIn ), z( zIn ), w( T{1} )
        {
        }
        template<ValidMathType U>
        vec4( U xIn, U yIn, U zIn, U wIn ) noexcept : x( static_cast<T>(xIn) ), y( static_cast<T>(yIn) ), z( static_cast<T>(zIn) ), w( static_cast<T>(wIn) )
        {
        }
        template<ValidMathType U>
        vec4( U xIn, U yIn, U zIn ) noexcept : vec4( xIn, yIn, zIn, T{1} )
        {
        }
#if defined(HAS_SSE42)
        vec4( const __m128 reg ) noexcept : _reg( reg )
        {
        }
#endif //HAS_SSE42
        vec4( const SimdVector<T>& reg ) noexcept : _reg( reg )
        {
        }
        vec4( T value ) noexcept : vec4( value, value, value, value )
        {
        }
        template<ValidMathType U>
        vec4( U value ) noexcept : vec4( value, value, value, value )
        {
        }
        vec4( const T* v ) noexcept : vec4( v[0], v[1], v[2], v[3] )
        {
        }
        vec4( const vec2<T> v ) noexcept : vec4( v, T{0} )
        {
        }
        vec4( const vec2<T> v, T zIn ) noexcept : vec4( v, zIn, T{1} )
        {
        }
        vec4( const vec2<T> v, T zIn, T wIn ) noexcept : vec4( v.x, v.y, zIn, wIn )
        {
        }
        vec4( const vec3<T>& v ) noexcept : vec4( v, T{1} )
        {
        }
        vec4( const vec3<T>& v, T wIn ) noexcept : vec4( v.x, v.y, v.z, wIn )
        {
        }
        template<ValidMathType U>
        vec4( const vec2<U> v ) noexcept : vec4( v.x, v.y, U{0}, U{0} )
        {
        }
        template<ValidMathType U>
        vec4( const vec3<U>& v ) noexcept : vec4( v.x, v.y, v.z, U{0} )
        {
        }
        template<ValidMathType U>
        vec4( const vec4<U>& v ) noexcept : vec4( v.x, v.y, v.z, v.w )
        {
        }

        [[nodiscard]] bool operator> ( const vec4& v ) const noexcept
        {
            return x > v.x && y > v.y && z > v.z && w > v.w;
        }
        [[nodiscard]] bool operator< ( const vec4& v ) const noexcept
        {
            return x < v.x&& y < v.y&& z < v.z&& w < v.w;
        }
        [[nodiscard]] bool operator<=( const vec4& v ) const noexcept
        {
            return *this < v || *this == v;
        }
        [[nodiscard]] bool operator>=( const vec4& v ) const noexcept
        {
            return *this > v || *this == v;
        }
        [[nodiscard]] bool operator==( const vec4& v ) const noexcept
        {
            return this->compare( v );
        }
        [[nodiscard]] bool operator!=( const vec4& v ) const noexcept
        {
            return !this->compare( v );
        }
        template<ValidMathType U>
        [[nodiscard]] bool operator!=( const vec4<U>& v ) const noexcept
        {
            return !this->compare( v );
        }
        template<ValidMathType U>
        [[nodiscard]] bool operator==( const vec4<U>& v ) const noexcept
        {
            return this->compare( v );
        }
        template<ValidMathType U>
        [[nodiscard]] vec4 operator-( U _f ) const noexcept
        {
            return vec4( this->x - _f, this->y - _f, this->z - _f, this->w - _f );
        }
        template<ValidMathType U>
        [[nodiscard]] vec4 operator+( U _f ) const noexcept
        {
            return vec4( this->x + _f, this->y + _f, this->z + _f, this->w + _f );
        }
        template<ValidMathType U>
        [[nodiscard]] vec4 operator*( U _f ) const noexcept
        {
            return vec4( this->x * _f, this->y * _f, this->z * _f, this->w * _f );
        }
        template<ValidMathType U>
        [[nodiscard]] vec4 operator/( U _f ) const noexcept
        {
            if ( !IS_ZERO( _f ) )
            {
                return vec4( static_cast<T>(this->x / _f),
                             static_cast<T>(this->y / _f),
                             static_cast<T>(this->z / _f),
                             static_cast<T>(this->w / _f) );
            }

            return *this;
        }

        [[nodiscard]] vec4 operator-() const noexcept
        {
            return vec4( -x, -y, -z, -w );
        }

        template<ValidMathType U>
        [[nodiscard]] vec4 operator+( const vec4<U>& v ) const noexcept
        {
            return vec4( this->x + v.x, this->y + v.y, this->z + v.z, this->w + v.w );
        }
        template<ValidMathType U>
        [[nodiscard]] vec4 operator-( const vec4<U>& v ) const noexcept
        {
            return vec4( this->x - v.x, this->y - v.y, this->z - v.z, this->w - v.w );
        }
        template<ValidMathType U>
        [[nodiscard]] vec4 operator*( const vec4<U>& v ) const noexcept
        {
            return vec4( this->x * v.x, this->y * v.y, this->z * v.z, this->w * v.w );
        }
        template<ValidMathType U>
        [[nodiscard]] vec4 operator/( const vec4<U>& v ) const noexcept
        {
            return vec4( IS_ZERO( v.x ) ? this->x : this->x / v.x,
                         IS_ZERO( v.y ) ? this->y : this->y / v.y,
                         IS_ZERO( v.z ) ? this->z : this->z / v.z,
                         IS_ZERO( v.w ) ? this->w : this->w / v.w );
        }

        template<ValidMathType U>
        vec4& operator+=( U _f ) noexcept
        {
            this->set( *this + _f );
            return *this;
        }
        template<ValidMathType U>
        vec4& operator-=( U _f ) noexcept
        {
            this->set( *this - _f );
            return *this;
        }
        template<ValidMathType U>
        vec4& operator*=( U _f ) noexcept
        {
            this->set( *this * _f );
            return *this;
        }
        template<ValidMathType U>
        vec4& operator/=( U _f ) noexcept
        {
            if ( !IS_ZERO( _f ) )
            {
                this->set( *this / _f );
            } return *this;
        }

        template<ValidMathType U>
        vec4& operator*=( const vec4<U>& v ) noexcept
        {
            this->set( *this * v ); return *this;
        }
        template<ValidMathType U>
        vec4& operator/=( const vec4<U>& v ) noexcept
        {
            this->set( *this / v ); return *this;
        }
        template<ValidMathType U>
        vec4& operator+=( const vec4<U>& v ) noexcept
        {
            this->set( *this + v ); return *this;
        }
        template<ValidMathType U>
        vec4& operator-=( const vec4<U>& v ) noexcept
        {
            this->set( *this - v ); return *this;
        }

        [[nodiscard]] operator T* ()       noexcept
        {
            return this->_v;
        }
        [[nodiscard]] operator const T* () const noexcept
        {
            return this->_v;
        }

        template<std::unsigned_integral U>
        [[nodiscard]] T& operator[]( U i ) noexcept
        {
            return this->_v[i];
        }
        template<std::unsigned_integral U>
        [[nodiscard]] const T& operator[]( U _i ) const noexcept
        {
            return this->_v[_i];
        }

        /// GLSL like accessors (const to prevent erroneous usage like .xyz() += n)
        [[nodiscard]] vec2<T> rb()  const noexcept
        {
            return vec2<T>( this->r, this->b );
        }
        [[nodiscard]] vec2<T> xz()  const noexcept
        {
            return this->rb();
        }
        [[nodiscard]] vec2<T> ra()  const noexcept
        {
            return vec2<T>( this->r, this->a );
        }
        [[nodiscard]] vec2<T> xw()  const noexcept
        {
            return this->ra();
        }
        [[nodiscard]] vec2<T> ga()  const noexcept
        {
            return vec2<T>( this->g, this->a );
        }
        [[nodiscard]] vec2<T> yw()  const noexcept
        {
            return this->ga();
        }
        [[nodiscard]] vec3<T> bgr() const noexcept
        {
            return vec3<T>( this->b, this->g, this->r );
        }
        [[nodiscard]] vec3<T> zyx() const noexcept
        {
            return this->bgr();
        }
        [[nodiscard]] vec3<T> rga() const noexcept
        {
            return vec3<T>( this->r, this->g, this->a );
        }
        [[nodiscard]] vec3<T> xyw() const noexcept
        {
            return this->rga();
        }

        void rb( const vec2<T> rb )  noexcept
        {
            this->xz( rb );
        }
        void xz( const vec2<T> xz )  noexcept
        {
            this->xz( xz.x, xz.y );
        }
        void xz( T xIn, T zIn ) noexcept
        {
            this->x = xIn; this->z = zIn;
        }
        void ra( const vec2<T> ra )  noexcept
        {
            this->xw( ra );
        }
        void xw( const vec2<T> xw )  noexcept
        {
            this->xw( xw.x, xw.y );
        }
        void xw( T xIn, T wIn ) noexcept
        {
            this->x = xIn; this->w = wIn;
        }
        void ga( const vec2<T> ga )  noexcept
        {
            this->yw( ga );
        }
        void yw( const vec2<T> yw )  noexcept
        {
            this->yw( yw.x, yw.y );
        }
        void yw( T yIn, T wIn ) noexcept
        {
            this->y = yIn; this->w = wIn;
        }
        void bgr( const vec3<T>& bgr ) noexcept
        {
            this->zyx( bgr );
        }
        void zyx( const vec3<T>& zyx ) noexcept
        {
            this->z = zyx.x; this->y = zyx.y; this->x = zyx.z;
        }
        void rga( const vec3<T>& rga ) noexcept
        {
            this->xyw( rga );
        }
        void xyw( const vec3<T>& xyw ) noexcept
        {
            this->xyw( xyw.x, xyw.y, xyw.z );
        }
        void xyw( T xIn, T yIn, T wIn ) noexcept
        {
            xy( xIn, yIn ); this->w = wIn;
        }
        void xzw( T xIn, T zIn, T wIn ) noexcept
        {
            xz( xIn, zIn ); this->w = wIn;
        }

        /// set the 4 components of the vector manually using a source pointer to a (large enough) array
        void set( const T* v ) noexcept
        {
            std::memcpy( _v, v, 4 * sizeof( T ) );
        }
        /// set the 4 components of the vector manually
        void set( T value ) noexcept
        {
            _reg = SimdVector<T>( value );
        }
        /// set the 4 components of the vector manually
        void set( T xIn, T yIn, T zIn, T wIn ) noexcept
        {
            _reg = SimdVector<T>( xIn, yIn, zIn, wIn );
        }
        template<ValidMathType U>
        void set( U xIn, U yIn, U zIn, U wIn ) noexcept
        {
            set( static_cast<T>(xIn), static_cast<T>(yIn), static_cast<T>(zIn), static_cast<T>(wIn) );
        }
        /// set the 4 components of the vector using a source vector
        void set( const vec4& v ) noexcept
        {
            _reg = v._reg;
        }
        /// set the 4 components of the vector using a smaller source vector
        void set( const vec3<T>& v ) noexcept
        {
            std::memcpy( &_v[0], &v._v[0], 3 * sizeof( T ) );
        }
        /// set the 4 components of the vector using a smaller source vector
        void set( const vec3<T>& v, T wIn ) noexcept
        {
            std::memcpy( &_v[0], &v._v[0], 3 * sizeof( T ) ); w = wIn;
        }
        /// set the 4 components of the vector using a smaller source vector
        void set( const vec2<T> v ) noexcept
        {
            std::memcpy( &_v[0], &v._v[0], 2 * sizeof( T ) );
        }
        /// set the 4 components of the vector using smaller source vectors
        void set( const vec2<T> v1, const vec2<T> v2 ) noexcept
        {
            this->set( v1.x, v1.y, v2.x, v2.y );
        }
        /// set all the components back to 0
        void reset() noexcept
        {
            this->set( T{0} );
        }
        /// compare 2 vectors
        template<ValidMathType U>
        [[nodiscard]] bool compare( const vec4<U>& v ) const noexcept;
        /// compare 2 vectors within the specified tolerance
        template<ValidMathType U>
        [[nodiscard]] bool compare( const vec4<U>& v, U epsi ) const noexcept;
        /// swap the components  of this vector with that of the specified one
        void swap( vec4* iv ) noexcept;
        /// swap the components  of this vector with that of the specified one
        void swap( vec4& iv ) noexcept;
        /// transform the vector to unit length
        vec4& normalize() noexcept;
        /// The current vector is perpendicular to the specified one within epsilon
        template<ValidMathType U>
        [[nodiscard]] bool isPerpendicular( const vec4<U>& other, F32 epsilon = EPSILON_F32 ) const noexcept;
        /// get the smallest value of X,Y,Z or W
        [[nodiscard]] T minComponent() const noexcept;
        /// get the largest value of X,Y,Z or W
        [[nodiscard]] T maxComponent() const noexcept;
        /// calculate the dot product between this vector and the specified one
        [[nodiscard]] T dot( const vec4& v ) const noexcept;
        /// return the vector's length
        [[nodiscard]] T length() const  noexcept
        {
            return Sqrt( lengthSquared() );
        }
        /// return the squared distance of the vector
        [[nodiscard]] T lengthSquared() const noexcept;
        /// project this vector onto the given direction
        [[nodiscard]] vec4 projectToNorm( const vec4<T>& direction );
        /// round all four values
        void round() noexcept;
        /// lerp between this and the specified vector by the specified amount
        void lerp( const vec4& v, T factor ) noexcept;
        /// lerp between this and the specified vector by the specified amount for each component
        void lerp( const vec4& v, const vec4& factor ) noexcept;
        union
        {
            struct
            {
                T x, y, z, w;
            };
            struct
            {
                T s, t, p, q;
            };
            struct
            {
                T r, g, b, a;
            };
            struct
            {
                T pitch, yaw, roll, _pad0;
            };
            struct
            {
                T turn, move, zoom, _pad1;
            };
            struct
            {
                T left, right, bottom, top;
            };
            struct
            {
                T fov, ratio, zNear, zFar;
            };
            struct
            {
                T width, height, depth, key;
            };
            struct
            {
                T offsetX, offsetY, sizeX, sizeY;
            };
            struct
            {
                vec2<T> xy; vec2<T> zw;
            };
            struct
            {
                vec2<T> rg; vec2<T> ba;
            };
            struct
            {
                vec3<T> xyz; T _w;
            };
            struct
            {
                vec3<T> rgb; T _a;
            };
            struct
            {
                T _x; vec3<T> yzw;
            };
            struct
            {
                T _r; vec3<T> gba;
            };
            struct
            {
                T _x1; vec2<T> yz;  T _w1;
            };
            struct
            {
                T _r1; vec2<T> gb;  T _a1;
            };

            T _v[4] = {T{0}, T{0}, T{0}, T{1}};
            SimdVector<T> _reg;
        };
    };
#pragma pack(pop)

    /// lerp between the 2 specified vectors by the specified amount
    template <typename T, ValidMathType U = T>
    [[nodiscard]] vec4<T> Lerp( const vec4<T>& u, const vec4<T>& v, U factor ) noexcept;
    /// lerp between the 2 specified vectors by the specified amount for each component
    template <typename T>
    [[nodiscard]] vec4<T> Lerp( const vec4<T>& u, const vec4<T>& v, const vec4<T>& factor ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec4<T> Abs( const vec4<T>& vector ) noexcept;
    /// min/max functions
    template <ValidMathType T>
    [[nodiscard]] vec4<T> Min( const vec4<T>& v1, const vec4<T>& v2 ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec4<T> Max( const vec4<T>& v1, const vec4<T>& v2 ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec4<T> Normalize( vec4<T>& vector ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec4<T> Normalized( const vec4<T>& vector ) noexcept;
    /// multiply a vector by a value
    template <ValidMathType T>
    [[nodiscard]] vec4<T> operator*( T fl, const vec4<T>& v ) noexcept;
    template <ValidMathType T>
    void OrthoNormalize( vec4<T>& n, vec4<T>& u );
    template <ValidMathType T>
    void OrthoNormalize( vec4<T>& v1, vec4<T>& v2, vec4<T>& v3 );
    template<ValidMathType T>
    [[nodiscard]] vec4<T> Perpendicular( const vec4<T>& vec, const vec4<T>& hint1, const vec4<T>& hint2 ) noexcept;
    template <ValidMathType T>
    [[nodiscard]] vec4<T> Clamped( const vec4<T>& v, const vec4<T>& min, const vec4<T>& max ) noexcept;

    /// Quaternion multiplications require these to be floats
    static const float2 VECTOR2_ZERO{ 0.0f };
    static const float3 VECTOR3_ZERO{ 0.0f };
    static const float4 VECTOR4_ZERO{ 0.0f };
    static const float2 VECTOR2_UNIT{ 1.0f };
    static const float3 VECTOR3_UNIT{ 1.0f };
    static const float4 VECTOR4_UNIT{ 1.0f };
    static const float2 VECTOR2_EPSILON{ EPSILON_F32 };
    static const float3 VECTOR3_EPSILON{ EPSILON_F32 };
    static const float4 VECTOR4_EPSILON{ EPSILON_F32 };
    static const float3 WORLD_X_AXIS{ 1.0f, 0.0f, 0.0f };
    static const float3 WORLD_Y_AXIS{ 0.0f, 1.0f, 0.0f };
    static const float3 WORLD_Z_AXIS{ 0.0f, 0.0f, 1.0f };
    static const float3 WORLD_X_NEG_AXIS{ -1.0f,  0.0f,  0.0f };
    static const float3 WORLD_Y_NEG_AXIS{ 0.0f, -1.0f,  0.0f };
    static const float3 WORLD_Z_NEG_AXIS{ 0.0f,  0.0f, -1.0f };
    static const float3 DEFAULT_GRAVITY{ 0.0f, -9.81f, 0.0f };

    static const int2 iVECTOR2_ZERO{ 0 };
    static const int3 iVECTOR3_ZERO{ 0 };
    static const int4 iVECTOR4_ZERO{ 0 };
    static const int3 iWORLD_X_AXIS{ 1, 0, 0 };
    static const int3 iWORLD_Y_AXIS{ 0, 1, 0 };
    static const int3 iWORLD_Z_AXIS{ 0, 0, 1 };
    static const int3 iWORLD_X_NEG_AXIS{ -1,  0,  0 };
    static const int3 iWORLD_Y_NEG_AXIS{ 0, -1,  0 };
    static const int3 iWORLD_Z_NEG_AXIS{ 0,  0, -1 };

    //ToDo: Move this to its own file
    template<typename T>
    class Rect : public vec4<T>
    {
        public:
        using vec4<T>::vec4;

        template<ValidMathType U>
        [[nodiscard]] bool contains( U xIn, U yIn ) const noexcept
        {
            return COORDS_IN_RECT( static_cast<T>(xIn), static_cast<T>(yIn), *this );
        }
        [[nodiscard]] bool contains( const vec2<T> coords ) const noexcept
        {
            return contains( coords.x, coords.y );
        }
        [[nodiscard]] bool contains( T xIn, T yIn ) const noexcept
        {
            return COORDS_IN_RECT( xIn, yIn, *this );
        }
        [[nodiscard]] vec2<T> clamp( T xIn, T yIn ) const noexcept
        {
            CLAMP_IN_RECT( xIn, yIn, *this ); return vec2<T>( xIn, yIn );
        }
        [[nodiscard]] vec2<T> clamp( const vec2<T> coords ) const noexcept
        {
            return clamp( coords.x, coords.y );
        }
    };

    static const Rect<I32> UNIT_VIEWPORT{ 0, 0, 1, 1 };
    static const Rect<I32> UNIT_RECT{ -1, 1, -1, 1 };

}  // namespace Divide

// Inline definitions
#include "MathVectors.inl"

#endif //DVD_MATH_VECTORS_H_
