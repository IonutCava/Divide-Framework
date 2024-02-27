

#include "engineMain.h"

#if defined(_WIN32)
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif

int main(int argc, char **argv)
{
    _MM_SET_FLUSH_ZERO_MODE( _MM_FLUSH_ZERO_ON );
    std::set_new_handler( []() noexcept{ assert( false && "Out of memory!" ); } );

    Divide::Engine engine{};
    const Divide::ErrorCode err = engine.run(argc, argv);

    return static_cast<int>(err) * -1;
}
