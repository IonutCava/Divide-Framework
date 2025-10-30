#include "Headers/PlatformDefines.h"
#include "Headers/PlatformRuntime.h"

#include "GUI/Headers/GUI.h"

#include "Utility/Headers/Localization.h"
#include "Platform/File/Headers/FileManagement.h"

namespace Divide
{

    namespace
    {
        NO_DESTROY SysInfo g_sysInfo;
    };

    namespace Assert
    {
        bool Callback( const bool expression, const std::string_view expressionStr, const std::string_view file, const int line, const std::string_view failMessage ) noexcept
        {
            if constexpr ( !Config::Build::IS_SHIPPING_BUILD )
            {
                if ( !expression ) [[unlikely]]
                {
                    if ( failMessage.empty() ) [[unlikely]]
                    {
                        return Callback( false, expressionStr, file, line, "Message truncated" );
                    }

                    const string msgOut = Util::StringFormat( "ASSERT [{} : {}]: {} : {}", file, line, expressionStr, failMessage );
                    if constexpr ( Config::Assert::LOG_ASSERTS )
                    {
                        Console::errorfn( msgOut.c_str() );
                    }

                    DIVIDE_ASSERT_MSG_BOX( msgOut.c_str() );
                    Console::Flush();

                    if constexpr ( Config::Assert::CONTINUE_ON_ASSERT )
                    {
                        DebugBreak();
                    }
                    else
                    {
                        assert( expression && msgOut.c_str() );
                    }
                }
            }

            return expression;
        }
    }; // namespace Assert

    SysInfo& sysInfo() noexcept
    {
        return g_sysInfo;
    }

    const SysInfo& const_sysInfo() noexcept
    {
        return g_sysInfo;
    }

    ErrorCode PlatformInit( const int argc, char** argv )
    {
        Runtime::mainThreadID( std::this_thread::get_id() );
        SeedRandom();

        InitSysInfo( sysInfo(), argc, argv );
        return Paths::initPaths();
    }

    bool PlatformClose()
    {
        return Runtime::resetMainThreadID();
    }

    void InitSysInfo( SysInfo& info, [[maybe_unused]] const I32 argc, [[maybe_unused]] char** argv )
    {
        if ( !GetAvailableMemory( info ) )
        {
            DebugBreak();
            // Assume 256Megs as a minimum
            info._availableRamInBytes = (1 << 8) * 1024 * 1024;
        }

        info._workingDirectory = getWorkingDirectory();
    }

    string GetClipboardText() noexcept
    {
        char* str = SDL_GetClipboardText();
        string ret(str);
        SDL_free(str);
        return ret;
    }

    void SetClipboardText( const char* text ) noexcept
    {
        SDL_SetClipboardText( text );
    }

    void ToggleCursor( const bool visible ) noexcept
    {
        if (CursorVisible() != visible)
        {
            visible ? SDL_ShowCursor()
                    : SDL_HideCursor();
        }
    }

    bool CursorVisible() noexcept
    {
        return SDL_CursorVisible();
    }

    std::string CurrentDateTimeString()
    {
        const std::time_t t = std::time( nullptr );
        std::tm* now = std::localtime( &t );

        char buffer[128];
        strftime( buffer, sizeof( buffer ), "%d_%m_%Y__%H_%M_%S", now );
        return buffer;
    }

};  // namespace Divide
