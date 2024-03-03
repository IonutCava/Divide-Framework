#include "UnitTests/unitTestCommon.h"

namespace Divide
{

TEST_CASE( "Fits In Registers", "[data_type_tests]" )
{
    CHECK_TRUE(fits_in_registers<U8>());
    CHECK_TRUE(fits_in_registers<U16>());
    CHECK_TRUE(fits_in_registers<U32>());
    CHECK_TRUE(fits_in_registers<I8>());
    CHECK_TRUE(fits_in_registers<I16>());
    CHECK_TRUE(fits_in_registers<I32>());
    CHECK_TRUE(fits_in_registers<U64>());
    CHECK_TRUE(fits_in_registers<vec2<U8>>());
    CHECK_TRUE(fits_in_registers<vec2<U16>>());
    CHECK_TRUE(fits_in_registers<vec2<U32>>());
    CHECK_FALSE(fits_in_registers<vec2<U64>>());
    CHECK_TRUE(fits_in_registers<vec2<I8>>());
    CHECK_TRUE(fits_in_registers<vec2<I16>>());
    CHECK_TRUE(fits_in_registers<vec2<I32>>());
    CHECK_FALSE(fits_in_registers<vec2<I64>>());
    CHECK_TRUE(fits_in_registers<vec2<F32>>());
    CHECK_FALSE(fits_in_registers<vec2<D64>>());
    CHECK_TRUE(fits_in_registers<mat2<U8>>());
    CHECK_TRUE(fits_in_registers<mat2<U16>>());
    CHECK_FALSE(fits_in_registers<mat2<U32>>());
    CHECK_FALSE(fits_in_registers<mat2<U64>>());
    CHECK_TRUE(fits_in_registers<mat2<I8>>());
    CHECK_TRUE(fits_in_registers<mat2<I16>>());
    CHECK_FALSE(fits_in_registers<mat2<I32>>());
    CHECK_FALSE(fits_in_registers<mat2<I64>>());
    CHECK_FALSE(fits_in_registers<mat2<F32>>());
    CHECK_FALSE(fits_in_registers<mat2<D64>>());
    CHECK_FALSE(fits_in_registers<vec3<U8>>());
    CHECK_FALSE(fits_in_registers<vec3<U16>>());
    CHECK_FALSE(fits_in_registers<vec3<U32>>());
    CHECK_FALSE(fits_in_registers<vec3<U64>>());
    CHECK_FALSE(fits_in_registers<vec3<I8>>());
    CHECK_FALSE(fits_in_registers<vec3<I16>>());
    CHECK_FALSE(fits_in_registers<vec3<I32>>());
    CHECK_FALSE(fits_in_registers<vec3<I64>>());
    CHECK_FALSE(fits_in_registers<vec3<F32>>());
    CHECK_FALSE(fits_in_registers<vec3<D64>>());
    CHECK_FALSE(fits_in_registers<mat3<U8>>());
    CHECK_FALSE(fits_in_registers<mat3<U16>>());
    CHECK_FALSE(fits_in_registers<mat3<U32>>());
    CHECK_FALSE(fits_in_registers<mat3<U64>>());
    CHECK_FALSE(fits_in_registers<mat3<I8>>());
    CHECK_FALSE(fits_in_registers<mat3<I16>>());
    CHECK_FALSE(fits_in_registers<mat3<I32>>());
    CHECK_FALSE(fits_in_registers<mat3<I64>>());
    CHECK_FALSE(fits_in_registers<mat3<F32>>());
    CHECK_FALSE(fits_in_registers<mat3<D64>>());
    CHECK_TRUE(fits_in_registers<vec4<U8>>());
    CHECK_TRUE(fits_in_registers<vec4<U16>>());
    CHECK_FALSE(fits_in_registers<vec4<U32>>());
    CHECK_FALSE(fits_in_registers<vec4<U64>>());
    CHECK_TRUE(fits_in_registers<vec4<I8>>());
    CHECK_TRUE(fits_in_registers<vec4<I16>>());
    CHECK_FALSE(fits_in_registers<vec4<I32>>());
    CHECK_FALSE(fits_in_registers<vec4<I64>>());
    CHECK_FALSE(fits_in_registers<vec4<F32>>());
    CHECK_FALSE(fits_in_registers<vec4<D64>>());
    CHECK_FALSE(fits_in_registers<mat4<U8>>());
    CHECK_FALSE(fits_in_registers<mat4<U16>>());
    CHECK_FALSE(fits_in_registers<mat4<U32>>());
    CHECK_FALSE(fits_in_registers<mat4<U64>>());
    CHECK_FALSE(fits_in_registers<mat4<I8>>());
    CHECK_FALSE(fits_in_registers<mat4<I16>>());
    CHECK_FALSE(fits_in_registers<mat4<I32>>());
    CHECK_FALSE(fits_in_registers<mat4<I64>>());
    CHECK_FALSE(fits_in_registers<mat4<F32>>());
    CHECK_FALSE(fits_in_registers<mat4<D64>>());

    CHECK_FALSE( fits_in_registers<eastl::vector<mat4<F32>>>() );
    CHECK_FALSE( fits_in_registers<eastl::shared_ptr<vec3<U8>>>() );
    CHECK_TRUE( fits_in_registers<eastl::unique_ptr<mat3<I16>>>() );
}

TEST_CASE( "Can Be Returned By Value", "[data_type_tests]" )
{
    CHECK_TRUE( can_be_returned_by_value<U8>() );
    CHECK_TRUE( can_be_returned_by_value<U16>() );
    CHECK_TRUE( can_be_returned_by_value<U32>() );
    CHECK_TRUE( can_be_returned_by_value<I8>() );
    CHECK_TRUE( can_be_returned_by_value<I16>() );
    CHECK_TRUE( can_be_returned_by_value<I32>() );
    CHECK_TRUE( can_be_returned_by_value<U64>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<U8>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<U16>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<U32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<U64>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<I8>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<I16>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<I32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<I64>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<F32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec2<D64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<U8>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<U16>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<U32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<U64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<I8>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<I16>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<I32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<I64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<F32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat2<D64>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<U8>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<U16>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<U32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<U64>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<I8>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<I16>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<I32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<I64>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<F32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec3<D64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<U8>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<U16>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<U32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<U64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<I8>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<I16>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<I32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<I64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<F32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat3<D64>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<U8>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<U16>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<U32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<U64>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<I8>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<I16>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<I32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<I64>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<F32>>() );
    CHECK_TRUE( can_be_returned_by_value<vec4<D64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<U8>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<U16>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<U32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<U64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<I8>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<I16>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<I32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<I64>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<F32>>() );
    CHECK_TRUE( can_be_returned_by_value<mat4<D64>>() );

    CHECK_TRUE( can_be_returned_by_value<eastl::vector<vec4<I8>>>() );
    CHECK_TRUE (can_be_returned_by_value<eastl::shared_ptr<vec2<I32>>>());
    CHECK_FALSE (can_be_returned_by_value<eastl::unique_ptr<vec3<F32>>>());
}

TEST_CASE( "Pass By Value", "[data_type_tests]" )
{
    CHECK_TRUE( pass_by_value<U8>() );
    CHECK_TRUE( pass_by_value<U16>() );
    CHECK_TRUE( pass_by_value<U32>() );
    CHECK_TRUE( pass_by_value<U64>() );
    CHECK_TRUE( pass_by_value<I8>() );
    CHECK_TRUE( pass_by_value<I16>() );
    CHECK_TRUE( pass_by_value<I32>() );
    CHECK_TRUE( pass_by_value<I64>() );
    CHECK_TRUE( pass_by_value<vec2<U8>>() );
    CHECK_TRUE( pass_by_value<vec2<U16>>() );
    CHECK_TRUE( pass_by_value<vec2<U32>>() );
    CHECK_FALSE( pass_by_value<vec2<U64>>() );
    CHECK_TRUE( pass_by_value<vec2<I8>>() );
    CHECK_TRUE( pass_by_value<vec2<I16>>() );
    CHECK_TRUE( pass_by_value<vec2<I32>>() );
    CHECK_FALSE( pass_by_value<vec2<I64>>() );
    CHECK_TRUE( pass_by_value<vec2<F32>>() );
    CHECK_FALSE( pass_by_value<vec2<D64>>() );
    CHECK_TRUE( pass_by_value<mat2<U8>>() );
    CHECK_TRUE( pass_by_value<mat2<U16>>() );
    CHECK_FALSE( pass_by_value<mat2<U32>>() );
    CHECK_FALSE( pass_by_value<mat2<U64>>() );
    CHECK_TRUE( pass_by_value<mat2<I8>>() );
    CHECK_TRUE( pass_by_value<mat2<I16>>() );
    CHECK_FALSE( pass_by_value<mat2<I32>>() );
    CHECK_FALSE( pass_by_value<mat2<I64>>() );
    CHECK_FALSE( pass_by_value<mat2<F32>>() );
    CHECK_FALSE( pass_by_value<mat2<D64>>() );
    CHECK_FALSE( pass_by_value<vec3<U8>>() );
    CHECK_FALSE( pass_by_value<vec3<U16>>() );
    CHECK_FALSE( pass_by_value<vec3<U32>>() );
    CHECK_FALSE( pass_by_value<vec3<U64>>() );
    CHECK_FALSE( pass_by_value<vec3<I8>>() );
    CHECK_FALSE( pass_by_value<vec3<I16>>() );
    CHECK_FALSE( pass_by_value<vec3<I32>>() );
    CHECK_FALSE( pass_by_value<vec3<I64>>() );
    CHECK_FALSE( pass_by_value<vec3<F32>>() );
    CHECK_FALSE( pass_by_value<vec3<D64>>() );
    CHECK_FALSE( pass_by_value<mat3<U8>>() );
    CHECK_FALSE( pass_by_value<mat3<U16>>() );
    CHECK_FALSE( pass_by_value<mat3<U32>>() );
    CHECK_FALSE( pass_by_value<mat3<U64>>() );
    CHECK_FALSE( pass_by_value<mat3<I8>>() );
    CHECK_FALSE( pass_by_value<mat3<I16>>() );
    CHECK_FALSE( pass_by_value<mat3<I32>>() );
    CHECK_FALSE( pass_by_value<mat3<I64>>() );
    CHECK_FALSE( pass_by_value<mat3<F32>>() );
    CHECK_FALSE( pass_by_value<mat3<D64>>() );
    CHECK_TRUE( pass_by_value<vec4<U8>>() );
    CHECK_TRUE( pass_by_value<vec4<U16>>() );
    CHECK_FALSE( pass_by_value<vec4<U32>>() );
    CHECK_FALSE( pass_by_value<vec4<U64>>() );
    CHECK_TRUE( pass_by_value<vec4<I8>>() );
    CHECK_TRUE( pass_by_value<vec4<I16>>() );
    CHECK_FALSE( pass_by_value<vec4<I32>>() );
    CHECK_FALSE( pass_by_value<vec4<I64>>() );
    CHECK_FALSE( pass_by_value<vec4<F32>>() );
    CHECK_FALSE( pass_by_value<vec4<D64>>() );
    CHECK_FALSE( pass_by_value<mat4<U8>>() );
    CHECK_FALSE( pass_by_value<mat4<U16>>() );
    CHECK_FALSE( pass_by_value<mat4<U32>>() );
    CHECK_FALSE( pass_by_value<mat4<U64>>() );
    CHECK_FALSE( pass_by_value<mat4<I8>>() );
    CHECK_FALSE( pass_by_value<mat4<I16>>() );
    CHECK_FALSE( pass_by_value<mat4<I32>>() );
    CHECK_FALSE( pass_by_value<mat4<I64>>() );
    CHECK_FALSE( pass_by_value<mat4<F32>>() );
    CHECK_FALSE( pass_by_value<mat4<D64>>() );

    CHECK_FALSE( pass_by_value<eastl::vector<vec4<I8>>>() );
    CHECK_FALSE( pass_by_value<eastl::shared_ptr<vec3<I8>>>() );
    CHECK_FALSE( pass_by_value<eastl::unique_ptr<vec4<D64>>>() );
}

TEST_CASE( "U24 Conversions", "[data_type_tests]" )
{
    constexpr U32 inputA = 134646u;
    constexpr U32 inputB = 0u;
    constexpr U32 inputC = 1u;

    const U24 testA(inputA);
    const U24 testB(inputB);
    const U24 testC = testB;
    U24 testD = U24(inputC);

    CHECK_EQUAL(testC, testB);
    CHECK_EQUAL(to_U32(testA), inputA);
    CHECK_EQUAL(to_U32(testB), inputB);
    CHECK_TRUE(testA > testB);
    CHECK_TRUE(testD < testA);
    CHECK_TRUE(testD <= U24(inputC));
    CHECK_TRUE(testA >= testD);
    CHECK_TRUE(testA > 222u);
    CHECK_TRUE(testB < 10u);
    CHECK_TRUE(testA == inputA);
    CHECK_TRUE(testB != inputA);
    CHECK_EQUAL(testB + 1u, inputC);
    CHECK_EQUAL(U24(inputC) - 1u, testB);
    CHECK_EQUAL(U24(inputC) - testD, inputB);
    CHECK_EQUAL(--testD, testB);
    CHECK_EQUAL(testD++, testB);
    CHECK_EQUAL(testD, U24(inputC));
}


TEST_CASE( "I24 Conversions", "[data_type_tests]" )
{
    constexpr I32 inputA = 134346;
    constexpr I32 inputB = 0;
    constexpr I32 inputC = -1;
    constexpr I32 inputD = -123213;

    const I24 testA(inputA);
    const I24 testB(inputB);
    const I24 testC = testB;
    I24 testD = I24(inputD);

    CHECK_EQUAL(testC, testB);
    CHECK_EQUAL(to_I32(testA), inputA);
    CHECK_EQUAL(to_I32(testB), inputB);
    CHECK_EQUAL(to_I32(testD), inputD);
    CHECK_TRUE(testA > testB);
    CHECK_TRUE(testD < inputB);
    CHECK_FALSE(testD >= I24(inputC));
    CHECK_TRUE(testA >= testD);
    CHECK_TRUE(testA > 222);
    CHECK_TRUE(testB < 10);
    CHECK_TRUE(testA == inputA);
    CHECK_TRUE(testB != inputA);
    CHECK_EQUAL(testB + to_I32(-1), inputC);
    CHECK_EQUAL(I24(inputC) - 1, -2);
    CHECK_EQUAL(I24(inputC) - testD, -1 - inputD);
    CHECK_EQUAL(--testD, inputD - 1);
    CHECK_EQUAL(testD++, inputD - 1);
    CHECK_EQUAL(testD, I24(inputD));
}

} //namespace Divide