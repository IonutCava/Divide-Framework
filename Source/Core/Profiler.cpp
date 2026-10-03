

#include "Headers/Profiler.h"
#include "Core/Headers/Application.h"

namespace Divide::Profiler
{
#if USE_OPTICK

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
#endif //USE_OPTICK

    void RegisterApp( Application * app )
    {
#       if USE_OPTICK
            g_appPtr = app;
#       else //USE_OPTICK
            DIVIDE_UNUSED(app);
#       endif //USE_OPTICK
    }

    void Initialise()
    {
#       if USE_OPTICK
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
#       endif //USE_OPTICK
    }

    void Shutdown()
    {
#       if USE_OPTICK
            g_appPtr = nullptr;
            OPTICK_SHUTDOWN()
#       endif //USE_OPTICK
    }

    void OnThreadStart( const std::string_view threadName )
    {
#       if USE_OPTICK
            OPTICK_START_THREAD(threadName.data())
#       else //USE_OPTICK
            DIVIDE_UNUSED(threadName);
#       endif //USE_OPTICK
    }
    void OnThreadStop()
    {
#       if USE_OPTICK
            OPTICK_STOP_THREAD()
#       endif //USE_OPTICK
    }
}; //namespace Divide::Profiler
