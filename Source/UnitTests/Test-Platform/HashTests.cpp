#include "UnitTests/unitTestCommon.h"
#include "Core/Resources/Headers/Resource.h"

namespace Divide
{

TEST_CASE( "String Hash Combine Tests", "[hash_test]" )
{
    const string inputA = "bla1";
    const string inputB = "bla1";

    size_t seed1 = 0, seed2 = 0;
    Util::Hash_combine(seed1, inputA);
    Util::Hash_combine(seed2, inputB);

    CHECK_EQUAL(seed1, seed2);
}

TEST_CASE( "ResourceDescriptor Hash Combine Tests", "[hash_test]" )
{
    ResourceDescriptor<CachedResource> inputA("testDescriptor");
    inputA.flag(true);

    ResourceDescriptor<CachedResource> inputB("testDescriptor");
    inputB.flag(true);

    CHECK_EQUAL( inputA.getHash(), inputB.getHash() );
    CHECK_EQUAL( inputA, inputB );

    P32 testMask;
    testMask.i = 0;
    testMask.b[2] = true;
    inputB.mask(testMask);

    CHECK_NOT_EQUAL( inputA.getHash(), inputB.getHash() );
    CHECK_NOT_EQUAL( inputA, inputB );
}

} //namespace Divide
