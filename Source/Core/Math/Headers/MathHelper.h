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

template <typename T> class mat2;
template <typename T> class mat3;
template <typename T> class mat4;
template <typename T> class vec2;
template <typename T> class vec3;
template <typename T> class vec4;
template <typename T> class Rect;
template <typename T> class Quaternion;

using float2 = vec2<F32>;
using float3 = vec3<F32>;
using float4 = vec4<F32>;
using int2   = vec2<I32>;
using int3   = vec3<I32>;
using int4   = vec4<I32>;
using uint2  = vec2<U32>;
using uint3  = vec3<U32>;
using uint4  = vec4<U32>;

using UColour3 = vec3<U8>;
using FColour3 = float3;

using UColour4 = vec4<U8>;
using FColour4 = float4;

using quatf = Quaternion<F32>;

#if !defined(M_PI)
    constexpr D64 M_PI = std::numbers::pi;
#endif

    constexpr D64 M_PI_DIV_2 = M_PI / 2;
    constexpr D64 M_PI_DIV_4 = M_PI / 4;
    constexpr D64 M_PI_MUL_2 = M_PI * 2;
    constexpr D64 M_PI_MUL_4 = M_PI * 4;
    constexpr D64 M_PI_DIV_180 = 0.01745329251994329576;
    constexpr D64 M_180_DIV_PI = 57.29577951308232087679;
    constexpr D64 M_PI_DIV_360 = 0.00872664625997164788;

    constexpr F32 M_PI_f         = to_F32(M_PI);
    constexpr F32 M_PI_DIV_2_f   = to_F32(M_PI_DIV_2);
    constexpr F32 M_PI_DIV_4_f   = to_F32(M_PI_DIV_4);
    constexpr F32 M_PI_MUL_2_f   = to_F32(M_PI_MUL_2);
    constexpr F32 M_PI_MUL_4_f   = to_F32(M_PI_MUL_4);
    constexpr F32 M_PI_DIV_180_f = to_F32(M_PI_DIV_180);
    constexpr F32 M_180_DIV_PI_f = to_F32(M_180_DIV_PI);
    constexpr F32 M_PI_DIV_360_f = to_F32(M_PI_DIV_360);

    constexpr F32 INV_RAND_MAX = 0.0000305185094f;

// SubRange instead of vec2<U16> to keep things trivial
struct SubRange
{
    U16 _offset{ 0u };
    U16 _count{ U16_MAX };

    bool operator==(const SubRange&) const = default;
};

template<typename T>
using base_type = typename std::remove_cv_t<typename std::remove_reference_t<T>>;

template<typename T>
concept ValidMathType = is_base_of_template<primitiveWrapper, base_type<T>>::value || (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);

template<typename T> using SignedIntegerBasedOnSize   = std::conditional_t<sizeof(T) == 8, I64, I32>;
template<typename T> using UnsignedIntegerBasedOnSize = std::conditional_t<sizeof(T) == 8, U64, U32>;
template<typename T> using IntegerTypeBasedOnSign     = std::conditional_t<std::is_unsigned_v<T>, UnsignedIntegerBasedOnSize<T>, SignedIntegerBasedOnSize<T>>;
template<typename T> using DefaultDistribution        = std::conditional_t<std::is_integral_v<T>,  std::uniform_int_distribution<IntegerTypeBasedOnSign<T>>, std::uniform_real_distribution<T>>;

template <ValidMathType T,
          typename Engine = std::mt19937_64,
          typename Distribution = DefaultDistribution<T>>
[[nodiscard]] T Random(T min, T max);

template <ValidMathType T,
          typename Engine = std::mt19937_64,
          typename Distribution = DefaultDistribution<T>>
[[nodiscard]] T Random(T max);

template <ValidMathType T,
          typename Engine = std::mt19937_64,
          typename Distribution = DefaultDistribution<T>>
[[nodiscard]] T Random();

template<typename Engine = std::mt19937_64>
void SeedRandom();

template<typename Engine = std::mt19937_64>
void SeedRandom(U32 seed);

template <ValidMathType T>
constexpr I32 SIGN(const T val)
{
    return (val < 0) ? -1 : (val > 0) ? 1 : 0;
}

/// Clamps value n between min and max
template <ValidMathType T>
constexpr void CLAMP(T& n, T min, T max) noexcept;

template <ValidMathType T>
constexpr void CLAMP_01(T& n) noexcept;

