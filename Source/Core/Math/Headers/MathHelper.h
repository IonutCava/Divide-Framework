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

/*Code references:
    Matrix inverse: http://www.devmaster.net/forums/showthread.php?t=14569
    Matrix multiply:
   http://fhtr.blogspot.com/2010/02/4x4-float-matrix-multiplication-using.html
    Square root: http://www.codeproject.com/KB/cpp/Sqrt_Prec_VS_Speed.aspx
*/

#pragma once
#ifndef DVD_CORE_MATH_MATH_HELPER_H_
#define DVD_CORE_MATH_MATH_HELPER_H_

#include <numbers>
namespace Divide {

#define TO_MEGABYTES(X) ((X) * 1024u * 1024u)

template<typename T>
concept ValidMathType = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

template <typename T>
class mat2;
template <typename T>
class mat3;
template <typename T>
class mat4;
template <typename T>
class vec2;
template <typename T>
class vec3;
template <typename T>
class vec4;
template <typename T>
class Rect;
template <typename T>
class Quaternion;

using UColour3 = vec3<U8>;
using FColour3 = vec3<F32>;

using UColour4 = vec4<U8>;
using FColour4 = vec4<F32>;

#if !defined(M_PI)
    constexpr D64 M_PI = std::numbers::pi;
    constexpr D64 M_PI_2 = M_PI / 2;
#endif

    constexpr D64 M_2PI = M_PI * 2;
    constexpr D64 M_PIDIV180 = 0.01745329251994329576;
    constexpr D64 M_180DIVPI = 57.29577951308232087679;
    constexpr D64 M_PIDIV360 = 0.00872664625997164788;
    constexpr D64 M_PI2 = M_2PI;

    constexpr F32 M_PI_f = to_F32(M_PI);
    constexpr F32 M_PI_2_f = to_F32(M_PI_2);
    constexpr F32 M_PI_4_f = M_PI_f / 4;
    constexpr F32 M_2PI_f = to_F32(M_2PI);
    constexpr F32 M_PIDIV180_f = to_F32(M_PIDIV180);
    constexpr F32 M_180DIVPI_f = to_F32(M_180DIVPI);
    constexpr F32 M_PIDIV360_f = to_F32(M_PIDIV360);
    constexpr F32 M_PI2_f = M_2PI_f;

    constexpr F32 INV_RAND_MAX = 0.0000305185094f;

// SubRange instead of vec2<U16> to keep things trivial
struct SubRange
{
    U16 _offset{ 0u };
    U16 _count{ U16_MAX };

