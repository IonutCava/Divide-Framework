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

#pragma once
#ifndef DVD_UNIT_TEST_COMMON_H
#define DVD_UNIT_TEST_COMMON_H

#include <catch2/catch_all.hpp>

namespace Divide::Time
{
    class ProfileTimer;
};

class platformInitRunListener : public Catch::EventListenerBase
{
  public:
    using Catch::EventListenerBase::EventListenerBase;

    static bool PLATFORM_INIT;

    static void PlatformInit();

    void testRunStarting( Catch::TestRunInfo const& ) override;
    void testRunEnded( Catch::TestRunStats const& ) override;
  private:
     Divide::Time::ProfileTimer* _testTimer = nullptr;
};

#ifndef CHECK_TRUE
#define CHECK_TRUE( ... ) CHECK( __VA_ARGS__ )
#endif //CHECK_TRUE

#ifndef CHECK_EQUAL
#define CHECK_EQUAL(LHS, RHS) CHECK_TRUE(LHS == RHS)
#endif //CHECK_EQUAL

#ifndef CHECK_COMPARE
#define CHECK_COMPARE(LHS, RHS) CHECK_TRUE(COMPARE(LHS, RHS))
#endif //CHECK_COMPARE

#ifndef CHECK_COMPARE_TOLERANCE
#define CHECK_COMPARE_TOLERANCE(LHS, RHS, TOL) CHECK_TRUE(COMPARE_TOLERANCE(LHS, RHS, TOL))
#endif //CHECK_COMPARE_TOLERANCE

#ifndef CHECK_NOT_EQUAL
#define CHECK_NOT_EQUAL(LHS, RHS) CHECK_FALSE(LHS == RHS)
#endif //CHECK_NOT_EQUAL

#ifndef CHECK_ZERO
#define CHECK_ZERO(X) CHECK_TRUE(Divide::IS_ZERO(X))
#endif //CHECK_EQUAL

#ifndef CHECK_NOT_ZERO
#define CHECK_NOT_ZERO(X) CHECK_FALSE(Divide::IS_ZERO(X))
#endif //CHECK_NOT_ZERO

#ifndef STATIC_CHECK_TRUE
#define STATIC_CHECK_TRUE( ... ) STATIC_CHECK( __VA_ARGS__ )
#endif //STATIC_CHECK_TRUE

#ifndef STATIC_CHECK_EQUAL
#define STATIC_CHECK_EQUAL(LHS, RHS) STATIC_CHECK_TRUE(LHS == RHS)
#endif //STATIC_CHECK_EQUAL

#ifndef STATIC_CHECK_COMPARE
#define STATIC_CHECK_COMPARE(LHS, RHS) STATIC_CHECK_TRUE(COMPARE(LHS, RHS))
#endif //STATIC_CHECK_COMPARE

#ifndef STATIC_CHECK_COMPARE_TOLERANCE
#define STATIC_CHECK_COMPARE_TOLERANCE(LHS, RHS, TOL) STATIC_CHECK_TRUE(COMPARE_TOLERANCE(LHS, RHS, TOL))
#endif //STATIC_CHECK_COMPARE_TOLERANCE

#ifndef STATIC_CHECK_NOT_EQUAL
#define STATIC_CHECK_NOT_EQUAL(LHS, RHS) STATIC_CHECK_FALSE(LHS == RHS)
#endif //STATIC_CHECK_NOT_EQUAL

#ifndef STATIC_CHECK_ZERO
#define STATIC_CHECK_ZERO(X) STATIC_CHECK_TRUE(Divide::IS_ZERO(X))
#endif //STATIC_CHECK_EQUAL

#ifndef STATIC_CHECK_NOT_ZERO
#define STATIC_CHECK_NOT_ZERO(X) STATIC_CHECK_FALSE(Divide::IS_ZERO(X))
#endif //STATIC_CHECK_NOT_ZERO


#endif //DVD_UNIT_TEST_COMMON_H
