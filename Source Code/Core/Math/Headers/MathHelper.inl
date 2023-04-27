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

#ifndef _CORE_MATH_MATH_HELPER_INL_
#define _CORE_MATH_MATH_HELPER_INL_

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

        template<typename T, typename Engine, typename Distribution> requires std::is_arithmetic_v<T> && std::is_fundamental_v<T>
        T rand( T min, T max )
        {
            return static_cast<T>(Distribution{ min, max }(detail::getEngine<Engine>()));
        }

        template<typename T, typename Engine, typename Distribution> requires std::is_arithmetic_v<T>
        T rand()
        {
            return rand<T, Engine, Distribution>( 0, std::numeric_limits<T>::max() );
        }
    }  // namespace customRNG

    template <typename T, typename Engine, typename Distribution> requires std::is_arithmetic_v<T>
    T Random( T min, T max )
    {
        if ( min > max ) [[unlikely]]
        {
            std::swap( min, max );
        }

        return customRNG::rand<T, Engine, Distribution>( min, max );
    }

    template <typename T, typename Engine, typename Distribution> requires std::is_arithmetic_v<T>
    T Random( T max )
    {
        return Random<T, Engine, Distribution>( max < 0 ? std::numeric_limits<T>::lowest() : 0, max );
    }

    template <typename T, typename Engine, typename Distribution> requires std::is_arithmetic_v<T>
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
    template <typename T> requires std::is_arithmetic<T>::value
    constexpr void CLAMP( T& n, const T min, const T max ) noexcept
    {
        n = std::min( std::max( n, min), max);
    }

    template <typename T>
    constexpr void CLAMP_01( T& n ) noexcept
    {
        return CLAMP( n, static_cast<T>(0), static_cast<T>(1) );
    }

    template <typename T> requires std::is_arithmetic<T>::value
    constexpr T CLAMPED( const T& n, const T min, const T max ) noexcept
    {
        T ret = n;
        CLAMP( ret, min, max );
        return ret;
    }

    template <typename T>
    constexpr T CLAMPED_01( const T& n ) noexcept
    {
        return CLAMPED( n, static_cast<T>( 0 ), static_cast<T>( 1 ) );
    }

    template <typename T> requires std::is_arithmetic<T>::value
    constexpr T MAP( T input, const T in_min, const T in_max, const T out_min, const T out_max, D64& slopeOut ) noexcept
    {
        const D64 diff = in_max > in_min ? to_D64( in_max - in_min ) : EPSILON_D64;
        slopeOut = 1.0 * (out_max - out_min) / diff;
        return static_cast<T>(out_min + slopeOut * (input - in_min));
    }

    template <typename T>
    constexpr void REMAP( T& input, T in_min, T in_max, T out_min, T out_max, D64& slopeOut ) noexcept
    {
        input = MAP( input, in_min, in_max, out_min, out_max, slopeOut );
    }

    template <typename T>
        requires std::is_arithmetic<T>::value
    constexpr T SQUARED( T input ) noexcept
    {
        return input * input;
    }

    template<typename T>
    void CLAMP_IN_RECT( T& inout_x, T& inout_y, T rect_x, T rect_y, T rect_z, T rect_w ) noexcept
    {
        CLAMP( inout_x, rect_x, rect_z + rect_x );
        CLAMP( inout_y, rect_y, rect_w + rect_y );
    }

    template<typename T>
    void CLAMP_IN_RECT( T& inout_x, T& inout_y, const Rect<T>& rect ) noexcept
    {
        CLAMP_IN_RECT( inout_x, inout_y, rect.x, rect.y, rect.z, rect.w );
    }

    template<typename T>
    void CLAMP_IN_RECT( T& inout_x, T& inout_y, const vec4<T>& rect ) noexcept
    {
        CLAMP_IN_RECT( inout_x, inout_y, rect.x, rect.y, rect.z, rect.w );
    }

    template <typename T>
    bool COORDS_IN_RECT( T input_x, T input_y, T rect_x, T rect_y, T rect_z, T rect_w ) noexcept
    {
        return IS_IN_RANGE_INCLUSIVE( input_x, rect_x, rect_z + rect_x ) &&
            IS_IN_RANGE_INCLUSIVE( input_y, rect_y, rect_w + rect_y );
    }

    template<typename T>
    bool COORDS_IN_RECT( T input_x, T input_y, const Rect<T>& rect ) noexcept
    {
        return COORDS_IN_RECT( input_x, input_y, rect.x, rect.y, rect.z, rect.w );
    }

    template <typename T>
    bool COORDS_IN_RECT( T input_x, T input_y, const vec4<T>& rect ) noexcept
    {
        return COORDS_IN_RECT( input_x, input_y, rect.x, rect.y, rect.z, rect.w );
    }

    template<typename T> requires std::is_integral<T>::value&& std::is_unsigned<T>::value
    constexpr T roundup( T value, unsigned maxb = sizeof( T ) * CHAR_BIT, unsigned curb = 1 )
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

    constexpr U32 minSquareMatrixSize( const U32 elementCount ) noexcept
    {
        U32 result = 1u;
        while ( result * result < elementCount )
        {
            ++result;
        }
        return result;
    }

    template <typename T, typename U>
    T Lerp( const T v1, const T v2, const U t ) noexcept
    {
        return v1 * (static_cast<U>(1) - t) + v2 * t;
    }

    template <typename T, typename U>
    T FastLerp( const T v1, const T v2, const U t ) noexcept
    {
        return v1 + t * (v2 - v1);
    }

    template <typename T>
    T Sqrt( const T input ) noexcept
    {
        return static_cast<T>(std::sqrt( input ));
    }

    template <typename T, typename U>
    T Sqrt( const U input ) noexcept
    {
        return static_cast<T>(std::sqrt( input ));
    }

    template <>
    inline F32 Sqrt( const __m128 input ) noexcept
    {
        return _mm_cvtss_f32( _mm_sqrt_ss( input ) );
    }
    ///(thx sqrt[-1] and canuckle of opengl.org forums)

    // Helper method to emulate GLSL
    inline F32 FRACT( const F32 floatValue ) noexcept
    {
        return to_F32( fmod( floatValue, 1.0f ) );
    }

    constexpr I8 FLOAT_TO_CHAR_SNORM( const F32_SNORM value ) noexcept
    {
        assert( value >= -1.f && value <= 1.f );
        return to_I8( (value >= 1.0f ? 127 : (value <= -1.0f ? -127 : to_I32( SIGN( value ) * std::floor( std::abs( value ) * 128.0f ) ))) );
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
        return to_U8( (value >= 1.0f ? 255 : (value <= 0.0f ? 0 : to_I32( std::floor( value * 256.0f ) ))) );
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

        template <typename T>
        constexpr DEGREES<T> to_VerticalFoV( const DEGREES<T> horizontalFoV, D64 aspectRatio ) noexcept
        {
            return static_cast<DEGREES<T>>(
                to_DEGREES( 2 * std::atan( std::tan( Angle::to_RADIANS( horizontalFoV ) * 0.5f ) / aspectRatio ) )
                );
        }

        template <typename T>
        constexpr DEGREES<T> to_HorizontalFoV( const DEGREES<T> verticalFoV, D64 aspectRatio ) noexcept
        {
            return static_cast<DEGREES<T>>(
                to_DEGREES( 2 * std::atan( std::tan( Angle::to_RADIANS( verticalFoV ) * 0.5f ) ) * aspectRatio )
                );
        }

        template <typename T>
        constexpr RADIANS<T> to_RADIANS( const DEGREES<T> angle ) noexcept
        {
            return static_cast<RADIANS<T>>(angle * M_PIDIV180);
        }

        template <>
        constexpr RADIANS<F32> to_RADIANS( const DEGREES<F32> angle ) noexcept
        {
            return static_cast<RADIANS<F32>>(angle * M_PIDIV180_f);
        }

        template <typename T>
        constexpr DEGREES<T> to_DEGREES( const RADIANS<T> angle ) noexcept
        {
            return static_cast<DEGREES<T>>(angle * M_180DIVPI);
        }

        template <>
        constexpr DEGREES<F32> to_DEGREES( const RADIANS<F32> angle ) noexcept
        {
            return static_cast<DEGREES<F32>>(angle * M_180DIVPI_f);
        }

        template <typename T>
        constexpr vec2<RADIANS<T>> to_RADIANS( const vec2<DEGREES<T>> angle ) noexcept
        {
            return vec2<RADIANS<T>>( angle * M_PIDIV180 );
        }

        template <typename T>
        constexpr vec2<DEGREES<T>> to_DEGREES( const vec2<RADIANS<T>> angle ) noexcept
        {
            return vec2<RADIANS<T>>( angle * M_180DIVPI );
        }

        template <typename T>
        constexpr vec3<RADIANS<T>> to_RADIANS( const vec3<DEGREES<T>>& angle ) noexcept
        {
            return vec3<RADIANS<T>>( angle * M_PIDIV180 );
        }

        template <typename T>
        constexpr vec3<DEGREES<T>> to_DEGREES( const vec3<RADIANS<T>>& angle ) noexcept
        {
            return vec3<DEGREES<T>>( angle * M_180DIVPI );
        }

        template <typename T>
        constexpr vec4<RADIANS<T>> to_RADIANS( const vec4<DEGREES<T>>& angle ) noexcept
        {
            return vec4<RADIANS<T>>( angle * M_PIDIV180 );
        }

        template <typename T>
        constexpr vec4<DEGREES<T>> to_DEGREES( const vec4<RADIANS<T>>& angle ) noexcept
        {
            return vec4<DEGREES<T>>( angle * M_180DIVPI );
        }

        /// Return the radian equivalent of the given degree value
        template <typename T>
        constexpr T DegreesToRadians( const T angleDegrees ) noexcept
        {
            return to_RADIANS( angleDegrees );
        }

        /// Return the degree equivalent of the given radian value
        template <typename T>
        constexpr T RadiansToDegrees( const T angleRadians ) noexcept
        {
            return to_DEGREES( angleRadians );
        }
        /// Returns the specified value. Used only for emphasis
        template <typename T>
        constexpr T Degrees( const T degrees ) noexcept
        {
            return degrees;
        }

        /// Returns the specified value. Used only for emphasis
        template <typename T>
        constexpr T Radians( const T radians ) noexcept
        {
            return radians;
        }

    }  // namespace Angle

    namespace Metric
    {

        /// Base value * 1000000000000
        template <typename T>
        constexpr T Tera( const T a )
        {
            return Tera<T, T>( a );
        }

        /// Base value * 1000000000
        template <typename T>
        constexpr T Giga( const T a )
        {
            return Giga<T, T>( a );
        }

        /// Base value * 1000000
        template <typename T>
        constexpr T Mega( const T a )
        {
            return Mega<T, T>( a );
        }

        /// Base value * 1000
        template <typename T>
        constexpr T Kilo( const T a )
        {
            return Kilo<T, T>( a );
        }

        /// Base value * 100
        template <typename T>
        constexpr T Hecto( const T a )
        {
            return Hecto<T, T>( a );
        }

        /// Base value * 10
        template <typename T>
        constexpr T Deca( const T a )
        {
            return Deca<T, T>( a );
        }

        /// Base value
        template <typename T>
        constexpr T Base( const T a )
        {
            return Base<T, T>( a );
        }

        /// Base value * 0.1
        template <typename T>
        constexpr T Deci( const T a )
        {
            return Deci<T, T>( a );
        }

        /// Base value * 0.01
        template <typename T>
        constexpr T Centi( const T a )
        {
            return Centi<T, T>( a );
        }

        /// Base value * 0.001
        template <typename T>
        constexpr T Milli( const T a )
        {
            return Milli<T, T>( a );
        }

        /// Base value * 0.000001
        template <typename T>
        constexpr T Micro( const T a )
        {
            return Micro<T, T>( a );
        }

        /// Base value * 0.000000001
        template <typename T>
        constexpr T Nano( const T a )
        {
            return Nano<T, T>( a );
        }

        /// Base value * 0.000000000001
        template <typename T>
        constexpr T Pico( const T a )
        {
            return Pico<T, T>( a );
        }


        template <typename T, typename U>
        constexpr T Tera( const U a )
        {
            return static_cast<T>(multiply( a, 1'000'000'000'000 ));
        }

        template <typename T, typename U>
        constexpr T Giga( const U a )
        {
            return static_cast<T>(multiply( a, 1'000'000'000 ));
        }

        template <typename T, typename U>
        constexpr T Mega( const U a )
        {
            return static_cast<T>(multiply( a, 1'000'000 ));
        }

        template <typename T, typename U>
        constexpr T Kilo( const U a )
        {
            return static_cast<T>(multiply( a, 1'000 ));
        }

        template <typename T, typename U>
        constexpr T Hecto( const U a )
        {
            return static_cast<T>(multiply( a, 100 ));
        }

        template <typename T, typename U>
        constexpr T Deca( const U a )
        {
            return static_cast<T>(multiply( a, 10 ));
        }

        template <typename T, typename U>
        constexpr T Base( const U a )
        {
            return static_cast<T>(a);
        }

        template <typename T, typename U>
        constexpr T Deci( const U a )
        {
            return static_cast<T>(divide( a, 10.0 ));
        }

        template <typename T, typename U>
        constexpr T Centi( const U a )
        {
            return static_cast<T>(divide( a, 100.0 ));
        }

        template <typename T, typename U>
        constexpr T Milli( const U a )
        {
            return static_cast<T>(divide( a, 1000.0 ));
        }

        template <typename T, typename U>
        constexpr T Micro( const U a )
        {
            return static_cast<T>(divide( a, 1e6 ));
        }

        template <typename T, typename U>
        constexpr T Nano( const U a )
        {
            return static_cast<T>(divide( a, 1e9 ));
        }

        template <typename T, typename U>
        constexpr T Pico( const U a )
        {
            return static_cast<T>(divide( a, 1e12 ));
        }

    }  // namespace Metric

    namespace Time
    {

        template <typename T>
        constexpr T Seconds( const T a )
        {
            return a;
        }

        template <typename T>
        constexpr T Milliseconds( const T a )
        {
            return a;
        }

        template <typename T>
        constexpr T Microseconds( const T a )
        {
            return a;
        }

        template <typename T>
        constexpr T Nanoseconds( const T a )
        {
            return a;
        }

        template <typename T, typename U>
        constexpr T Seconds( const U a )
        {
            return static_cast<T>(a);
        }

        template <typename T, typename U>
        constexpr T Milliseconds( const U a )
        {
            return static_cast<T>(a);
        }

        template <typename T, typename U>
        constexpr T Microseconds( const U a )
        {
            return static_cast<T>(a);
        }

        template <typename T, typename U>
        constexpr T Nanoseconds( const U a )
        {
            return static_cast<T>(a);
        }

        template <typename T, typename U>
        constexpr T NanosecondsToSeconds( const U a ) noexcept
        {
            return Metric::Nano<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T NanosecondsToMilliseconds( const U a ) noexcept
        {
            return Metric::Micro<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T NanosecondsToMicroseconds( const U a ) noexcept
        {
            return Metric::Milli<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T MicrosecondsToSeconds( const U a ) noexcept
        {
            return Metric::Micro<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T MicrosecondsToMilliseconds( const U a ) noexcept
        {
            return Metric::Milli<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T MicrosecondsToNanoseconds( const U a ) noexcept
        {
            return Metric::Kilo<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T MillisecondsToSeconds( const U a ) noexcept
        {
            return Metric::Milli<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T MillisecondsToMicroseconds( const U a ) noexcept
        {
            return Metric::Kilo<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T MillisecondsToNanoseconds( const U a ) noexcept
        {
            return Metric::Mega<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T SecondsToMilliseconds( const U a ) noexcept
        {
            return Metric::Kilo<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T SecondsToMicroseconds( const U a ) noexcept
        {
            return Metric::Mega<T, U>( a );
        }

        template <typename T, typename U>
        constexpr T SecondsToNanoseconds( const U a ) noexcept
        {
            return Metric::Giga<T, U>( a );
        }

    }  // namespace Time

    namespace Util
    {

        FORCE_INLINE size_t GetAlignmentCorrected( const size_t value, const size_t alignment ) noexcept
        {
            return value % alignment == 0u
                                      ? value
                                      : ((value + alignment - 1u) / alignment) * alignment;
        }

        /// a la Boost
        template<typename T, typename... Rest>
        FORCE_INLINE void Hash_combine( std::size_t& seed, const T& v, const Rest&... rest ) noexcept
        {
            seed ^= std::hash<T>{}(v)+0x9e3779b9 + (seed << 6) + (seed >> 2);
            (Hash_combine( seed, rest ), ...);
        };

        template<typename... Rest>
        FORCE_INLINE void Hash_combine( size_t& seed, const size_t& v, const Rest&... rest ) noexcept
        {
            seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            (Hash_combine( seed, rest ), ...);
        }

        template <typename TargetType, typename SourceType>
        TargetType ConvertData( const SourceType& data )
        {
            TargetType targetValue;
            std::istringstream iss( data );
            iss >> targetValue;
            DIVIDE_ASSERT( !iss.fail(), "Util::convertData error : invalid conversion!" );
            return targetValue;
        }

        template<class FwdIt, class Compare>
        void InsertionSort( FwdIt first, FwdIt last, Compare cmp )
        {
            for ( auto it = first; it != last; ++it )
            {
                auto const insertion = eastl::upper_bound( first, it, *it, cmp );
                eastl::rotate( insertion, it, eastl::next( it ) );
            }
        }

    }  // namespace Util
}  // namespace Divide

#endif  //_CORE_MATH_MATH_HELPER_INL_