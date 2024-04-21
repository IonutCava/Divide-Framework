#include "UnitTests/unitTestCommon.h"

namespace Divide
{

TEST_CASE( "Vec Size Tests", "[math_vectors_test]" )
{   
    const vec2<I8>  a1;
    const vec2<U8>  a2;
    const vec2<I16> a3;
    const vec2<U16> a4;
    const vec2<I32> a5;
    const vec2<U32> a6;
    const vec2<I64> a7;
    const vec2<U64> a8;
    const vec2<F32> a9;
    const vec2<D64> a10;

    CHECK_EQUAL(sizeof(a5), (32 * 2) / 8);

    CHECK_EQUAL(sizeof(a1),  sizeof(I8)  * 2);
    CHECK_EQUAL(sizeof(a2),  sizeof(U8)  * 2);
    CHECK_EQUAL(sizeof(a3),  sizeof(I16) * 2);
    CHECK_EQUAL(sizeof(a4),  sizeof(U16) * 2);
    CHECK_EQUAL(sizeof(a5),  sizeof(I32) * 2);
    CHECK_EQUAL(sizeof(a6),  sizeof(U32) * 2);
    CHECK_EQUAL(sizeof(a7),  sizeof(I64) * 2);
    CHECK_EQUAL(sizeof(a8),  sizeof(U64) * 2);
    CHECK_EQUAL(sizeof(a9),  sizeof(F32) * 2);
    CHECK_EQUAL(sizeof(a10), sizeof(D64) * 2);
    CHECK_EQUAL(sizeof(a10), sizeof(a9)  * 2);

    const vec3<I8>  b1;
    const vec3<U8>  b2;
    const vec3<I16> b3;
    const vec3<U16> b4;
    const vec3<I32> b5;
    const vec3<U32> b6;
    const vec3<I64> b7;
    const vec3<U64> b8;
    const vec3<F32> b9;
    const vec3<D64> b10;

    CHECK_EQUAL(sizeof(b5), (32 * 3) / 8);

    CHECK_EQUAL(sizeof(b1),  sizeof(I8)  * 3);
    CHECK_EQUAL(sizeof(b2),  sizeof(U8)  * 3);
    CHECK_EQUAL(sizeof(b3),  sizeof(I16) * 3);
    CHECK_EQUAL(sizeof(b4),  sizeof(U16) * 3);
    CHECK_EQUAL(sizeof(b5),  sizeof(I32) * 3);
    CHECK_EQUAL(sizeof(b6),  sizeof(U32) * 3);
    CHECK_EQUAL(sizeof(b7),  sizeof(I64) * 3);
    CHECK_EQUAL(sizeof(b8),  sizeof(U64) * 3);
    CHECK_EQUAL(sizeof(b9),  sizeof(F32) * 3);
    CHECK_EQUAL(sizeof(b10), sizeof(D64) * 3);
    CHECK_EQUAL(sizeof(b10), sizeof(b9)  * 2);

    const vec4<I8>  c1;
    const vec4<U8>  c2;
    const vec4<I16> c3;
    const vec4<U16> c4;
    const vec4<I32> c5;
    const vec4<U32> c6;
    const vec4<I64> c7;
    const vec4<U64> c8;
    const vec4<F32> c9;
    const vec4<D64> c10;

    CHECK_EQUAL(sizeof(c5), (32 * 4) / 8);

    CHECK_EQUAL(sizeof(c1),  sizeof(I8)  * 4);
    CHECK_EQUAL(sizeof(c2),  sizeof(U8)  * 4);
    CHECK_EQUAL(sizeof(c3),  sizeof(I16) * 4);
    CHECK_EQUAL(sizeof(c4),  sizeof(U16) * 4);
    CHECK_EQUAL(sizeof(c5),  sizeof(I32) * 4);
    CHECK_EQUAL(sizeof(c6),  sizeof(U32) * 4);
    CHECK_EQUAL(sizeof(c7),  sizeof(I64) * 4);
    CHECK_EQUAL(sizeof(c8),  sizeof(U64) * 4);
    CHECK_EQUAL(sizeof(c9),  sizeof(F32) * 4);
    CHECK_EQUAL(sizeof(c10), sizeof(D64) * 4);
    CHECK_EQUAL(sizeof(c10), sizeof(c9)  * 2);
}

TEST_CASE( "Vec Constructor Conversions", "[math_vectors_test]" )
{
    const vec2<F32> input1(1.0f, 2.0f);
    const vec2<I32> input2(1, 2);

    CHECK_EQUAL(input2, vec2<I32>(input1));

    CHECK_EQUAL(vec2<F32>(vec3<I32>(vec4<U32>(5.0f))), vec2<F32>(vec3<U32>(vec4<I32>(5u))));
}

TEST_CASE( "Vec Length Tests", "[math_vectors_test]" )
{
    vec2<F32> input1;
    vec3<F32> input2;
    vec4<F32> input3;
    vec4<F32> input4{0.f, 0.f, 0.f, 1.f};

    CHECK_ZERO(input1.length());
    CHECK_ZERO(input2.length());
    CHECK_ZERO(input3.length());
    CHECK_TRUE(COMPARE(input4.length(), 1.f));
    input3.w = 0.0f;
    CHECK_ZERO(input3.length());


    input1.set(2.0f, 3.0f);
    CHECK_NOT_ZERO(input1.length());

    input2.set(4.0f, 3.0f, 2.0f);
    input3.set(4.0f, 3.0f, 2.0f, 0.0f);
    CHECK_TRUE(COMPARE(std::sqrt(input2.lengthSquared()), input3.length()));
}

TEST_CASE( "Vec-Scalar Multiply Tests", "[math_vectors_test]" )
{
    const vec2<I32> input1(-2);
    const vec3<F32> input2(5.0f, 0.0f, -5.0f);
    const vec4<U32> input3(10);

    const vec2<I32> result1(-22);
    const vec3<F32> result2(2.5f, 0.0f, -2.5f);
    const vec4<U32> result3(30);

    CHECK_EQUAL(input1 * 11, result1);
    CHECK_EQUAL(input2 * 0.5f, result2);
    CHECK_EQUAL(input3 * 3, result3);
}

TEST_CASE( "Vec-Vec Multiply Tests", "[math_vectors_test]" )
{
    const vec2<U32> input1(2);
    const vec3<I32> input2(5, 0, -5);
    const vec4<F32> input3(10.0f);

    const vec2<U32> input4(4);
    const vec3<I32> input5(2, 3, -1);
    const vec4<F32> input6(1.0f);

    const vec2<U32> result1((2u * 4u), (2u * 4u));
    const vec3<I32> result2((5 * 2), (0 * 3), (-5 * -1));
    const vec4<F32> result3((10.0f * 1.0f), (10.0f * 1.0f), (10.0f * 1.0f), (10.0f * 1.0f));

    CHECK_EQUAL(input1 * input4, result1);
    CHECK_EQUAL(input2 * input5, result2);
    CHECK_EQUAL(input3 * input6, result3);
}

TEST_CASE( "Vec Dot Tests", "[math_vectors_test]" )
{
    const vec2<U32> input1(2);
    const vec3<I32> input2(5, 0, -5);
    const vec4<F32> input3(10.0f);

    const vec2<U32> input4(4);
    const vec3<I32> input5(2, 3, -1);
    const vec4<F32> input6(1.0f);

    CHECK_EQUAL(input1.dot(input4), (2u * 4u) + (2u * 4u));
    CHECK_EQUAL(input2.dot(input5), (5 * 2) +( 0 * 3) + (-5 * -1));
    CHECK_TRUE(COMPARE(input3.dot(input6), (10.0f * 1.0f) + (10.0f * 1.0f) + (10.0f * 1.0f) + (10.0f * 1.0f)));
}


} //namespace Divide
