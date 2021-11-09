#include "stdafx.h"

#include "Headers/Defines.h"

namespace Divide {

TEST(U24Conversions)
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


TEST(I24Conversions)
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