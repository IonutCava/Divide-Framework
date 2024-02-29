#include "Platform/Headers/PlatformDefines.h"

//Using: https://gitlab.com/cppocl/unit_test_framework
#include <test/Test.hpp>
#include <iostream>


struct ConsoleLogger
{
    void operator ()( char const* str )
    {
        Divide::Console::printf( str );
        std::cout << str;
    }
};

struct SetupTeardown
{
    SetupTeardown()
    {
        std::cout << "Running Engine Unit Tests!" << std::endl;
        Divide::Console::ToggleFlag( Divide::Console::Flags::PRINT_IMMEDIATE, true );
        TEST_OVERRIDE_LOG( ConsoleLogger, new ConsoleLogger() );
    }
    ~SetupTeardown()
    {
        Divide::Console::Flush();
        std::cout << std::endl;
    }

} g_TestSetup;


bool PreparePlatform() 
{
    using namespace Divide;

    static ErrorCode err = ErrorCode::PLATFORM_INIT_ERROR;
    if (err != ErrorCode::NO_ERR)
    {
        if (!PlatformClose()) 
        {
            NOP();
        }

        const char* data[] = { "--disableCopyright" };
        err = PlatformInit(1, const_cast<char**>(data));

        if (err != ErrorCode::NO_ERR)
        {
            std::cout << "Platform error code: " << static_cast<int>(err) << std::endl;
        }
    }

    return err == ErrorCode::NO_ERR;
}

#include "Tests/ByteBufferTests.hpp"
#include "Tests/ConversionTests.hpp"
#include "Tests/DataTypeTests.hpp"
#include "Tests/HashTests.hpp"
#include "Tests/MathMatrixTests.hpp"
#include "Tests/MathVectorTests.hpp"
#include "Tests/ScriptingTests.hpp"
#include "Tests/StringTests.hpp"
#include "Tests/ThreadingTests.hpp"

int main( [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    using namespace Divide;

    SCOPE_EXIT
    {
        if ( !PlatformClose() )
        {
            std::cout << "Platform close error!" << std::endl;
        }

        ocl::TestClass::SetLogger( nullptr );
    };

    if (TEST_HAS_FAILED)
    {
        std::cout << "Errors detected!" << std::endl;
        return -1;
    }

    std::cout << "No errors detected!" << std::endl;
    return 0;
}

