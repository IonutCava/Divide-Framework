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

#ifndef DVD_CORE_MATH_MATH_HELPER_INL_
#define DVD_CORE_MATH_MATH_HELPER_INL_

namespace Divide
{

    namespace customRNG
    {
        namespace detail
        {
            template<typename Engine>
            Engine& getEngine()
            {
                thread_local std::random_device rnddev{};
                thread_local Engine rndeng = Engine( rnddev() );
                return rndeng;
            }
        }  // namespace detail

        template<typename Engine>
        void srand( const U32 seed )
        {
            detail::getEngine<Engine>().seed( seed );
        }

        template<typename Engine>
        void srand()
        {
            std::random_device rnddev{};
            srand<Engine>( rnddev() );
        }

        template<ValidMathType T, typename Engine, typename Distribution>
        T rand( T min, T max )
        {
            return static_cast<T>(Distribution{ min, max }(detail::getEngine<Engine>()));
        }

        template<ValidMathType T, typename Engine, typename Distribution>
        T rand()
        {
            return rand<T, Engine, Distribution>( 0, std::numeric_limits<T>::max() );
        }
    }  // namespace customRNG

    template <ValidMathType T, typename Engine, typename Distribution>
    T Random( T min, T max )
    {
        if ( min > max ) [[unlikely]]
        {
            std::swap( min, max );
        }

        return customRNG::rand<T, Engine, Distribution>( min, max );
    }

    template <ValidMathType T, typename Engine, typename Distribution>
    T Random( T max )
    {
        return Random<T, Engine, Distribution>( max < 0 ? std::numeric_limits<T>::lowest() : 0, max );
    }

    template <ValidMathType T, typename Engine, typename Distribution>
    T Random()
    {
        return Random<T, Engine, Distribution>( std::numeric_limits<T>::max() );
    }

    template<typename Engine>
    void SeedRandom()
    {
        customRNG::srand<Engine>();
    }

    template<typename Engine>
    void SeedRandom( const U32 seed )
    {
        customRNG::srand<Engine>( seed );
    }

    /// Clamps value n between min and max
    template <ValidMathType T>
    constexpr void CLAMP( T& n, const T min, const T max ) noexcept
    {
        n = std::min( std::max( n, min), max);
    }

    template <ValidMathType T>
    constexpr void CLAMP_01( T& n ) noexcept
    {
        return CLAMP( n, static_cast<T>(0), static_cast<T>(1) );
    }

    template <ValidMathType T>
    constexpr T CLAMPED( const T n, const T min, const T max ) noexcept
    {
        T ret = n;
        CLAMP( ret, min, max );
        return ret;
    }

    template <ValidMathType T>
    constexpr T CLAMPED_01( const T n ) noexcept
    {
        return CLAMPED( n, static_cast<T>( 0 ), static_cast<T>( 1 ) );
    }


    template <ValidMathType T>
    constexpr T MAP(const T input, const T in_min, const T in_max, const T out_min, const T out_max, D64& slopeOut ) noexcept
    {
        const D64 diff = in_max > in_min ? to_D64( in_max - in_min ) : EPSILON_D64;
        slopeOut = 1.0 * (out_max - out_min) / diff;
        return static_cast<T>(out_min + slopeOut * (input - in_min));
    }

    template <ValidMathType T>
    constexpr void REMAP( T& input, T in_min, T in_max, T out_min, T out_max, D64& slopeOut ) noexcept
    {
        input = MAP( input, in_min, in_max, out_min, out_max, slopeOut );
    }

    template <ValidMathType T>
    constexpr T SQUARED( T input ) noexcept
    {
        return input * input;
    }

    template<ValidMathType T>
    void CLAMP_IN_RECT( T& inout_x, T& inout_y, T rect_x, T rect_y, T rect_z, T rect_w ) noexcept
    {
        CLAMP( inout_x, rect_x, rect_z + rect_x );
        CLAMP( inout_y, rect_y, rect_w + rect_y );
    }

    template<ValidMathType T>
    void CLAMP_IN_RECT( T& inout_x, T& inout_y, const Rect<T>& rect ) noexcept
    {
        CLAMP_IN_RECT( inout_x, inout_y, rect.x, rect.y, rect.z, rect.w );
    }

    template<ValidMathType T>
    void CLAMP_IN_RECT( T& inout_x, T& inout_y, const vec4<T>& rect ) noexcept
    {
        CLAMP_IN_RECT( inout_x, inout_y, rect.x, rect.y, rect.z, rect.w );
    }

