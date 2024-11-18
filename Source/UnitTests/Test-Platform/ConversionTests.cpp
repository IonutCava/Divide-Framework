#include "UnitTests/unitTestCommon.h"

namespace Divide
{

TEST_CASE("Metric Cast", "[conversion_tests]")
{
    constexpr U64 baseValue = 4321u;

    constexpr U64 petaResult  = baseValue * 1e15;
    constexpr U64 teraResult  = baseValue * 1e12;
    constexpr U64 gigaResult  = baseValue * 1e9;
    constexpr U64 megaResult  = baseValue * 1e6;
    constexpr U64 kiloResult  = baseValue * 1e3;
    constexpr U64 hectoResult = baseValue * 1e2;
    constexpr U64 decaResult  = baseValue * 1e1;
    constexpr D64 deciResult  = baseValue / 1e1;
    constexpr D64 centiResult = baseValue / 1e2;
    constexpr D64 milliResult = baseValue / 1e3;
    constexpr D64 microResult = baseValue / 1e6;
    constexpr D64 nanoResult  = baseValue / 1e9;
    constexpr D64 picoResult  = baseValue / 1e12;

    constexpr U64 peta  = Metric::Peta<U64>(baseValue);
    constexpr U64 tera  = Metric::Tera<U64>(baseValue);
    constexpr U64 giga  = Metric::Giga<U64>(baseValue);
    constexpr U64 mega  = Metric::Mega<U64>(baseValue);
    constexpr U64 kilo  = Metric::Kilo<U64>(baseValue);
    constexpr U64 hecto = Metric::Hecto<U64>(baseValue);
    constexpr U64 deca  = Metric::Deca<U64>(baseValue);
    constexpr U64 base  = Metric::Base<U64>(baseValue);
    constexpr D64 deci  = Metric::Deci<D64>(baseValue);
    constexpr D64 centi = Metric::Centi<D64>(baseValue);
    constexpr D64 milli = Metric::Milli<D64>(baseValue);
    constexpr D64 micro = Metric::Micro<D64>(baseValue);
    constexpr D64 nano  = Metric::Nano<D64>(baseValue);
    constexpr D64 pico  = Metric::Pico<D64>(baseValue);

    STATIC_CHECK_EQUAL(base, baseValue);
    STATIC_CHECK_EQUAL(peta, petaResult);
    STATIC_CHECK_EQUAL(tera, teraResult);
    STATIC_CHECK_EQUAL(giga, gigaResult);
    STATIC_CHECK_EQUAL(mega, megaResult);
    STATIC_CHECK_EQUAL(kilo, kiloResult);
    STATIC_CHECK_EQUAL(hecto, hectoResult);
    STATIC_CHECK_EQUAL(deca, decaResult);
    STATIC_CHECK_EQUAL(deci, deciResult);
    STATIC_CHECK_EQUAL(centi, centiResult);
    STATIC_CHECK_EQUAL(milli, milliResult);
    STATIC_CHECK_EQUAL(micro, microResult);
    STATIC_CHECK_EQUAL(nano, nanoResult);
    STATIC_CHECK_EQUAL(pico, picoResult);
}

TEST_CASE( "Time Downcast", "[conversion_tests]" )
{
    constexpr U32 inputSeconds = 4;
    constexpr U32 inputMilliseconds = 5;
    constexpr U32 inputMicroseconds = 6;
    constexpr F32 inputTimeFloat = 0.01666f;
    constexpr D64 inputTimeDouble = 0.01666;
    constexpr U32 microToNanoResult = 6'000u;

    constexpr U32 milliToMicroResult = 5'000u;
    constexpr D64 milliToNanoResult = 5e6;

    constexpr U32 secondsToMilliResult = 4'000u;
    constexpr D64 secondsToMicroResult = 4e6;
    constexpr D64 secondsToNanoResult = 4e9;
    constexpr U64 floatMicroSecondsResult = 16660u;

    constexpr U32 microToNano = Time::MicrosecondsToNanoseconds<U32>(inputMicroseconds);
    constexpr U32 milliToMicro = Time::MillisecondsToMicroseconds<U32>(inputMilliseconds);
    constexpr D64 milliToNano = Time::MillisecondsToNanoseconds<D64>(inputMilliseconds);
    constexpr U32 secondsToMilli = Time::SecondsToMilliseconds<U32>(inputSeconds);
    constexpr D64 secondsToMicro = Time::SecondsToMicroseconds<D64>(inputSeconds);
    constexpr D64 secondsToNano = Time::SecondsToNanoseconds<D64>(inputSeconds);

    constexpr U64 microSecFromFloatResult = Time::SecondsToMicroseconds<U64>(inputTimeFloat); 
    constexpr U64 microSecFromDoubleResult = Time::SecondsToMicroseconds<U64>(inputTimeDouble); 

    STATIC_CHECK_EQUAL(microToNanoResult, microToNano);
    STATIC_CHECK_EQUAL(milliToMicroResult, milliToMicro);
    STATIC_CHECK_COMPARE(milliToNanoResult, milliToNano);
    STATIC_CHECK_COMPARE(secondsToNanoResult, secondsToNano);
    STATIC_CHECK_EQUAL(secondsToMilliResult, secondsToMilli);
    STATIC_CHECK_COMPARE(secondsToMicroResult, secondsToMicro);
    STATIC_CHECK_COMPARE(microSecFromDoubleResult, floatMicroSecondsResult);
    STATIC_CHECK_COMPARE_TOLERANCE(microSecFromFloatResult, floatMicroSecondsResult, 1u);
}

TEST_CASE( "Time Upcast", "[conversion_tests]" )
{
    constexpr U32 secondsResult = 4;
    constexpr U32 millisecondsResult = 5;
    constexpr U32 microsecondsResult = 6;
    constexpr F32 deltaTimeResult = 0.25f;

    constexpr D64 inputNanoToSeconds = 4e9;
    constexpr D64 inputNanoToMilli = 5e6;
    constexpr U32 inputNanoToMicro = 6'000u;

    constexpr D64 inputMicroToSeconds = 4e6;
    constexpr U32 inputMicroToMilli = 5'000u;
    constexpr U32 inputMilliToSeconds = 4'000u;
    constexpr U64 inputDeltaTimeUS = 250000u;

    constexpr U32 nanoToSeconds = Time::NanosecondsToSeconds<U32>(inputNanoToSeconds);
    constexpr U32 nanoToMilli = Time::NanosecondsToMilliseconds<U32>(inputNanoToMilli);
    constexpr U32 nanoToMicro = Time::NanosecondsToMicroseconds<U32>(inputNanoToMicro);

    constexpr U32 microToSeconds = Time::MicrosecondsToSeconds<U32>(inputMicroToSeconds);
    constexpr U32 microToMilli = Time::MicrosecondsToMilliseconds<U32>(inputMicroToMilli);

    constexpr U32 milliToSeconds = Time::MillisecondsToSeconds<U32>(inputMilliToSeconds);

    constexpr F32 deltaTime = Time::MicrosecondsToSeconds<F32>(inputDeltaTimeUS);

    STATIC_CHECK_EQUAL(secondsResult, nanoToSeconds);
    STATIC_CHECK_EQUAL(millisecondsResult, nanoToMilli);
    STATIC_CHECK_EQUAL(microsecondsResult, nanoToMicro);

    STATIC_CHECK_EQUAL(secondsResult, microToSeconds);
    STATIC_CHECK_EQUAL(millisecondsResult, microToMilli);

    STATIC_CHECK_EQUAL(secondsResult, milliToSeconds);

    STATIC_CHECK_COMPARE(deltaTime, deltaTimeResult);
}

TEST_CASE("Data Upcast", "[conversion_tests]")
{
    constexpr U64 testBytes = 3u;

    constexpr U64 KiloByteResult  = testBytes * 1024;
    constexpr U64 MegaByteResult  = KiloByteResult * 1024;
    constexpr U64 GigaByteResult = MegaByteResult * 1024;
    constexpr U64 TeraByteResult  = GigaByteResult * 1024;
    constexpr U64 PetaByteResult  = TeraByteResult * 1024;

    constexpr U64 bytes    = Bytes::Base(testBytes);
    constexpr U64 kiloByte = Bytes::Kilo(bytes);
    constexpr U64 megaByte = Bytes::Mega(bytes);
    constexpr U64 gigaByte = Bytes::Giga(bytes);
    constexpr U64 teraByte = Bytes::Tera(bytes);
    constexpr U64 petaByte = Bytes::Peta(bytes);

    constexpr U64 bytesFactorResult    = testBytes * Bytes::Factor_B;
    constexpr U64 kiloByteFactorResult = testBytes * Bytes::Factor_KB;
    constexpr U64 megaByteFactorResult = testBytes * Bytes::Factor_MB;
    constexpr U64 gigaByteFactorResult = testBytes * Bytes::Factor_GB;
    constexpr U64 teraByteFactorResult = testBytes * Bytes::Factor_TB;
    constexpr U64 petaByteFactorResult = testBytes * Bytes::Factor_PB;

    CHECK_EQUAL(bytes,    testBytes);
    CHECK_EQUAL(kiloByte, KiloByteResult);
    CHECK_EQUAL(megaByte, MegaByteResult);
    CHECK_EQUAL(gigaByte, GigaByteResult);
    CHECK_EQUAL(teraByte, TeraByteResult);
    CHECK_EQUAL(petaByte, PetaByteResult);

    CHECK_EQUAL(bytes,    bytesFactorResult);
    CHECK_EQUAL(kiloByte, kiloByteFactorResult);
    CHECK_EQUAL(megaByte, megaByteFactorResult);
    CHECK_EQUAL(gigaByte, gigaByteFactorResult);
    CHECK_EQUAL(teraByte, teraByteFactorResult);
    CHECK_EQUAL(petaByte, petaByteFactorResult);
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

    constexpr U8 result1U = FLOAT_TO_CHAR_UNORM(input1);
    constexpr F32_NORM result1F = UNORM_CHAR_TO_FLOAT(result1U);

    STATIC_CHECK_TRUE(COMPARE_TOLERANCE(result1F, input1, tolerance));
    STATIC_CHECK_TRUE(COMPARE_TOLERANCE_ACCURATE(result1F, input1, tolerance));

    constexpr I8 result2I = FLOAT_TO_CHAR_SNORM(input2);
    constexpr F32_SNORM result2F = SNORM_CHAR_TO_FLOAT(result2I);
    STATIC_CHECK_TRUE(COMPARE_TOLERANCE(result2F, input2, tolerance));
    STATIC_CHECK_TRUE(COMPARE_TOLERANCE_ACCURATE(result2F, input2, tolerance));

    constexpr U8 result3I = PACKED_FLOAT_TO_CHAR_UNORM(input3);
    constexpr F32_SNORM result3F = UNORM_CHAR_TO_PACKED_FLOAT(result3I);
    STATIC_CHECK_TRUE(COMPARE_TOLERANCE(result3F, input3, tolerance));
    STATIC_CHECK_TRUE(COMPARE_TOLERANCE_ACCURATE(result3F, input3, tolerance));


    STATIC_CHECK_COMPARE(ABS(0.0), 0.0);
    STATIC_CHECK_COMPARE(ABS(0.f), 0.f);
    STATIC_CHECK_COMPARE(ABS(0), 0);
    STATIC_CHECK_COMPARE(ABS(0u), 0u);

    STATIC_CHECK_COMPARE(ABS(-0.5f), 0.5f);
    STATIC_CHECK_COMPARE(ABS(10.5f), 10.5f);

    STATIC_CHECK_COMPARE(ABS(-0.9999), 0.9999);
    STATIC_CHECK_COMPARE(ABS(543.123), 543.123);

    STATIC_CHECK_COMPARE(ABS(-123), 123);
    STATIC_CHECK_COMPARE(ABS(321), 321);

    STATIC_CHECK_COMPARE(ABS(222u), 222u);

    STATIC_CHECK_COMPARE(CEIL(0.0), 0.0);
    STATIC_CHECK_COMPARE(CEIL(0.5), 1.0);
    STATIC_CHECK_COMPARE(CEIL(0.999999), 1.0);
    STATIC_CHECK_COMPARE(CEIL(1),  1);
    STATIC_CHECK_COMPARE(CEIL(123.0f), 123.f);
    STATIC_CHECK_COMPARE(CEIL(123.4f),124.f);
    STATIC_CHECK_COMPARE(CEIL(-0.5), 0.0);
    STATIC_CHECK_COMPARE(CEIL(-0.999999), 0.0);
    STATIC_CHECK_COMPARE(CEIL(-1.f), -1.f);
    STATIC_CHECK_COMPARE(CEIL(-123.f), -123.f);
    STATIC_CHECK_COMPARE(CEIL(-123.4), -123.0);


    STATIC_CHECK_COMPARE(FLOOR(0.0), 0.0);
    STATIC_CHECK_COMPARE(FLOOR(0.5), 0.0);
    STATIC_CHECK_COMPARE(FLOOR(0.999999), 0.0);
    STATIC_CHECK_COMPARE(FLOOR(1), 1);
    STATIC_CHECK_COMPARE(FLOOR(123.0f), 123.f);
    STATIC_CHECK_COMPARE(FLOOR(123.4f), 123.f);
    STATIC_CHECK_COMPARE(FLOOR(-0.5), -1.0);
    STATIC_CHECK_COMPARE(FLOOR(-0.999999), -1.);
    STATIC_CHECK_COMPARE(FLOOR(-1.0), -1.0);
    STATIC_CHECK_COMPARE(FLOOR(-123.0), -123.0);
    STATIC_CHECK_COMPARE(FLOOR(-123.4), -124.0);

    STATIC_CHECK_FALSE(ALMOST_EQUAL_RELATIVE_AND_ABS(5.f, 6.f,  EPSILON_F32, 0.15f));
    STATIC_CHECK_TRUE(ALMOST_EQUAL_RELATIVE_AND_ABS(5.f, 6.f, EPSILON_F32, 0.25f));
    STATIC_CHECK_TRUE(ALMOST_EQUAL_RELATIVE_AND_ABS(5.f, 6.f, 1.f, 0.1f));
    STATIC_CHECK_FALSE(ALMOST_EQUAL_RELATIVE_AND_ABS(5.f, 6.f, 0.9f, 0.1f));

    STATIC_CHECK_FALSE(ALMOST_EQUAL_RELATIVE_AND_ABS(5.0, 6.0, EPSILON_D64, 0.15));
    STATIC_CHECK_TRUE(ALMOST_EQUAL_RELATIVE_AND_ABS(5.0, 6.0, EPSILON_D64, 0.25));
    STATIC_CHECK_TRUE(ALMOST_EQUAL_RELATIVE_AND_ABS(5.0, 6.0, 1.0, 0.1));
    STATIC_CHECK_FALSE(ALMOST_EQUAL_RELATIVE_AND_ABS(5.0, 6.0, 0.90, 0.1));
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
    const F32 result1F  = Util::UNPACK_HALF1x16(result1US);

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
