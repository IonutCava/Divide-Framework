#include "unitTestCommon.h"

#include "Platform/Headers/PlatformDefines.h"
#include <iostream>

void platformInitRunListener::testRunStarting( Catch::TestRunInfo const& )
{
    using namespace Divide;

    static ErrorCode err = ErrorCode::PLATFORM_INIT_ERROR;
    if ( err != ErrorCode::NO_ERR )
    {
        if ( !PlatformClose() )
        {
            NOP();
        }

        const char* data[] = { "--disableCopyright" };
        err = PlatformInit( 1, const_cast<char**>(data) );

    }

    if ( err != ErrorCode::NO_ERR )
    {
        std::cout << "Platform error code: " << static_cast<int>(err) << std::endl;
    }
}

void platformInitRunListener::testRunEnded( Catch::TestRunStats const& )
{
    if ( !Divide::PlatformClose() )
    {
        std::cout << "Platform close error!" << std::endl;
    }
}



CATCH_REGISTER_LISTENER( platformInitRunListener )