

#include "Headers/Profiler.h"
#include "Core/Headers/Application.h"

namespace Divide::Profiler
{
#if ENABLE_OPTICK_PROFILER

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

            default:
            case Optick::State::DUMP_CAPTURE: break;
        }

        return OnProfilerStateChanged( Profiler::State::COUNT );
    }
#endif //ENABLE_OPTICK_PROFILER

    void RegisterApp( Application * app )
    {
#       if ENABLE_OPTICK_PROFILER
            g_appPtr = app;
#       else //ENABLE_OPTICK_PROFILER
            DIVIDE_UNUSED(app);
#       endif //ENABLE_OPTICK_PROFILER
    }

    void Initialise()
    {
#       if ENABLE_OPTICK_PROFILER
#           if defined(ENABLE_MIMALLOC)
                OPTICK_SET_MEMORY_ALLOCATOR([](size_t size) -> void*
                                             {
                                                 return mi_new(size);
                                             },
                                             []( void* p )
                                             {
                                                 mi_free(p);
                                             },
                                             []()
                                             {
                                                 // Thread allocator
                                                 NOP();
                                             })
#           endif //ENABLE_MIMALLOC
            if constexpr (g_TrackOptickStateChange)
            {
                OPTICK_SET_STATE_CHANGED_CALLBACK( OnOptickStateChanged )
            }
#       endif //ENABLE_OPTICK_PROFILER
    }

    void Shutdown()
    {
#       if ENABLE_OPTICK_PROFILER
            g_appPtr = nullptr;
            OPTICK_SHUTDOWN()
#       endif //ENABLE_OPTICK_PROFILER
    }

    void OnThreadStart( const std::string_view threadName )
    {
#       if ENABLE_OPTICK_PROFILER
            OPTICK_START_THREAD(threadName.data())
#       else //ENABLE_OPTICK_PROFILER
            DIVIDE_UNUSED(threadName);
#       endif //ENABLE_OPTICK_PROFILER
    }
    void OnThreadStop()
    {
#       if ENABLE_OPTICK_PROFILER
            OPTICK_STOP_THREAD()
#       endif //ENABLE_OPTICK_PROFILER
    }
}; //namespace Divide::Profiler
