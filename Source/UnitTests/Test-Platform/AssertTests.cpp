// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

namespace Divide
{
    TEST_CASE( "Expected GPU calls do not shadow caller variables and evaluate once", "[assert_tests]" )
    {
        const bool result = true;
        DIVIDE_EXPECTED_GPU_CALL( result );
        U32 evaluations = 0u;
        DIVIDE_EXPECTED_GPU_CALL( ++evaluations == 1u );
        CHECK( evaluations == 1u );
    }
}