template <ValidMathType T>
[[nodiscard]] constexpr T CLAMPED(T n, T min, T max) noexcept;

template <ValidMathType T>
[[nodiscard]] constexpr T CLAMPED_01(T n) noexcept;

template <ValidMathType T>
[[nodiscard]] constexpr T MAP(T input, T in_min, T in_max, T out_min, T out_max, D64& slopeOut) noexcept;

template <ValidMathType T>
constexpr void REMAP(T& input, T in_min, T in_max, T out_min, T out_max, D64& slopeOut) noexcept;

template <ValidMathType T>
[[nodiscard]] constexpr T SQUARED(T input) noexcept;

template <ValidMathType T>
[[nodiscard]] constexpr T MAP(const T input, const T in_min, const T in_max, const T out_min, const T out_max) noexcept {
    D64 slope = 0.0;
    return MAP(input, in_min, in_max, out_min, out_max, slope);
}

template <ValidMathType T>
constexpr void REMAP(T& input, T in_min, T in_max, T out_min, T out_max) noexcept
{
    D64 slope = 0.0;
    input = MAP(input, in_min, in_max, out_min, out_max, slope);
}

template <ValidMathType T>
[[nodiscard]] vec2<T> COORD_REMAP(vec2<T> input, const Rect<T>& in_rect, const Rect<T>& out_rect) noexcept
{
    return vec2<T> {
        MAP(input.x, in_rect.x, in_rect.x + in_rect.z, out_rect.x, out_rect.x + out_rect.z),
        MAP(input.y, in_rect.y, in_rect.y + in_rect.w, out_rect.y, out_rect.y + out_rect.w)
    };
}

template <ValidMathType T>
[[nodiscard]] vec3<T> COORD_REMAP(vec3<T> input, const Rect<T>& in_rect, const Rect<T>& out_rect) noexcept
{
    return vec3<T>(COORD_REMAP(input.xy(), in_rect, out_rect), input.z);
}

template <ValidMathType T>
[[nodiscard]] T NORMALIZE(T input, const T range_min, const T range_max) noexcept
{
    return MAP<T>(input, range_min, range_max, T(0), T(1));
}

template<ValidMathType T>
void CLAMP_IN_RECT(T& inout_x, T& inout_y, T rect_x, T rect_y, T rect_z, T rect_w) noexcept;

template<ValidMathType T>
void CLAMP_IN_RECT(T& inout_x, T& inout_y, const Rect<T>& rect) noexcept;

template<ValidMathType T>
void CLAMP_IN_RECT(T& inout_x, T& inout_y, const vec4<T>& rect) noexcept;

template<ValidMathType T>
[[nodiscard]] bool COORDS_IN_RECT(T input_x, T input_y, T rect_x, T rect_y, T rect_z, T rect_w) noexcept;

template<ValidMathType T>
[[nodiscard]] bool COORDS_IN_RECT(T input_x, T input_y, const Rect<T>& rect) noexcept;

template<ValidMathType T>
[[nodiscard]] bool COORDS_IN_RECT(T input_x, T input_y, const vec4<T>& rect) noexcept;

template<ValidMathType T>
[[nodiscard]] constexpr T roundup(T value, U32 maxb = sizeof(T) * CHAR_BIT, U32 curb = 1);
[[nodiscard]] constexpr U32 nextPOW2(U32 n) noexcept;
[[nodiscard]] constexpr U32 prevPOW2(U32 n) noexcept;

template<ValidMathType T>
[[nodiscard]] constexpr T MipCount(T width, T height) noexcept;

// Calculate the smallest NxN matrix that can hold the specified
// number of elements. Returns N
[[nodiscard]] constexpr U32 minSquareMatrixSize(U32 elementCount) noexcept;

template <typename T, ValidMathType U = T> [[nodiscard]] T Lerp(T v1, T v2, U t) noexcept;
template <typename T, ValidMathType U = T> [[nodiscard]] T LerpFast(T v1, T v2, U t) noexcept;
template <ValidMathType T, typename U = T> [[nodiscard]] T Sqrt(U input) noexcept;
template <ValidMathType T, typename U = T> [[nodiscard]] T InvSqrt(U input) noexcept;
template <ValidMathType T, typename U = T> [[nodiscard]] T InvSqrtFast(U input) noexcept;

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
template<typename T1, typename T2> constexpr auto BitSet(T1& arg, const T2 pos) { return arg |= 1 << pos; }
template<typename T1, typename T2> constexpr auto BitClr(T1& arg, const T2 pos) { return arg &= ~(1 << pos); }
template<typename T1, typename T2> constexpr bool BitTst(const T1 arg, const T2 pos) { return (arg & 1 << pos) != 0; }
template<typename T1, typename T2> constexpr bool BitDiff(const T1 arg1, const T2 arg2) { return arg1 ^ arg2; }