    bool operator==(const SubRange&) const = default;
};

template<typename T>
using SignedIntegerBasedOnSize = typename std::conditional<sizeof(T) == 8, I64, I32>::type;
template<typename T>
using UnsignedIntegerBasedOnSize = typename std::conditional<sizeof(T) == 8, U64, U32>::type;
template<typename T>
using IntegerTypeBasedOnSign = typename std::conditional<std::is_unsigned<T>::value,
                                                         UnsignedIntegerBasedOnSize<T>,
                                                         SignedIntegerBasedOnSize<T>>::type;

template<typename T>
using DefaultDistribution = typename std::conditional<std::is_integral<T>::value, 
                                     std::uniform_int_distribution<IntegerTypeBasedOnSign<T>>,
                                     std::uniform_real_distribution<T>>::type;

template <typename T, 
          typename Engine = std::mt19937_64,
          typename Distribution = DefaultDistribution<T>> requires std::is_arithmetic_v<T>
[[nodiscard]] T Random(T min, T max);

template <typename T,
          typename Engine = std::mt19937_64,
          typename Distribution = DefaultDistribution<T>> requires std::is_arithmetic_v<T>
[[nodiscard]] T Random(T max);

template <typename T,
          typename Engine = std::mt19937_64,
          typename Distribution = DefaultDistribution<T>> requires std::is_arithmetic_v<T>
[[nodiscard]] T Random();

template<typename Engine = std::mt19937_64>
void SeedRandom();

template<typename Engine = std::mt19937_64>
void SeedRandom(U32 seed);

template <typename T> 
constexpr I32 SIGN(const T val) {
    return (val < 0) ? -1 : (val > 0) ? 1 : 0;
}

/// Clamps value n between min and max
template <typename T> requires std::is_arithmetic<T>::value
constexpr void CLAMP(T& n, T min, T max) noexcept;

template <typename T> requires std::is_arithmetic<T>::value
constexpr void CLAMP_01(T& n) noexcept;

template <typename T> requires std::is_arithmetic<T>::value
[[nodiscard]] constexpr T CLAMPED(T n, T min, T max) noexcept;

template <typename T> requires std::is_arithmetic<T>::value
[[nodiscard]] constexpr T CLAMPED_01(T n) noexcept;

template <typename T> requires std::is_arithmetic<T>::value
[[nodiscard]] constexpr T MAP(T input, T in_min, T in_max, T out_min, T out_max, D64& slopeOut) noexcept;

template <typename T>
constexpr void REMAP(T& input, T in_min, T in_max, T out_min, T out_max, D64& slopeOut) noexcept;

template <typename T>
[[nodiscard]] constexpr T SQUARED(T input) noexcept;

template <typename T>
[[nodiscard]] constexpr T MAP(const T input, const T in_min, const T in_max, const T out_min, const T out_max) noexcept {
    D64 slope = 0.0;
    return MAP(input, in_min, in_max, out_min, out_max, slope);
}

template <typename T>
constexpr void REMAP(T& input, T in_min, T in_max, T out_min, T out_max) noexcept {
    D64 slope = 0.0;
    input = MAP(input, in_min, in_max, out_min, out_max, slope);
}

template <typename T>
[[nodiscard]] vec2<T> COORD_REMAP(vec2<T> input, const Rect<T>& in_rect, const Rect<T>& out_rect) noexcept {
    return vec2<T> {
        MAP(input.x, in_rect.x, in_rect.x + in_rect.z, out_rect.x, out_rect.x + out_rect.z),
        MAP(input.y, in_rect.y, in_rect.y + in_rect.w, out_rect.y, out_rect.y + out_rect.w)
    };
}

template <typename T>
[[nodiscard]] vec3<T> COORD_REMAP(vec3<T> input, const Rect<T>& in_rect, const Rect<T>& out_rect) noexcept {
    return vec3<T>(COORD_REMAP(input.xy(), in_rect, out_rect), input.z);
}

template <typename T>
[[nodiscard]] T NORMALIZE(T input, const T range_min, const T range_max) noexcept {
    return MAP<T>(input, range_min, range_max, T(0), T(1));
}

template<typename T>
void CLAMP_IN_RECT(T& inout_x, T& inout_y, T rect_x, T rect_y, T rect_z, T rect_w) noexcept;

template<typename T>
void CLAMP_IN_RECT(T& inout_x, T& inout_y, const Rect<T>& rect) noexcept;

template<typename T>
void CLAMP_IN_RECT(T& inout_x, T& inout_y, const vec4<T>& rect) noexcept;

template<typename T>
[[nodiscard]] bool COORDS_IN_RECT(T input_x, T input_y, T rect_x, T rect_y, T rect_z, T rect_w) noexcept;

template<typename T>
[[nodiscard]] bool COORDS_IN_RECT(T input_x, T input_y, const Rect<T>& rect) noexcept;

template<typename T>
[[nodiscard]] bool COORDS_IN_RECT(T input_x, T input_y, const vec4<T>& rect) noexcept;

[[nodiscard]] constexpr U32 nextPOW2(U32 n) noexcept;
[[nodiscard]] constexpr U32 prevPOW2(U32 n) noexcept;

template<typename T>
[[nodiscard]] constexpr T MipCount(T width, T height) noexcept;

// Calculate the smallest NxN matrix that can hold the specified
// number of elements. Returns N
[[nodiscard]] constexpr U32 minSquareMatrixSize(U32 elementCount) noexcept;

template <typename T, typename U>
[[nodiscard]] T Lerp(T v1, T v2, U t) noexcept;

template <typename T, typename U>
[[nodiscard]] T FastLerp(T v1, T v2, U t) noexcept;

template <typename T>
[[nodiscard]] T Sqrt(T input) noexcept;

template <typename T, typename U>
[[nodiscard]] T Sqrt(U input) noexcept;

///Helper methods to go from an snorm float (-1...1) to packed unorm char (value => (value + 1) * 0.5 => U8)
[[nodiscard]] constexpr U8 PACKED_FLOAT_TO_CHAR_UNORM(F32_SNORM value) noexcept;
[[nodiscard]] constexpr F32_SNORM UNORM_CHAR_TO_PACKED_FLOAT(U8 value) noexcept;

/// Returns clamped value * 128
[[nodiscard]] constexpr I8 FLOAT_TO_CHAR_SNORM(F32_SNORM value) noexcept;
// Returns clamped value / 128.f
[[nodiscard]] constexpr F32_SNORM SNORM_CHAR_TO_FLOAT(I8 value) noexcept;

/// Returns value / 255.f
[[nodiscard]] constexpr F32_NORM UNORM_CHAR_TO_FLOAT(U8 value) noexcept;
/// Returns round(value * 255)
[[nodiscard]] constexpr U8 FLOAT_TO_CHAR_UNORM(F32_NORM value) noexcept;

/// Helper method to emulate GLSL
[[nodiscard]] F32 FRACT(F32 floatValue) noexcept;

// bit manipulation
template<typename T1, typename T2>
constexpr auto BitSet(T1& arg, const T2 pos) { return arg |= 1 << pos; }

template<typename T1, typename T2>
constexpr auto BitClr(T1& arg, const T2 pos) { return arg &= ~(1 << pos); }

template<typename T1, typename T2>
constexpr bool BitTst(const T1 arg, const T2 pos) { return (arg & 1 << pos) != 0; }

template<typename T1, typename T2>
constexpr bool BitDiff(const T1 arg1, const T2 arg2) { return arg1 ^ arg2; }

template<typename T1, typename T2, typename T3>
constexpr bool BitCmp(const T1 arg1, const T2 arg2, const T3 pos) { return arg1 << pos == arg2 << pos; }

// bitmask manipulation
template<typename T1, typename T2>
constexpr auto BitMaskSet(T1& arg, const T2 mask) { return arg |= mask; }
template<typename T1, typename T2>
constexpr auto BitMaskClear(T1& arg, const T2 mask) { return arg &= ~mask; }
template<typename T1, typename T2>
constexpr auto BitMaskFlip(T1& arg, const T2 mask) { return arg ^= mask; }
template<typename T1, typename T2>
constexpr auto BitMaskCheck(T1& arg, const T2 mask) { return arg & mask; }

namespace Angle {

template<typename T> requires std::is_fundamental_v<T>
using RADIANS = T;

template<typename T> requires std::is_fundamental_v<T>
using DEGREES = T;

