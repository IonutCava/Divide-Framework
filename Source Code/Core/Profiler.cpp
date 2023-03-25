#include "stdafx.h"

#include "Headers/Profiler.h"

namespace Divide::Profiler
{
    void InitAllocators()
    {
        if constexpr( detail::enabled )
        {
            OPTICK_SET_MEMORY_ALLOCATOR([](size_t size) -> void*
                                         {
                                             return xmalloc(size);
                                         },
                                         []( void* p )
                                         {
                                             xfree(p);
                                         },
                                         []()
                                         {
                                             // Thread allocator
                                             NOP();
                                         })
        }
    }

    void Shutdown()
    {
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