template<typename T1, typename T2, typename T3> constexpr bool BitCmp(const T1 arg1, const T2 arg2, const T3 pos) { return arg1 << pos == arg2 << pos; }

// bitmask manipulation
template<typename T1, typename T2> constexpr auto BitMaskSet(T1& arg, const T2 mask) { return arg |= mask; }
template<typename T1, typename T2> constexpr auto BitMaskClear(T1& arg, const T2 mask) { return arg &= ~mask; }
template<typename T1, typename T2> constexpr auto BitMaskFlip(T1& arg, const T2 mask) { return arg ^= mask; }
template<typename T1, typename T2> constexpr auto BitMaskCheck(T1& arg, const T2 mask) { return arg & mask; }

namespace Angle {

    struct Radians_t{};
    struct Degrees_t{};

#if 1
    template<typename T> requires std::is_fundamental_v<T>
    using RADIANS = primitiveWrapper<T, Radians_t>;

    template<typename T> requires std::is_fundamental_v<T>
    using DEGREES = primitiveWrapper<T, Degrees_t>;
#else
    template<typename T> requires std::is_fundamental_v<T>
    using RADIANS = T;

    template<typename T> requires std::is_fundamental_v<T>
    using DEGREES = T;
#endif
    using DEGREES_F = DEGREES<F32>;
    using RADIANS_F = RADIANS<F32>;

    using RADIANS_D = RADIANS<D64>;
    using DEGREES_D = DEGREES<D64>;

    template <typename T> [[nodiscard]] constexpr DEGREES<T> to_VerticalFoV(DEGREES<T> horizontalFoV, D64 aspectRatio) noexcept;
    template <typename T> [[nodiscard]] constexpr DEGREES<T> to_HorizontalFoV(DEGREES<T> verticalFoV, D64 aspectRatio) noexcept;

    template <typename T> [[nodiscard]] RADIANS<T>       to_RADIANS(DEGREES<T> angle)              noexcept;
    template <typename T> [[nodiscard]] DEGREES<T>       to_DEGREES(RADIANS<T> angle)              noexcept;
    template <typename T> [[nodiscard]] vec2<RADIANS<T>> to_RADIANS(const vec2<DEGREES<T>>& angle) noexcept;
    template <typename T> [[nodiscard]] vec2<DEGREES<T>> to_DEGREES(const vec2<RADIANS<T>>& angle) noexcept;
    template <typename T> [[nodiscard]] vec3<RADIANS<T>> to_RADIANS(const vec3<DEGREES<T>>& angle) noexcept;
    template <typename T> [[nodiscard]] vec3<DEGREES<T>> to_DEGREES(const vec3<RADIANS<T>>& angle) noexcept;
    template <typename T> [[nodiscard]] vec4<RADIANS<T>> to_RADIANS(const vec4<DEGREES<T>>& angle) noexcept;
    template <typename T> [[nodiscard]] vec4<DEGREES<T>> to_DEGREES(const vec4<RADIANS<T>>& angle) noexcept;
} //namespace Angle

