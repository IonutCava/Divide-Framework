// Copyright (c) 2026 DIVIDE-Studio
#include "unitTestCommon.h"

#include "Core/Headers/SmokeRuntimeDialogs.h"
#include "Utility/Headers/ImageTools.h"
#include "Utility/Headers/Localization.h"

#include <csignal>
#include <cstdlib>
#include <exception>
#include <catch2/catch_session.hpp>

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

CATCH_REGISTER_LISTENER( platformInitRunListener )

namespace Divide::UnitTests
{
    const char* g_testExecutablePath = nullptr;
}

namespace
{
    volatile std::sig_atomic_t g_messageEvaluations = 0;

    [[noreturn]] void ExitOnAbort( int ) noexcept
    {
        std::_Exit( g_messageEvaluations == 1 ? 73 : 74 );
    }

    const char* CountedMessage() noexcept
    {
        g_messageEvaluations = g_messageEvaluations + 1;
        return "unexpected message with {braces}";
    }

    int RunAssertionChild( const std::string_view mode )
    {
        if ( !Divide::RenderSmokeTest::ConfigureNonInteractiveCRT() )
        {
            return 80;
        }
#if !defined(_WIN32)
        const rlimit noCoreDump{ 0, 0 };
        if ( setrlimit( RLIMIT_CORE, &noCoreDump ) != 0 )
        {
            return 80;
        }
#endif
#if defined(SIGTRAP)
        if ( std::signal( SIGTRAP, SIG_IGN ) == SIG_ERR )
        {
            return 80;
        }
#endif
        std::set_terminate( [] { std::_Exit( 79 ); } );

        if ( mode == "native" )
        {
            DIVIDE_UNEXPECTED_CALL_MSG( "native unexpected call" );
        }

        if ( std::signal( SIGABRT, ExitOnAbort ) == SIG_ERR )
        {
            return 80;
        }
        if ( mode == "default" )
        {
            g_messageEvaluations = 1;
            DIVIDE_UNEXPECTED_CALL();
        }
        if ( mode == "message" )
        {
            DIVIDE_UNEXPECTED_CALL_MSG( CountedMessage() );
        }
        if ( mode == "expected" )
        {
            int evaluations = 0;
            DIVIDE_EXPECTED_CALL( ++evaluations == 0 );
            DIVIDE_EXPECTED_CALL_MSG( ++evaluations == 1, "expected message with {braces}" );
            DIVIDE_EXPECTED_GPU_CALL_MSG( ++evaluations == 2, "expected GPU message with {braces}" );

            Divide::Locale::LanguageData locale;
            if ( evaluations != 3 ||
                 std::string_view( locale.get( 1u, false, "fallback" ) ) != "fallback" ||
                 std::string_view( locale.get( 1u, false, nullptr ) ) != "MISSING_ENTRY_NO_DEFAULT" )
            {
                return 81;
            }

            const std::array<Divide::Byte, 4> pixels{};
            if ( Divide::ImageTools::SaveImage( {}, 1u, 1u, 5u, 4u, false, pixels.data(), Divide::ImageTools::SaveImageFormat::PNG ) ||
                 Divide::ImageTools::SaveImage( {}, 1u, 1u, 4u, 4u, false, pixels.data(), Divide::ImageTools::SaveImageFormat::COUNT ) )
            {
                return 81;
            }
            return 76;
        }
        return 78;
    }
}

int main( int argc, char* argv[] )
{
    Divide::UnitTests::g_testExecutablePath = argv[0];
    if ( argc == 3 && std::string_view( argv[1] ) == "--divide-assert-child" )
    {
        return RunAssertionChild( argv[2] );
    }
    return Catch::Session().run( argc, argv );
}
