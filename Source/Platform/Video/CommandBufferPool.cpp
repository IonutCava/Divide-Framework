

#include "Headers/CommandBufferPool.h"

namespace Divide {
namespace GFX {

static CommandBufferPool g_sCommandBufferPool;

void DestroyPools() noexcept
{
    g_sCommandBufferPool.reset();
}

CommandBufferPool::~CommandBufferPool()
{
    DIVIDE_ASSERT(_bufferCount == 0);
}

void CommandBufferPool::reset() noexcept
{
    LockGuard<Mutex> lock( _mutex );
    _pool = {};
    ++_generation;
}

Handle<CommandBuffer> CommandBufferPool::allocateBuffer()
{
    LockGuard<Mutex> lock(_mutex);
    return Handle<CommandBuffer>
    {
        ._ptr = _pool.newElement(),
        ._generation = _generation,
        ._index = _bufferCount++
    };
}

void CommandBufferPool::deallocateBuffer( Handle<CommandBuffer>& buffer)
{
    if (buffer != INVALID_HANDLE<CommandBuffer>)
    {

        LockGuard<Mutex> lock(_mutex);
        if (buffer._generation == _generation)
        {
            if ( buffer._ptr != nullptr )
            {
                _pool.deleteElement( buffer._ptr );
            }
            --_bufferCount;
        }
        else
        {
            // Pool was reset and all command buffers deallocated in bulk
            NOP();
        }

        buffer = INVALID_HANDLE<CommandBuffer>;
    }
}

Handle<CommandBuffer> AllocateCommandBuffer()
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    return g_sCommandBufferPool.allocateBuffer();
}

void DeallocateCommandBuffer( Handle<CommandBuffer>& buffer)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    g_sCommandBufferPool.deallocateBuffer(buffer);
}

}; //namespace GFX
}; //namespace Divide