// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"
#include "Platform/Video/Headers/CullCounterReadbackState.h"

namespace Divide
{
    TEST_CASE( "Cull counter slots stay reserved until asynchronous completion", "[culling][readback]" )
    {
        CullCounterReadbackState state;
        state.reset( 2u );

        const U16 first = state.reserve( 0u, 1u );
        REQUIRE( first == 0u );
        REQUIRE( state.markInFlight( first ) );

        const U16 second = state.reserve( 1u, 2u );
        REQUIRE( second == 1u );
        REQUIRE( state.markInFlight( second ) );

        CHECK( state.reserve( 0u, 3u ) == U16_MAX );
        CHECK( state.state( first ) == CullCounterReadbackState::SlotState::IN_FLIGHT );
        CHECK( state.state( second ) == CullCounterReadbackState::SlotState::IN_FLIGHT );
    }

    TEST_CASE( "Out-of-order cull readbacks publish only the newest successful result", "[culling][readback]" )
    {
        CullCounterReadbackState state;
        state.reset( 2u );

        uint4 latestResult{};
        U64 latestSequence = 0u;

        const U16 older = state.reserve( 0u, 1u );
        REQUIRE( older != U16_MAX );
        REQUIRE( state.markInFlight( older ) );
        state.readbackData( older )->_v[0] = 10u;

        const U16 newer = state.reserve( 1u, 2u );
        REQUIRE( newer != U16_MAX );
        REQUIRE( state.markInFlight( newer ) );
        state.readbackData( newer )->_v[0] = 20u;

        CHECK( state.complete( newer, true, latestResult, latestSequence ) );
        CHECK( latestResult._v[0] == 20u );
        CHECK( latestSequence == 2u );

        CHECK_FALSE( state.complete( older, true, latestResult, latestSequence ) );
        CHECK( latestResult._v[0] == 20u );
        CHECK( latestSequence == 2u );
        CHECK( state.state( older ) == CullCounterReadbackState::SlotState::FREE );
        CHECK( state.state( newer ) == CullCounterReadbackState::SlotState::FREE );
    }

    TEST_CASE( "Unsubmitted and failed cull readbacks release slots without publishing", "[culling][readback]" )
    {
        CullCounterReadbackState state;
        state.reset( 1u );

        uint4 latestResult{};
        U64 latestSequence = 4u;

        const U16 unsubmitted = state.reserve( 0u, 5u );
        REQUIRE( unsubmitted == 0u );
        state.releaseReserved();
        CHECK( state.state( unsubmitted ) == CullCounterReadbackState::SlotState::FREE );

        const U16 failed = state.reserve( 0u, 6u );
        REQUIRE( failed == 0u );
        REQUIRE( state.markInFlight( failed ) );
        state.readbackData( failed )->_v[0] = 99u;

        CHECK_FALSE( state.complete( failed, false, latestResult, latestSequence ) );
        CHECK( state.state( failed ) == CullCounterReadbackState::SlotState::FREE );
        CHECK( latestResult._v[0] == 0u );
        CHECK( latestSequence == 4u );
    }
}