    using DEGREES_F = DEGREES<F32>;
    using RADIANS_F = RADIANS<F32>;

    using RADIANS_D = RADIANS<D64>;
    using DEGREES_D = DEGREES<D64>;

template <typename T>
[[nodiscard]] constexpr DEGREES<T> to_VerticalFoV(DEGREES<T> horizontalFoV, D64 aspectRatio) noexcept;
template <typename T>
[[nodiscard]] constexpr DEGREES<T> to_HorizontalFoV(DEGREES<T> verticalFoV, D64 aspectRatio) noexcept;
template <typename T>
[[nodiscard]] constexpr RADIANS<T> to_RADIANS(DEGREES<T> angle) noexcept;
template <typename T>
[[nodiscard]] constexpr DEGREES<T> to_DEGREES(RADIANS<T> angle) noexcept;
template <typename T>
[[nodiscard]] constexpr vec2<RADIANS<T>> to_RADIANS(vec2<DEGREES<T>> angle) noexcept;
template <typename T>
[[nodiscard]] constexpr vec2<DEGREES<T>> to_DEGREES(vec2<RADIANS<T>> angle) noexcept;
template <typename T>
[[nodiscard]] constexpr vec3<RADIANS<T>> to_RADIANS(const vec3<DEGREES<T>>& angle) noexcept;
template <typename T>
[[nodiscard]] constexpr vec3<DEGREES<T>> to_DEGREES(const vec3<RADIANS<T>>& angle) noexcept;
template <typename T>
[[nodiscard]] constexpr vec4<RADIANS<T>> to_RADIANS(const vec4<DEGREES<T>>& angle) noexcept;
template <typename T>
[[nodiscard]] constexpr vec4<DEGREES<T>> to_DEGREES(const vec4<RADIANS<T>>& angle) noexcept;
} //namespace Angle
/// Return the radian equivalent of the given degree value
template <typename T>
[[nodiscard]] constexpr T DegreesToRadians(T angleDegrees) noexcept;
/// Return the degree equivalent of the given radian value
template <typename T>
[[nodiscard]] constexpr T RadiansToDegrees(T angleRadians) noexcept;
/// Returns the specified value. Used only for emphasis
template <typename T>
[[nodiscard]] constexpr T Degrees(T degrees) noexcept;
/// Returns the specified value. Used only for emphasis
template <typename T>
[[nodiscard]] constexpr T Radians(T radians) noexcept;
};