namespace Metric
{
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Tera(  InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Giga(  InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Mega(  InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Kilo(  InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Hecto( InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Deca(  InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Base(  InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Deci(  InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Centi( InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Milli( InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Micro( InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Nano(  InType a );
    template <typename OutType = F32, typename InType = OutType> [[nodiscard]] constexpr OutType Pico(  InType a );
} //namespace Metric

namespace Bytes
{
    template <typename OutType = size_t, typename InType = OutType> [[nodiscard]] constexpr OutType Peta( InType a );
    template <typename OutType = size_t, typename InType = OutType> [[nodiscard]] constexpr OutType Tera( InType a );
    template <typename OutType = size_t, typename InType = OutType> [[nodiscard]] constexpr OutType Giga( InType a );
    template <typename OutType = size_t, typename InType = OutType> [[nodiscard]] constexpr OutType Mega( InType a );
    template <typename OutType = size_t, typename InType = OutType> [[nodiscard]] constexpr OutType Kilo( InType a );
    template <typename OutType = size_t, typename InType = OutType> [[nodiscard]] constexpr OutType Base( InType a );
} //namespace Bytes

namespace Time
{
    /// Return the passed param without any modification (Used only for emphasis).
    template <typename OutType = U64, typename InType = OutType> [[nodiscard]] constexpr OutType Hours(        InType a );
    template <typename OutType = U64, typename InType = OutType> [[nodiscard]] constexpr OutType Minutes(      InType a );
    template <typename OutType = U64, typename InType = OutType> [[nodiscard]] constexpr OutType Seconds(      InType a );
    template <typename OutType = U64, typename InType = OutType> [[nodiscard]] constexpr OutType Milliseconds( InType a );
    template <typename OutType = U64, typename InType = OutType> [[nodiscard]] constexpr OutType Microseconds( InType a );
    template <typename OutType = U64, typename InType = OutType> [[nodiscard]] constexpr OutType Nanoseconds(  InType a );

    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType NanosecondsToSeconds(       InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType NanosecondsToMilliseconds(  InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType NanosecondsToMicroseconds(  InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType MicrosecondsToSeconds(      InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType MicrosecondsToMilliseconds( InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType MicrosecondsToNanoseconds(  InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType MillisecondsToSeconds(      InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType MillisecondsToMicroseconds( InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType MillisecondsToNanoseconds(  InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType SecondsToMilliseconds(      InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType SecondsToMicroseconds(      InType a ) noexcept;
    template <typename OutType = D64, typename InType = OutType> [[nodiscard]] constexpr OutType SecondsToNanoseconds(       InType a ) noexcept;

} // namespace Time

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

template<typename T>
inline constexpr bool Is_Float_Angle = std::is_same_v<T, Angle::RADIANS<F32>> || std::is_same_v<T, Angle::DEGREES<F32>>;

namespace Util
{

struct Circle
{
    F32 center[2] = {0.0f, 0.0f};
    F32 radius = 1.f;
};

[[nodiscard]] bool IntersectCircles(const Circle& cA, const Circle& cB, float2* pointsOut) noexcept;

[[nodiscard]] size_t GetAlignmentCorrected(const size_t value, const size_t alignment) noexcept;

/// a la Boost
template <typename T, typename... Rest>
void Hash_combine(std::size_t& seed, const T& v, const Rest&... rest) noexcept;

/** Ogre3D
@brief Normalise the selected rotations to be within the +/-180 degree range.
@details The normalise uses a wrap around,
@details so for example a yaw of 360 degrees becomes 0 degrees, and -190 degrees
becomes 170.
@param inputRotation rotation to normalize
@param normYaw If false, the yaw isn't normalized.
@param normPitch If false, the pitch isn't normalized.
@param normRoll If false, the roll isn't normalized.
*/
void Normalize(vec3<Angle::RADIANS_F>& inputRotation,
               bool normYaw = true,
               bool normPitch = true,
               bool normRoll = true) noexcept;

[[nodiscard]] UColour4 ToByteColour(const FColour4& floatColour) noexcept;
[[nodiscard]] UColour3 ToByteColour(const FColour3& floatColour) noexcept;
[[nodiscard]] FColour4 ToFloatColour(const UColour4& byteColour) noexcept;
[[nodiscard]] FColour3 ToFloatColour(const UColour3& byteColour) noexcept;
[[nodiscard]] FColour4 ToFloatColour( const uint4& colour ) noexcept;
[[nodiscard]] FColour3 ToFloatColour( const uint3& colour ) noexcept;

void ToByteColour(const FColour4& floatColour, UColour4& colourOut) noexcept;
void ToByteColour(const FColour3& floatColour, UColour3& colourOut) noexcept;
void ToFloatColour(const UColour4& byteColour, FColour4& colourOut) noexcept;
void ToFloatColour(const UColour3& byteColour, FColour3& colourOut) noexcept;
void ToFloatColour( const uint4& uintColour, FColour4& colourOut ) noexcept;
void ToFloatColour( const uint3& uintColour, FColour3& colourOut ) noexcept;

bool ToDualQuaternion(const mat4<F32>& transform,
                      quatf& rotQuatOut,
                      quatf& transQuatOut);

bool DecomposeMatrix(const mat4<F32>& transform,
                     float3& translationOut,
                     float3& scaleOut,
                     quatf& rotationOut,
                     bool& isUniformScaleOut);

bool DecomposeMatrix(const mat4<F32>& transform,
                     float3& translationOut,
                     float3& scaleOut,
                     vec3<Angle::RADIANS_F>& rotationOut,
                     bool& isUniformScaleOut);

bool DecomposeMatrix(const mat4<F32>& transform,
                     float3& translationOut,
                     float3& scaleOut,
                     vec3<Angle::RADIANS_F>& rotationOut);
                     
bool DecomposeMatrix(const mat4<F32>& transform,
                     float3& translationOut,
                     float3& scaleOut,
                     quatf& rotationOut);

bool DecomposeMatrix(const mat4<F32>& transform,
                     float3& translationOut,
                     float3& scaleOut);

bool DecomposeMatrix(const mat4<F32>& transform,
                     float3& translationOut);

//ref: https://community.khronos.org/t/glsl-packing-a-normal-in-a-single-float/52039/3
// Pack 3 values into 1 float
[[nodiscard]] F32 PACK_VEC3(F32_SNORM x, F32_SNORM y, F32_SNORM z) noexcept;
[[nodiscard]] F32 PACK_VEC3(U8 x, U8 y, U8 z) noexcept;

[[nodiscard]] F32 PACK_VEC3(const vec3<F32_SNORM>& value) noexcept;

[[nodiscard]] U32    PACK_HALF2x16(float2 value);
              void   UNPACK_HALF2x16(U32 src, float2& value);
[[nodiscard]] float2 UNPACK_HALF2x16(U32 src);

[[nodiscard]] U32  PACK_HALF2x16(F32 x, F32 y);
              void UNPACK_HALF2x16(U32 src, F32& x, F32& y);

/// Only convert the range [-1024., 1024.] for accurate results
[[nodiscard]] U16  PACK_HALF1x16(F32 value);
/// Only convert the range [-1024., 1024.] for accurate results
              void UNPACK_HALF1x16(U16 src, F32& value);
/// Only convert the range [-1024., 1024.] for accurate results
[[nodiscard]] F32  UNPACK_HALF1x16(U16 src);

[[nodiscard]] F32 UINT_TO_FLOAT(U32 src);
[[nodiscard]] U32 FLOAT_TO_UINT(F32 src);

[[nodiscard]] F32 INT_TO_FLOAT(I32 src);
[[nodiscard]] I32 FLOAT_TO_INT(F32 src);

[[nodiscard]] U32  PACK_UNORM4x8(const vec4<F32_NORM>& value);
[[nodiscard]] U32  PACK_UNORM4x8(vec4<U8> value);
              void UNPACK_UNORM4x8(U32 src, vec4<F32_NORM>& value);

[[nodiscard]] U32  PACK_UNORM4x8(F32_NORM x, F32_NORM y, F32_NORM z, F32_NORM w);
[[nodiscard]] U32  PACK_UNORM4x8(U8 x, U8 y, U8 z, U8 w);
              void UNPACK_UNORM4x8(U32 src, F32_NORM& x, F32_NORM& y, F32_NORM& z, F32_NORM& w);
              void UNPACK_UNORM4x8(U32 src, U8& x, U8& y, U8& z, U8& w);

[[nodiscard]] vec4<U8>       UNPACK_UNORM4x8_U8(U32 src);
[[nodiscard]] vec4<F32_NORM> UNPACK_UNORM4x8_F32(U32 src);

// UnPack 3 values from 1 float
              void            UNPACK_VEC3(F32 src, F32_SNORM& x, F32_SNORM& y, F32_SNORM& z) noexcept;
              void            UNPACK_VEC3(F32 src, vec3<F32_SNORM>& res) noexcept;
[[nodiscard]] vec3<F32_SNORM> UNPACK_VEC3(F32 src) noexcept;

[[nodiscard]] U32            PACK_11_11_10(const vec3<F32_NORM>& value);
              void           UNPACK_11_11_10(U32 src, vec3<F32_NORM>& res);
[[nodiscard]] vec3<F32_NORM> UNPACK_11_11_10(U32 src);

[[nodiscard]] U32  PACK_11_11_10(F32_NORM x, F32_NORM y, F32_NORM z);
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