    template <ValidMathType T>
    bool COORDS_IN_RECT( T input_x, T input_y, T rect_x, T rect_y, T rect_z, T rect_w ) noexcept
    {
        return IS_IN_RANGE_INCLUSIVE( input_x, rect_x, rect_z + rect_x ) &&
               IS_IN_RANGE_INCLUSIVE( input_y, rect_y, rect_w + rect_y );
    }

    template<ValidMathType T>
    bool COORDS_IN_RECT( T input_x, T input_y, const Rect<T>& rect ) noexcept
    {
        return COORDS_IN_RECT( input_x, input_y, rect.x, rect.y, rect.z, rect.w );
    }

    template <ValidMathType T>
    bool COORDS_IN_RECT( T input_x, T input_y, const vec4<T>& rect ) noexcept
    {
        return COORDS_IN_RECT( input_x, input_y, rect.x, rect.y, rect.z, rect.w );
    }

    template<ValidMathType T>
    constexpr T roundup( T value, U32 maxb, U32 curb )
    {
        return maxb <= curb
            ? value
            : roundup( ((value - 1) | ((value - 1) >> curb)) + 1, maxb, curb << 1 );
    }

    constexpr U32 nextPOW2( U32 n ) noexcept
    {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;

        return ++n;
    }

    constexpr U32 prevPOW2( U32 n ) noexcept
    {
        n = n | n >> 1;
        n = n | n >> 2;
        n = n | n >> 4;
        n = n | n >> 8;
        n = n | n >> 16;
        return n - (n >> 1);
    }

    template<ValidMathType T>
    constexpr T MipCount(const T width, const T height) noexcept
    {
        if ( width >= T{1} && height >= T{1} )
        {
            return static_cast<T>(std::floor(std::log2f(std::fmaxf(to_F32(width), to_F32(height))))) + T{1};
        }

        return T{1};
    }

    constexpr U32 minSquareMatrixSize( const U32 elementCount ) noexcept
    {
        U32 result = 1u;
        while ( result * result < elementCount )
        {
            ++result;
        }
        return result;
    }

    template <typename T, ValidMathType U> T Lerp    ( const T v1, const T v2, const U t ) noexcept { return v1 * (U{1} - t) + v2 * t; }
    template <typename T, ValidMathType U> T LerpFast( const T v1, const T v2, const U t ) noexcept { return v1 + t * (v2 - v1); }

    template <ValidMathType T, typename U> T Sqrt( const U input )       noexcept { return static_cast<T>(std::sqrt( input )); }
    template <ValidMathType T, typename U> T InvSqrt( const U input )    noexcept { return static_cast<T>(1.0 / Sqrt<D64>(to_D64(input))); }
    template <ValidMathType T, typename U> T InvSqrtFast( const U input) noexcept { return static_cast<T>(1.f / Sqrt<F32>(to_F32(input))); }

    template<> FORCE_INLINE F32 Sqrt( const __m128 input )      noexcept { return _mm_cvtss_f32( _mm_sqrt_ss( input )); }
    template<> FORCE_INLINE F32 InvSqrt(const __m128 input)     noexcept { return 1.f / Sqrt<F32>(input); }
    template<> FORCE_INLINE F32 InvSqrtFast(const __m128 input) noexcept { return _mm_cvtss_f32(_mm_rsqrt_ss(input)); }

    template<> FORCE_INLINE F32 Sqrt(const F32 input)        noexcept { return Sqrt<F32, __m128>(_mm_set_ss(input)); }
    template<> FORCE_INLINE F32 InvSqrt(const F32 input)     noexcept { return InvSqrt<F32, __m128>(_mm_set_ss(input)); }
    template<> FORCE_INLINE F32 InvSqrtFast(const F32 input) noexcept { return InvSqrtFast<F32, __m128>(_mm_set_ss(input)); }

    ///(thx sqrt[-1] and canuckle of opengl.org forums)

    // Helper method to emulate GLSL
    FORCE_INLINE F32 FRACT( const F32 floatValue ) noexcept
    {
        return to_F32( fmod( floatValue, 1.0f ) );
    }