namespace Metric
{
/// Base value * 1000000000000
template <typename T>
[[nodiscard]] constexpr T Tera(T a);
/// Base value * 1000000000
template <typename T>
[[nodiscard]] constexpr T Giga(T a);
/// Base value * 1000000
template <typename T>
[[nodiscard]] constexpr T Mega(T a);
/// Base value * 1000
template <typename T>
[[nodiscard]] constexpr T Kilo(T a);
/// Base value * 100
template <typename T>
[[nodiscard]] constexpr T Hecto(T a);
/// Base value * 10
template <typename T>
[[nodiscard]] constexpr T Deca(T a);
/// Base value
template <typename T>
[[nodiscard]] constexpr T Base(T a);
/// Base value * 0.1
template <typename T>
[[nodiscard]] constexpr T Deci(T a);
/// Base value * 0.01
template <typename T>
[[nodiscard]] constexpr T Centi(T a);
/// Base value * 0.001
template <typename T>
[[nodiscard]] constexpr T Milli(T a);
/// Base value * 0.000001
template <typename T>
[[nodiscard]] constexpr T Micro(T a);
/// Base value * 0.000000001
template <typename T>
[[nodiscard]] constexpr T Nano(T a);
/// Base value * 0.000000000001
template <typename T>
[[nodiscard]] constexpr T Pico(T a);

/// Base value * 1000000000000
template <typename T, typename U>
[[nodiscard]] constexpr T Tera(U a);
/// Base value * 1000000000
template <typename T, typename U>
[[nodiscard]] constexpr T Giga(U a);
/// Base value * 1000000
template <typename T, typename U>
[[nodiscard]] constexpr T Mega(U a);
/// Base value * 1000
template <typename T, typename U>
[[nodiscard]] constexpr T Kilo(U a);
/// Base value * 100
template <typename T, typename U>
[[nodiscard]] constexpr T Hecto(U a);
/// Base value * 10
template <typename T, typename U>
[[nodiscard]] constexpr T Deca(U a);
/// Base value
template <typename T, typename U>
[[nodiscard]] constexpr T Base(U a);
/// Base value * 0.1
template <typename T, typename U>
[[nodiscard]] constexpr T Deci(U a);
/// Base value * 0.01
template <typename T, typename U>
[[nodiscard]] constexpr T Centi(U a);
/// Base value * 0.001
template <typename T, typename U>
[[nodiscard]] constexpr T Milli(U a);
/// Base value * 0.000001
template <typename T, typename U>
[[nodiscard]] constexpr T Micro(U a);
/// Base value * 0.000000001
template <typename T, typename U>
[[nodiscard]] constexpr T Nano(U a);
/// Base value * 0.000000000001
template <typename T, typename U>
[[nodiscard]] constexpr T Pico(U a);
} //namespace Metric

struct SimpleTime
{
    U8 _hour = 0u;
    U8 _minutes = 0u;
};

struct SimpleLocation
{
    F32 _latitude = 0;
    F32 _longitude = 0;
};

