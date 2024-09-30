#include "UnitTests/unitTestCommon.h"

namespace Divide
{

TEST_CASE( "Time Downcast", "[conversion_tests]" )
{
    constexpr U32 inputSeconds = 4;
    constexpr U32 inputMilliseconds = 5;
    constexpr U32 inputMicroseconds = 6;

    constexpr U32 microToNanoResult = 6'000u;

    constexpr U32 milliToMicroResult = 5'000u;
    constexpr D64 milliToNanoResult = 5e6;

    constexpr U32 secondsToMilliResult = 4'000u;
    constexpr D64 secondsToMicroResult = 4e6;
    constexpr D64 secondsToNanoResult = 4e9;

    constexpr U32 microToNano = Time::MicrosecondsToNanoseconds<U32>(inputMicroseconds);
    constexpr U32 milliToMicro = Time::MillisecondsToMicroseconds<U32>(inputMilliseconds);
    constexpr D64 milliToNano = Time::MillisecondsToNanoseconds<D64>(inputMilliseconds);
    constexpr U32 secondsToMilli = Time::SecondsToMilliseconds<U32>(inputSeconds);
    constexpr D64 secondsToMicro = Time::SecondsToMicroseconds<D64>(inputSeconds);
    constexpr D64 secondsToNano = Time::SecondsToNanoseconds<D64>(inputSeconds);

    CHECK_EQUAL(microToNanoResult, microToNano);
    CHECK_EQUAL(milliToMicroResult, milliToMicro);
    CHECK_TRUE(COMPARE(milliToNanoResult, milliToNano));
    CHECK_EQUAL(secondsToMilliResult, secondsToMilli);
    CHECK_TRUE(COMPARE(secondsToMicroResult, secondsToMicro));
    CHECK_TRUE(COMPARE(secondsToNanoResult, secondsToNano));
}

TEST_CASE( "Time Upcast", "[conversion_tests]" )
{
    constexpr U32 secondsResult = 4;
    constexpr U32 millisecondsResult = 5;
    constexpr U32 microsecondsResult = 6;

    constexpr D64 inputNanoToSeconds = 4e9;
    constexpr D64 inputNanoToMilli = 5e6;
    constexpr U32 inputNanoToMicro = 6'000u;

    constexpr D64 inputMicroToSeconds = 4e6;
    constexpr U32 inputMicroToMilli = 5'000u;
    constexpr U32 inputMilliToSeconds = 4'000u;

    constexpr U32 nanoToSeconds = Time::NanosecondsToSeconds<U32>(inputNanoToSeconds);
    constexpr U32 nanoToMilli = Time::NanosecondsToMilliseconds<U32>(inputNanoToMilli);
    constexpr U32 nanoToMicro = Time::NanosecondsToMicroseconds<U32>(inputNanoToMicro);

    constexpr U32 microToSeconds = Time::MicrosecondsToSeconds<U32>(inputMicroToSeconds);
    constexpr U32 microToMilli = Time::MicrosecondsToMilliseconds<U32>(inputMicroToMilli);

    constexpr U32 milliToSeconds = Time::MillisecondsToSeconds<U32>(inputMilliToSeconds);

    CHECK_EQUAL(secondsResult, nanoToSeconds);
    CHECK_EQUAL(millisecondsResult, nanoToMilli);
    CHECK_EQUAL(microsecondsResult, nanoToMicro);

    CHECK_EQUAL(secondsResult, microToSeconds);
    CHECK_EQUAL(millisecondsResult, microToMilli);

    CHECK_EQUAL(secondsResult, milliToSeconds);
}

TEST_CASE( "MAP Range test", "[conversion_tests]" )
{
    constexpr U32 in_min = 0;
    constexpr U32 in_max = 100;
    constexpr U32 out_min = 0;
    constexpr U32 out_max = 10;
    constexpr U32 in = 20;
    constexpr U32 result = 2;
    CHECK_EQUAL(result, MAP(in, in_min, in_max, out_min, out_max));
}

TEST_CASE( "Mip count test", "conversion_tests" )
{
    CHECK_EQUAL(1u,  MipCount(1u,    1u));
    CHECK_EQUAL(1u,  MipCount(1024u, 0u));
    CHECK_EQUAL(1u,  MipCount(0u,    768u));
    CHECK_EQUAL(11u, MipCount(1600u, 1u));
    CHECK_EQUAL(10u, MipCount(800,   600));
    CHECK_EQUAL(11u, MipCount(1920,  1080));
    CHECK_EQUAL(12u, MipCount(2560,  1080));
    CHECK_EQUAL(12u, MipCount(3840,  2160));
}

TEST_CASE( "Float To Char Conversions", "[conversion_tests]" )
{
    // We don't expect these to match properly, but we still need a decent level of precision
    constexpr F32 tolerance = 0.005f;

    constexpr F32_NORM  input1 =  0.75f;
    constexpr F32_SNORM input2 = -0.66f;
    constexpr F32_SNORM input3 =  0.36f;

    const U8 result1U = FLOAT_TO_CHAR_UNORM(input1);
    const F32_NORM result1F = UNORM_CHAR_TO_FLOAT(result1U);
    CHECK_TRUE(COMPARE_TOLERANCE(result1F, input1, tolerance));

    const I8 result2I = FLOAT_TO_CHAR_SNORM(input2);
    const F32_SNORM result2F = SNORM_CHAR_TO_FLOAT(result2I);
    CHECK_TRUE(COMPARE_TOLERANCE(result2F, input2, tolerance));

    const U8 result3I = PACKED_FLOAT_TO_CHAR_UNORM(input3);
    const F32_SNORM result3F = UNORM_CHAR_TO_PACKED_FLOAT(result3I);
    CHECK_TRUE(COMPARE_TOLERANCE(result3F, input3, tolerance));
}

TEST_CASE( "Vec Packing Tests", "[conversion_tests]" )
{
    // We don't expect these to match properly, but we still need a decent level of precision
    constexpr F32 tolerance = 0.05f;

    const vec2<F32_SNORM> input1{ 0.5f, -0.3f };
    const vec3<F32_SNORM> input2{ 0.55f, -0.1f, 1.0f};
    const vec3<F32_SNORM> input3{ 0.25f, 0.67f, 0.123f};
    const vec4<U8>        input4{ 32, 64, 128, 255};
    const F32             input5{ -1023.99f };
    const U32 result1U = Util::PACK_HALF2x16(input1);
    const F32 result2U = Util::PACK_VEC3(input2);

    const U16 result1US = Util::PACK_HALF1x16(input5);
    const F32 result1F = Util::UNPACK_HALF1x16(result1US);

    const vec2<F32_SNORM> result1V = Util::UNPACK_HALF2x16(result1U);
    const vec3<F32_SNORM> result2V = Util::UNPACK_VEC3(result2U);
    
    CHECK_TRUE(result1V.compare(input1, tolerance));
    CHECK_TRUE(result2V.compare(input2, tolerance));
    CHECK_TRUE(COMPARE_TOLERANCE(result1F, input5, tolerance));

    const U32 result3U = Util::PACK_11_11_10(input3);
    const vec3<F32_SNORM> result3V = Util::UNPACK_11_11_10(result3U);

    CHECK_TRUE(result3V.compare(input3, tolerance));

    const U32 result4U = Util::PACK_UNORM4x8(input4);
    const vec4<U8> result4V = Util::UNPACK_UNORM4x8_U8(result4U);
    CHECK_EQUAL(result4V, input4);
}


} //namespace Divide