    constexpr I8 FLOAT_TO_CHAR_SNORM( const F32_SNORM value ) noexcept
    {
        assert( value >= -1.f && value <= 1.f );
        return to_I8( (value >= 1.0f ? 127 : (value <= -1.0f ? -127 : to_I32( SIGN( value ) * FLOOR_CONSTEXPR(ABS_CONSTEXPR( value ) * 128.0f ) ))) );
    }

    constexpr F32_SNORM SNORM_CHAR_TO_FLOAT( const I8 value ) noexcept
    {
        return value / 127.f;
    }

    constexpr U8 PACKED_FLOAT_TO_CHAR_UNORM( const F32_SNORM value ) noexcept
    {
        assert( value >= -1.f && value <= 1.f );

        const F32_NORM normValue = (value + 1) * 0.5f;
        return FLOAT_TO_CHAR_UNORM( normValue );
    }

    constexpr F32_SNORM UNORM_CHAR_TO_PACKED_FLOAT( const U8 value ) noexcept
    {
        const F32_NORM normValue = UNORM_CHAR_TO_FLOAT( value );

        return 2 * normValue - 1;
    }

    constexpr F32_NORM UNORM_CHAR_TO_FLOAT( const U8 value ) noexcept
    {
        return value == 255 ? 1.0f : value / 256.0f;
    }

    constexpr U8 FLOAT_TO_CHAR_UNORM( const F32_NORM value ) noexcept
    {
        assert( value >= 0.f && value <= 1.f );
        return to_U8( (value >= 1.0f ? 255 : (value <= 0.0f ? 0 : to_I32(FLOOR_CONSTEXPR( value * 256.0f ) ))) );
    }

    namespace Util
    {
        // Pack 3 values into 1 float
        inline F32 PACK_VEC3( const F32_SNORM x, const F32_SNORM y, const F32_SNORM z ) noexcept
        {
            assert( x >= -1.0f && x <= 1.0f );
            assert( y >= -1.0f && y <= 1.0f );
            assert( z >= -1.0f && z <= 1.0f );

            //Scale and bias
            return PACK_VEC3( to_U8( ((x + 1.0f) * 0.5f) * 255.0f ),
                              to_U8( ((y + 1.0f) * 0.5f) * 255.0f ),
                              to_U8( ((z + 1.0f) * 0.5f) * 255.0f ) );
        }

        inline F32 PACK_VEC3( const U8 x, const U8 y, const U8 z ) noexcept
        {
            const U32 packed = x << 16 | y << 8 | z;
            return to_F32( to_D64( packed ) / to_D64( 1 << 24 ) );
        }

        // UnPack 3 values from 1 float
        inline void UNPACK_VEC3( const F32 src, F32_SNORM& x, F32_SNORM& y, F32_SNORM& z ) noexcept
        {
            x = FRACT( src );
            y = FRACT( src * 256.0f );
            z = FRACT( src * 65536.0f );

            // Unpack to the -1..1 range
            x = x * 2.0f - 1.0f;
            y = y * 2.0f - 1.0f;
            z = z * 2.0f - 1.0f;
        }
    } //namespace Util

    namespace Angle
    {
        template <typename T> constexpr DEGREES<T> to_VerticalFoV(   const DEGREES<T> horizontalFoV, const D64 aspectRatio ) noexcept { return to_DEGREES<T>(RADIANS<T>(2 * std::atan( std::tan( to_RADIANS<T>( horizontalFoV ) * 0.5f ) ) / aspectRatio)); }
        template <typename T> constexpr DEGREES<T> to_HorizontalFoV( const DEGREES<T> verticalFoV,   const D64 aspectRatio ) noexcept { return to_DEGREES<T>(RADIANS<T>(2 * std::atan( std::tan( to_RADIANS<T>( verticalFoV )   * 0.5f ) ) * aspectRatio)); }

        template <typename T> inline       vec4<RADIANS<T>> to_RADIANS( const vec4<DEGREES<T>>& angle ) noexcept { return { to_RADIANS(angle.xyz), to_RADIANS(angle.w) }; }
        template <typename T> inline       vec3<RADIANS<T>> to_RADIANS( const vec3<DEGREES<T>>& angle ) noexcept { return { to_RADIANS(angle.xy),  to_RADIANS(angle.z) }; }
        template <typename T> inline       vec2<RADIANS<T>> to_RADIANS( const vec2<DEGREES<T>>& angle ) noexcept { return { to_RADIANS(angle.x),   to_RADIANS(angle.y) }; }
        template <typename T> FORCE_INLINE RADIANS<T>       to_RADIANS( const DEGREES<T> angle )        noexcept { return RADIANS<T>{angle.value * M_PI_DIV_180};         }
        template <>           FORCE_INLINE RADIANS_F        to_RADIANS( const DEGREES_F  angle )        noexcept { return RADIANS_F{angle.value * M_PI_DIV_180_f};        }

