// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"

namespace Divide
{

namespace
{
    enum class TestFlags : U8
    {
        NONE = 0,
        A = toBit(1),
        B = toBit(2),
        C = toBit(3)
    };

    enum class UntouchedFlags : U8
    {
        X = toBit(1),
        Y = toBit(2)
    };
}

DIVIDE_ENUM_FLAGS(TestFlags);

namespace
{
    template<class E>
    concept HasBitwiseOr = requires(E a) { a | a; };
}

TEST_CASE( "Opted-in enum flags support bitwise operators", "[enum_flags_tests]" )
{
    STATIC_CHECK( EnableEnumFlags<TestFlags> );
    STATIC_CHECK( EnumFlag<TestFlags> );
    STATIC_CHECK( !EnableEnumFlags<UntouchedFlags> );
    STATIC_CHECK( HasBitwiseOr<TestFlags> );
    STATIC_CHECK( !HasBitwiseOr<UntouchedFlags> );

    STATIC_CHECK( to_base(TestFlags::A | TestFlags::B) == (to_base(TestFlags::A) | to_base(TestFlags::B)) );
    STATIC_CHECK( to_base(TestFlags::A & (TestFlags::A | TestFlags::B)) == to_base(TestFlags::A) );
    STATIC_CHECK( to_base(TestFlags::A ^ TestFlags::B) == (to_base(TestFlags::A) ^ to_base(TestFlags::B)) );
    STATIC_CHECK( to_base(~TestFlags::A) == static_cast<BaseType<TestFlags>>(~to_base(TestFlags::A)) );

    TestFlags flags = TestFlags::A;
    flags |= TestFlags::B;
    CHECK( to_base(flags) == (to_base(TestFlags::A) | to_base(TestFlags::B)) );

    flags &= TestFlags::A;
    CHECK( to_base(flags) == to_base(TestFlags::A) );

    flags ^= TestFlags::C;
    CHECK( to_base(flags) == (to_base(TestFlags::A) ^ to_base(TestFlags::C)) );
}

} //namespace Divide