namespace Time
{
/// Return the passed param without any modification
/// Used only for emphasis
template <typename T>
[[nodiscard]] constexpr T Hours(T a);
template <typename T>
[[nodiscard]] constexpr T Minutes( T a );
template <typename T>
[[nodiscard]] constexpr T Seconds( T a );
template <typename T>
[[nodiscard]] constexpr T Milliseconds(T a);
template <typename T>
[[nodiscard]] constexpr T Microseconds(T a);
template <typename T>
[[nodiscard]] constexpr T Nanoseconds(T a);

template <typename T, typename U>
[[nodiscard]] constexpr T Hours( U a );
template <typename T, typename U>
[[nodiscard]] constexpr T Minutes( U a );
template <typename T, typename U>
[[nodiscard]] constexpr T Seconds(U a);
template <typename T, typename U>
[[nodiscard]] constexpr T Milliseconds(U a);
template <typename T, typename U>
[[nodiscard]] constexpr T Microseconds(U a);
template <typename T, typename U>
[[nodiscard]] constexpr T Nanoseconds(U a);

template <typename T = D64, typename U>
[[nodiscard]] constexpr T NanosecondsToSeconds(U a) noexcept;
template <typename T = D64, typename U>
[[nodiscard]] constexpr T NanosecondsToMilliseconds(U a) noexcept;
template <typename T = U64, typename U>
[[nodiscard]] constexpr T NanosecondsToMicroseconds(U a) noexcept;

template <typename T = D64, typename U>
[[nodiscard]] constexpr T MicrosecondsToSeconds(U a) noexcept;
template <typename T = U64, typename U>
[[nodiscard]] constexpr T MicrosecondsToMilliseconds(U a) noexcept;
template <typename T = U64, typename U>
[[nodiscard]] constexpr T MicrosecondsToNanoseconds(U a) noexcept;

template <typename T = D64, typename U>
[[nodiscard]] constexpr T MillisecondsToSeconds(U a) noexcept;
template <typename T = U64, typename U>
[[nodiscard]] constexpr T MillisecondsToMicroseconds(U a) noexcept;
template <typename T = U64, typename U>
[[nodiscard]] constexpr T MillisecondsToNanoseconds(U a) noexcept;

template <typename T = D64, typename U>
[[nodiscard]] constexpr T SecondsToMilliseconds(U a) noexcept;
template <typename T = U64, typename U>
[[nodiscard]] constexpr T SecondsToMicroseconds(U a) noexcept;
template <typename T = U64, typename U>
[[nodiscard]] constexpr T SecondsToNanoseconds(U a) noexcept;
}  // namespace Time

template<typename T>
using base_type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

template<typename T>
concept ValidMathType = is_base_of_template<primitiveWrapper, base_type<T>>::value || (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);

