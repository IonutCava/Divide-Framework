#include "Platform/Headers/PlatformDefines.h"

#include <iostream>

//Using: https://gitlab.com/cppocl/unit_test_framework
#include <test/Test.hpp>


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
    static Divide::ErrorCode err = Divide::ErrorCode::PLATFORM_INIT_ERROR;
    if (err != Divide::ErrorCode::NO_ERR)
    {
        if (!Divide::PlatformClose()) 
        {
            NOP();
        }

        const char* data[] = { "--disableCopyright" };
        err = Divide::PlatformInit(1, const_cast<char**>(data));

        if (err != Divide::ErrorCode::NO_ERR)
        {
            std::cout << "Platform error code: " << static_cast<int>(err) << std::endl;
        }
    }

    return err == Divide::ErrorCode::NO_ERR;
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

int main(int argc, char **argv)
{
    DIVIDE_UNUSED(argv);

    int state = 0;
    if (TEST_HAS_FAILED)
    {
        std::cout << "Errors detected!" << std::endl;
        state = -1;
    }
    else
    {
        std::cout << "No errors detected!" << std::endl;
    }

    if (!Divide::PlatformClose())
    {
        std::cout << "Platform close error!" << std::endl;
    }

    if (argc == 1)
    {
        system("pause");
    }

    ocl::TestClass::SetLogger(nullptr);

    return state;
}

