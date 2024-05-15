


#include "config.h"

#include "Headers/PlatformDefines.h"
#include "Headers/PlatformRuntime.h"

#include "GUI/Headers/GUI.h"

#include "Utility/Headers/Localization.h"
#include "Platform/File/Headers/FileManagement.h"

#include <iostream>
#include <mimalloc-new-delete.h>

namespace Divide
{

    namespace
    {
        NO_DESTROY SysInfo g_sysInfo;
    };

    namespace Assert
    {

        bool DIVIDE_ASSERT_FUNC( const bool expression, const char* expressionStr, const char* file, const int line, const char* failMessage ) noexcept
        {
            if constexpr ( !Config::Build::IS_SHIPPING_BUILD )
            {
                if ( !expression ) [[unlikely]]
                {
                    if ( failMessage == nullptr || strlen( failMessage ) == 0 ) [[unlikely]]
                    {
                        return DIVIDE_ASSERT_FUNC( false, expressionStr, file, line, "Message truncated" );
                    }

                    const auto msgOut = Util::StringFormat( "ASSERT [{} : {}]: {} : {}", file, line, expressionStr, failMessage );
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
        Paths::initPaths( );
        Console::Start();
        
        return ErrorCode::NO_ERR;
    }

    bool PlatformClose()
    {
        Runtime::resetMainThreadID();
        Console::Stop();

        return true;
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

    const char* GetClipboardText() noexcept
    {
        return SDL_GetClipboardText();
    }

    void SetClipboardText( const char* text ) noexcept
    {
        SDL_SetClipboardText( text );
    }

    void ToggleCursor( const bool state ) noexcept
    {
        if ( CursorState() != state )
        {
            SDL_ShowCursor( state ? SDL_TRUE : SDL_FALSE );
        }
    }

    bool CursorState() noexcept
    {
        return SDL_ShowCursor( SDL_QUERY ) == SDL_ENABLE;
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
