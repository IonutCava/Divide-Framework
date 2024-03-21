

#include "config.h"

#include "Headers/PlatformDefines.h"
#include "Headers/PlatformRuntime.h"

#include "GUI/Headers/GUI.h"

#include "Utility/Headers/Localization.h"
#include "Utility/Headers/MemoryTracker.h"

#include "Platform/File/Headers/FileManagement.h"

#include <iostream>

namespace Divide
{

    namespace
    {
        SysInfo g_sysInfo;
    };

    namespace MemoryManager
    {
        void log_new( void* p, const size_t size, const char* zFile, const size_t nLine )
        {
            if constexpr ( Config::Build::IS_DEBUG_BUILD )
            {
                if ( MemoryTracker::Ready )
                {
                    AllocTracer.Add( p, size, zFile, nLine );
                }
            }
        }

        void log_delete( void* p )
        {
            if constexpr ( Config::Build::IS_DEBUG_BUILD )
            {
                if ( MemoryTracker::Ready )
                {
                    AllocTracer.Remove( p );
                }
            }
        }
    };  // namespace MemoryManager

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

                    const auto msgOut = Util::StringFormat( "ASSERT [%s : %d]: %s : %s", file, line, expressionStr, failMessage );
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
        Paths::initPaths( sysInfo() );
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
        info._workingDirectory.append( "/" );
    }

    U16 HardwareThreadCount() noexcept
    {
        return to_U16(std::max( std::thread::hardware_concurrency(), 2u ));
    }

    bool CreateDirectories( const ResourcePath& path )
    {
        return CreateDirectories( path.c_str() );
    }

    bool CreateDirectories( const char* path )
    {
        static Mutex s_DirectoryLock;

        LockGuard<Mutex> w_lock( s_DirectoryLock );
        assert( path != nullptr && strlen( path ) > 0 );
        //Always end in a '/'
        assert( path[strlen( path ) - 1] == '/' );

        vector<string> directories;
        Util::Split<vector<string>, string>( path, '/', directories );
        if ( directories.empty() )
        {
            Util::Split<vector<string>, string>( path, '\\', directories );
        }

        string previousPath = "./";
        for ( const string& dir : directories )
        {
            if ( !createDirectory( (previousPath + dir).c_str() ) )
            {
                return false;
            }
            previousPath += dir;
            previousPath += "/";
        }

        return true;
    }

    FileAndPath GetInstallLocation( char* argv0 )
    {
        if ( argv0 == nullptr || argv0[0] == 0 )
        {
            return {};
        }

        return splitPathToNameAndLocation( extractFilePathAndName( argv0 ).c_str() );
    }

    const char* GetClipboardText( [[maybe_unused]] void* user_data ) noexcept
    {
        return SDL_GetClipboardText();
    }

    void SetClipboardText( [[maybe_unused]] void* user_data, const char* text ) noexcept
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

void* operator new(const size_t size, [[maybe_unused]] const char* zFile, [[maybe_unused]] const size_t nLine)
{
    void* ptr = malloc( size );
#if defined(_DEBUG)
    Divide::MemoryManager::log_new( ptr, size, zFile, nLine );
#endif
    return ptr;
}

void operator delete(void* ptr, [[maybe_unused]] const char* zFile, [[maybe_unused]] size_t nLine)
{
#if defined(_DEBUG)
    Divide::MemoryManager::log_delete( ptr );
#endif

    free( ptr );
}

void* operator new[]( const size_t size, [[maybe_unused]] const char* zFile, [[maybe_unused]] const size_t nLine )
{
    void* ptr = malloc( size );
#if defined(_DEBUG)
    Divide::MemoryManager::log_new( ptr, size, zFile, nLine );
#endif
    return ptr;
}

void operator delete[]( void* ptr, [[maybe_unused]] const char* zFile, [[maybe_unused]] size_t nLine )
{
#if defined(_DEBUG)
    Divide::MemoryManager::log_delete( ptr );
#endif
    free( ptr );
}