namespace Util
{

struct Circle
{
    F32 center[2] = {0.0f, 0.0f};
    F32 radius = 1.f;
};

[[nodiscard]] bool IntersectCircles(const Circle& cA, const Circle& cB, vec2<F32>* pointsOut) noexcept;

[[nodiscard]] size_t GetAlignmentCorrected(const size_t value, const size_t alignment) noexcept;

/// a la Boost
template <typename T, typename... Rest>
void Hash_combine(size_t& seed, const T& v, const Rest&... rest) noexcept;

template<class FwdIt, class Compare = std::less<typename std::iterator_traits<FwdIt>::value_type>>
void InsertionSort(FwdIt first, FwdIt last, Compare cmp = Compare());

/** Ogre3D
@brief Normalise the selected rotations to be within the +/-180 degree range.
@details The normalise uses a wrap around,
@details so for example a yaw of 360 degrees becomes 0 degrees, and -190 degrees
becomes 170.
@param inputRotation rotation to normalize
@param degrees if true, values are in degrees, otherwise we use radians
@param normYaw If false, the yaw isn't normalized.
@param normPitch If false, the pitch isn't normalized.
@param normRoll If false, the roll isn't normalized.
*/
void Normalize(vec3<F32>& inputRotation, bool degrees = false,
               bool normYaw = true, bool normPitch = true,
               bool normRoll = true) noexcept;

[[nodiscard]] UColour4  ToByteColour(const FColour4& floatColour) noexcept;
[[nodiscard]] UColour3  ToByteColour(const FColour3& floatColour) noexcept;
[[nodiscard]] FColour4 ToFloatColour(const UColour4& byteColour) noexcept;
[[nodiscard]] FColour3 ToFloatColour(const UColour3& byteColour) noexcept;
[[nodiscard]] FColour4 ToFloatColour( const vec4<U32>& colour ) noexcept;
[[nodiscard]] FColour3 ToFloatColour( const vec3<U32>& colour ) noexcept;

void ToByteColour(const FColour4& floatColour, UColour4& colourOut) noexcept;
void ToByteColour(const FColour3& floatColour, UColour3& colourOut) noexcept;
void ToFloatColour(const UColour4& byteColour, FColour4& colourOut) noexcept;
void ToFloatColour(const UColour3& byteColour, FColour3& colourOut) noexcept;
void ToFloatColour( const vec4<U32>& uintColour, FColour4& colourOut ) noexcept;
void ToFloatColour( const vec3<U32>& uintColour, FColour3& colourOut ) noexcept;

bool decomposeMatrix(const mat4<F32>& transform,
                     vec3<F32>& translationOut,
                     vec3<F32>& scaleOut,
                     vec3<Angle::RADIANS_F>& rotationOut,
                     bool& isUniformScaleOut);

bool decomposeMatrix(const mat4<F32>& transform,
                     vec3<F32>& translationOut,
                     vec3<F32>& scaleOut,
                     vec3<Angle::RADIANS_F>& rotationOut);

bool decomposeMatrix(const mat4<F32>& transform,
                     vec3<F32>& translationOut,
                     vec3<F32>& scaleOut);

bool decomposeMatrix(const mat4<F32>& transform,
                     vec3<F32>& translationOut);

//ref: https://community.khronos.org/t/glsl-packing-a-normal-in-a-single-float/52039/3
// Pack 3 values into 1 float
[[nodiscard]] F32 PACK_VEC3(F32_SNORM x, F32_SNORM y, F32_SNORM z) noexcept;
[[nodiscard]] F32 PACK_VEC3(U8 x, U8 y, U8 z) noexcept;

[[nodiscard]] F32 PACK_VEC3(const vec3<F32_SNORM>& value) noexcept;

[[nodiscard]] U32 PACK_HALF2x16(vec2<F32> value);
void UNPACK_HALF2x16(U32 src, vec2<F32>& value);
[[nodiscard]] vec2<F32> UNPACK_HALF2x16(U32 src);

[[nodiscard]] U32 PACK_HALF2x16(F32 x, F32 y);
void UNPACK_HALF2x16(U32 src, F32& x, F32& y);

/// Only convert the range [-1024., 1024.] for accurate results
[[nodiscard]] U16 PACK_HALF1x16(F32 value);
/// Only convert the range [-1024., 1024.] for accurate results
void UNPACK_HALF1x16(U16 src, F32& value);
/// Only convert the range [-1024., 1024.] for accurate results
[[nodiscard]] F32 UNPACK_HALF1x16(U16 src);

[[nodiscard]] F32 UINT_TO_FLOAT(U32 src);
U32 FLOAT_TO_UINT(F32 src);

[[nodiscard]] F32 INT_TO_FLOAT(I32 src);
I32 FLOAT_TO_INT(F32 src);

[[nodiscard]] U32 PACK_UNORM4x8(const vec4<F32_NORM>& value);
[[nodiscard]] U32 PACK_UNORM4x8(vec4<U8> value);
void UNPACK_UNORM4x8(U32 src, vec4<F32_NORM>& value);

[[nodiscard]] U32 PACK_UNORM4x8(F32_NORM x, F32_NORM y, F32_NORM z, F32_NORM w);
[[nodiscard]] U32 PACK_UNORM4x8(U8 x, U8 y, U8 z, U8 w);
void UNPACK_UNORM4x8(U32 src, F32_NORM& x, F32_NORM& y, F32_NORM& z, F32_NORM& w);
void UNPACK_UNORM4x8(U32 src, U8& x, U8& y, U8& z, U8& w);
[[nodiscard]] vec4<U8> UNPACK_UNORM4x8_U8(U32 src);
[[nodiscard]] vec4<F32_NORM> UNPACK_UNORM4x8_F32(U32 src);

// UnPack 3 values from 1 float
void UNPACK_VEC3(F32 src, F32_SNORM& x, F32_SNORM& y, F32_SNORM& z) noexcept;
void UNPACK_VEC3(F32 src, vec3<F32_SNORM>& res) noexcept;
[[nodiscard]] vec3<F32_SNORM> UNPACK_VEC3(F32 src) noexcept;

[[nodiscard]] U32 PACK_11_11_10(const vec3<F32_NORM>& value);
void UNPACK_11_11_10(U32 src, vec3<F32_NORM>& res);
[[nodiscard]] vec3<F32_NORM> UNPACK_11_11_10(U32 src);

[[nodiscard]] U32 PACK_11_11_10(F32_NORM x, F32_NORM y, F32_NORM z);
void UNPACK_11_11_10(U32 src, F32_NORM& x, F32_NORM& y, F32_NORM& z);

}  // namespace Util
}  // namespace Divide

namespace std {
    template<typename T, size_t N>
    struct hash<array<T, N> >
    {
        using argument_type = array<T, N>;
        using result_type = size_t;

        result_type operator()(const argument_type& a) const
        {
            result_type h = 17;
            for (const T& elem : a)
            {
                Divide::Util::Hash_combine(h, elem);
            }
            return h;
        }
    };
}

#endif  //DVD_CORE_MATH_MATH_HELPER_H_

#include "MathHelper.inl"
