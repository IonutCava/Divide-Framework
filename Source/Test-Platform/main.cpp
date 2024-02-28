

#include "Platform/Headers/PlatformDefines.h"
//Using: https://gitlab.com/cppocl/unit_test_framework
#include <test/Test.hpp>
#include <iostream>


bool PreparePlatform() {
    static Divide::ErrorCode err = Divide::ErrorCode::PLATFORM_INIT_ERROR;
    if (err != Divide::ErrorCode::NO_ERR) {
        if (!Divide::PlatformClose()) {
            NOP();
        }
        const char* data[] = { "--disableCopyright" };
        err = Divide::PlatformInit(1, const_cast<char**>(data));
        if (err != Divide::ErrorCode::NO_ERR) {
            std::cout << "Platform error code: " << static_cast<int>(err) << std::endl;
        }
    }

    return err == Divide::ErrorCode::NO_ERR;
}

int main(int argc, [[maybe_unused]] char **argv) {

    std::cout << "Running Engine Unit Tests!" << std::endl;

    int state = 0;
    if (TEST_HAS_FAILED) {
        std::cout << "Errors detected!" << std::endl;
        state = -1;
    } else {
        std::cout << "No errors detected!" << std::endl;
    }

    if (!Divide::PlatformClose()) {
        std::cout << "Platform close error!" << std::endl;
    }

    if (argc == 1) {
        system("pause");
    }

    return state;
}