        template <typename T> inline       vec4<DEGREES<T>> to_DEGREES( const vec4<RADIANS<T>>& angle ) noexcept { return { to_DEGREES(angle.xyz), to_DEGREES(angle.w) }; }
        template <typename T> inline       vec3<DEGREES<T>> to_DEGREES( const vec3<RADIANS<T>>& angle ) noexcept { return { to_DEGREES(angle.xy),  to_DEGREES(angle.z) }; }
        template <typename T> inline       vec2<DEGREES<T>> to_DEGREES( const vec2<RADIANS<T>>& angle ) noexcept { return { to_DEGREES(angle.x),   to_DEGREES(angle.y) }; }
        template <typename T> FORCE_INLINE DEGREES<T>       to_DEGREES( const RADIANS<T> angle )        noexcept { return DEGREES<T>(angle.value * M_180_DIV_PI);         }
        template <>           FORCE_INLINE DEGREES_F        to_DEGREES( const RADIANS_F  angle )        noexcept { return DEGREES_F(angle.value * M_180_DIV_PI_f);        }

    }  // namespace Angle

    namespace Metric
    {
        template <typename OutType, typename InType> constexpr OutType Peta(  const InType a ) { return static_cast<OutType>(multiply( a, 1e15 )); }
        template <typename OutType, typename InType> constexpr OutType Tera(  const InType a ) { return static_cast<OutType>(multiply( a, 1e12 )); }
        template <typename OutType, typename InType> constexpr OutType Giga(  const InType a ) { return static_cast<OutType>(multiply( a, 1e9  )); }
        template <typename OutType, typename InType> constexpr OutType Mega(  const InType a ) { return static_cast<OutType>(multiply( a, 1e6  )); }
        template <typename OutType, typename InType> constexpr OutType Kilo(  const InType a ) { return static_cast<OutType>(multiply( a, 1e3  )); }
        template <typename OutType, typename InType> constexpr OutType Hecto( const InType a ) { return static_cast<OutType>(multiply( a, 1e2  )); }
        template <typename OutType, typename InType> constexpr OutType Deca(  const InType a ) { return static_cast<OutType>(multiply( a, 1e1  )); }
        template <typename OutType, typename InType> constexpr OutType Base(  const InType a ) { return static_cast<OutType>(multiply( a, 1e0  )); }
        template <typename OutType, typename InType> constexpr OutType Deci(  const InType a ) { return static_cast<OutType>(  divide( a, 1e1  )); }
        template <typename OutType, typename InType> constexpr OutType Centi( const InType a ) { return static_cast<OutType>(  divide( a, 1e2  )); }
        template <typename OutType, typename InType> constexpr OutType Milli( const InType a ) { return static_cast<OutType>(  divide( a, 1e3  )); }
        template <typename OutType, typename InType> constexpr OutType Micro( const InType a ) { return static_cast<OutType>(  divide( a, 1e6  )); }
        template <typename OutType, typename InType> constexpr OutType Nano(  const InType a ) { return static_cast<OutType>(  divide( a, 1e9  )); }
        template <typename OutType, typename InType> constexpr OutType Pico(  const InType a ) { return static_cast<OutType>(  divide( a, 1e12 )); }
    }  // namespace Metric

    namespace Bytes
    {
        constexpr size_t Factor_B  = 1u;
        constexpr size_t Factor_KB = Factor_B  * 1024u;
        constexpr size_t Factor_MB = Factor_KB * 1024u;
        constexpr size_t Factor_GB = Factor_MB * 1024u;
        constexpr size_t Factor_TB = Factor_GB * 1024u;
        constexpr size_t Factor_PB = Factor_TB * 1024u;

