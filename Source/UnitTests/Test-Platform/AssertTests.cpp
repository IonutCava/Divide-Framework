// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

namespace Divide
{
    namespace
    {
        struct AssertionChildResult
        {
            int _exitCode = 0;
            std::string _output;
        };

        [[nodiscard]] AssertionChildResult RunAssertionChild( const char* mode )
        {
            const char* args[] = { UnitTests::g_testExecutablePath, "--divide-assert-child", mode, nullptr };
            const SDL_PropertiesID properties = SDL_CreateProperties();
            REQUIRE( properties != 0u );
            const bool configured = SDL_SetPointerProperty( properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args ) &&
                                    SDL_SetNumberProperty( properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP ) &&
                                    SDL_SetBooleanProperty( properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true );
            if ( !configured )
            {
                const std::string error = SDL_GetError();
                SDL_DestroyProperties( properties );
                FAIL( error );
            }
            SDL_Process* const child = SDL_CreateProcessWithProperties( properties );
            const std::string error = SDL_GetError();
            SDL_DestroyProperties( properties );
            INFO( error );
            REQUIRE( child != nullptr );
            const std::unique_ptr<SDL_Process, decltype( &SDL_DestroyProcess )> process( child, SDL_DestroyProcess );
            SDL_IOStream* const output = SDL_GetProcessOutput( process.get() );
            if ( output == nullptr )
            {
                SDL_KillProcess( process.get(), true );
                SDL_WaitProcess( process.get(), true, nullptr );
                FAIL( SDL_GetError() );
            }

            AssertionChildResult result{};
            const auto drainOutput = [&]
            {
                std::array<char, 1024> buffer{};
                size_t count = 0u;
                while ( ( count = SDL_ReadIO( output, buffer.data(), buffer.size() ) ) > 0u )
                {
                    result._output.append( buffer.data(), count );
                }
            };
            const U64 start = SDL_GetTicks();
            while ( true )
            {
                drainOutput();
                if ( SDL_WaitProcess( process.get(), false, &result._exitCode ) )
                {
                    drainOutput();
                    break;
                }
                if ( SDL_GetTicks() - start >= 15000u )
                {
                    SDL_KillProcess( process.get(), true );
                    SDL_WaitProcess( process.get(), true, nullptr );
                    FAIL( "Assertion child did not terminate within 15 seconds" );
                }
                SDL_Delay( 10u );
            }
            return result;
        }
    }

    TEST_CASE( "Expected GPU calls do not shadow caller variables and evaluate once", "[assert_tests]" )
    {
        const bool result = true;
        DIVIDE_EXPECTED_GPU_CALL( result );
        U32 evaluations = 0u;
        DIVIDE_EXPECTED_GPU_CALL( ++evaluations == 1u );
        CHECK( evaluations == 1u );
    }

    TEST_CASE( "Unexpected calls report their source and terminate", "[assert_tests]" )
    {
        SECTION( "default message" )
        {
            const AssertionChildResult result = RunAssertionChild( "default" );
            CHECK( result._exitCode == 73 );
            CHECK( result._output.find( "UNEXPECTED CALL" ) != std::string::npos );
            CHECK( result._output.find( "main.cpp" ) != std::string::npos );
        }
        SECTION( "message expression is evaluated once" )
        {
            const AssertionChildResult result = RunAssertionChild( "message" );
            CHECK( result._exitCode == 73 );
            CHECK( result._output.find( "unexpected message with {braces}" ) != std::string::npos );
        }
        SECTION( "native abort without a signal handler" )
        {
            const AssertionChildResult result = RunAssertionChild( "native" );
            CHECK( result._exitCode != 0 );
            CHECK( result._exitCode != 79 );
            CHECK( result._output.find( "native unexpected call" ) != std::string::npos );
        }
    }

    TEST_CASE( "Expected calls and recoverable fallbacks can continue", "[assert_tests]" )
    {
        const AssertionChildResult result = RunAssertionChild( "expected" );
        INFO( result._output );
        CHECK( result._exitCode == 76 );
    }
}
