

#include "Headers/Profiler.h"
#include "Core/Headers/Application.h"

namespace Divide::Profiler
{
    namespace
    {
        Divide::Application* g_appPtr = nullptr;
    }

    bool OnProfilerStateChanged( const Profiler::State state )
    {
        if ( g_appPtr != nullptr ) [[likely]]
        {
            return Attorney::ApplicationProfiler::onProfilerStateChanged( g_appPtr, state);
        }

        return true;
    }

    static constexpr bool g_TrackOptickStateChange = false;
    static bool OnOptickStateChanged( const Optick::State::Type state )
    {
        switch(state)
        {
            case Optick::State::START_CAPTURE:  return OnProfilerStateChanged( Profiler::State::STARTED );
            case Optick::State::STOP_CAPTURE:
            case Optick::State::CANCEL_CAPTURE: return OnProfilerStateChanged( Profiler::State::STOPPED );
            default: break;
        }

        return OnProfilerStateChanged( Profiler::State::COUNT );
    }

    void RegisterApp( Application * app )
    {
        g_appPtr = app;
    }

    void Initialise()
    {
        constexpr bool USE_XMALLOC = false;

        if constexpr( detail::enabled )
        {
            OPTICK_SET_MEMORY_ALLOCATOR([](size_t size) -> void*
                                         {
                                             if constexpr ( USE_XMALLOC )
                                             {
                                                 return xmalloc( size );
                                             }

                                             return operator new(size);
                                         },
                                         []( void* p )
                                         {
                                             if constexpr ( USE_XMALLOC )
                                             {
                                                 xfree( p );
                                             }
                                             else
                                             {
                                                 operator delete(p);
                                             }
                                         },
                                         []()
                                         {
                                             // Thread allocator
                                             NOP();
                                         })
            if constexpr (g_TrackOptickStateChange)
            {
                OPTICK_SET_STATE_CHANGED_CALLBACK( OnOptickStateChanged );
            }
        }
    }

    void Shutdown()
    {
        g_appPtr = nullptr;

        if constexpr (detail::enabled)
        {
            OPTICK_SHUTDOWN();
        }
    }

    void OnThreadStart( const std::string_view threadName )
    {
        if constexpr( detail::enabled )
        {
            OPTICK_START_THREAD(threadName.data());
        }
    }
    void OnThreadStop()
    {
        if constexpr( detail::enabled )
        {
            OPTICK_STOP_THREAD();
        }
    }
}; //namespace Divide::Profiler