        template <typename OutType, typename InType> constexpr OutType Peta( const InType a ) { return static_cast<OutType>(multiply(a, Factor_PB)); }
        template <typename OutType, typename InType> constexpr OutType Tera( const InType a ) { return static_cast<OutType>(multiply(a, Factor_TB)); }
        template <typename OutType, typename InType> constexpr OutType Giga( const InType a ) { return static_cast<OutType>(multiply(a, Factor_GB)); }
        template <typename OutType, typename InType> constexpr OutType Mega( const InType a ) { return static_cast<OutType>(multiply(a, Factor_MB)); }
        template <typename OutType, typename InType> constexpr OutType Kilo( const InType a ) { return static_cast<OutType>(multiply(a, Factor_KB)); }
        template <typename OutType, typename InType> constexpr OutType Base( const InType a ) { return static_cast<OutType>(multiply(a, Factor_B )); }
    } //namespace Bytes

    namespace Time
    {
        template <typename OutType, typename InType> constexpr OutType Hours(        const InType a ) { return Metric::Base<OutType, InType>(a); }
        template <typename OutType, typename InType> constexpr OutType Minutes(      const InType a ) { return Metric::Base<OutType, InType>(a); }
        template <typename OutType, typename InType> constexpr OutType Seconds(      const InType a ) { return Metric::Base<OutType, InType>(a); }
        template <typename OutType, typename InType> constexpr OutType Milliseconds( const InType a ) { return Metric::Base<OutType, InType>(a); }
        template <typename OutType, typename InType> constexpr OutType Microseconds( const InType a ) { return Metric::Base<OutType, InType>(a); }
        template <typename OutType, typename InType> constexpr OutType Nanoseconds(  const InType a ) { return Metric::Base<OutType, InType>(a); }

        template <typename OutType, typename InType> constexpr OutType NanosecondsToSeconds(       const InType a ) noexcept { return Metric::Nano<OutType, InType>( a );  }
        template <typename OutType, typename InType> constexpr OutType NanosecondsToMilliseconds(  const InType a ) noexcept { return Metric::Micro<OutType, InType>( a ); }
        template <typename OutType, typename InType> constexpr OutType NanosecondsToMicroseconds(  const InType a ) noexcept { return Metric::Milli<OutType, InType>( a ); }
        template <typename OutType, typename InType> constexpr OutType MicrosecondsToSeconds(      const InType a ) noexcept { return Metric::Micro<OutType, InType>( a ); }
        template <typename OutType, typename InType> constexpr OutType MicrosecondsToMilliseconds( const InType a ) noexcept { return Metric::Milli<OutType, InType>( a ); }
        template <typename OutType, typename InType> constexpr OutType MicrosecondsToNanoseconds(  const InType a ) noexcept { return Metric::Kilo<OutType, InType>( a );  }
        template <typename OutType, typename InType> constexpr OutType MillisecondsToSeconds(      const InType a ) noexcept { return Metric::Milli<OutType, InType>( a ); }
        template <typename OutType, typename InType> constexpr OutType MillisecondsToMicroseconds( const InType a ) noexcept { return Metric::Kilo<OutType, InType>( a );  }
        template <typename OutType, typename InType> constexpr OutType MillisecondsToNanoseconds(  const InType a ) noexcept { return Metric::Mega<OutType, InType>( a );  }
        template <typename OutType, typename InType> constexpr OutType SecondsToMilliseconds(      const InType a ) noexcept { return Metric::Kilo<OutType, InType>( a );  }
        template <typename OutType, typename InType> constexpr OutType SecondsToMicroseconds(      const InType a ) noexcept { return Metric::Mega<OutType, InType>( a );  }
        template <typename OutType, typename InType> constexpr OutType SecondsToNanoseconds(       const InType a ) noexcept { return Metric::Giga<OutType, InType>( a );  }

    }  // namespace Time

    namespace Util
    {
        FORCE_INLINE size_t GetAlignmentCorrected( const size_t value, const size_t alignment ) noexcept
        {
            return (value % alignment == 0u)
                                      ? value
                                      : ((value + alignment - 1u) / alignment) * alignment;
        }

        /// a la Boost
        template<std::unsigned_integral T, typename... Rest>
        FORCE_INLINE void Hash_combine( std::size_t& seed, const T& v, const Rest&... rest ) noexcept
        {
            seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            (Hash_combine( seed, rest ), ...);
        }

        template<typename T, typename... Rest>
        FORCE_INLINE void Hash_combine(std::size_t& seed, const T& v, const Rest&... rest) noexcept
        {
            Hash_combine(seed, std::hash<T>{}(v), FWD(rest)...);
        };

    }  // namespace Util
}  // namespace Divide

#endif  //DVD_CORE_MATH_MATH_HELPER_INL_
