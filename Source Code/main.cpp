#include "stdafx.h"

#include "engineMain.h"

#if defined(_WIN32)
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif

int main(int argc, char **argv) { 

    Divide::Engine engine{};
    engine.run(argc, argv);

    return engine.errorCode();
}
