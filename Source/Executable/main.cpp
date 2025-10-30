

#include "EngineMain.h"
#include <SDL3/SDL_main.h>

#if !defined(SHOW_CONSOLE_WINDOW)

#if defined(_WIN32)
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif //_WIN32

#endif //!SHOW_CONSOLE_WINDOW

int main(int argc, char **argv)
{
    _MM_SET_FLUSH_ZERO_MODE( _MM_FLUSH_ZERO_ON );
    std::set_new_handler( []() noexcept{ assert( false && "Out of memory!" ); } );

    const Divide::ErrorCode err = Divide::Engine::Run(argc, argv);

    return static_cast<int>(err) * -1;
}
