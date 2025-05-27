

#include "Headers/CommandBufferPool.h"

namespace Divide {
namespace GFX {

static CommandBufferPool* g_sCommandBufferPool = nullptr;

void InitPools(const size_t poolSizeFactor)
{
    DIVIDE_ASSERT(g_sCommandBufferPool == nullptr);

    g_sCommandBufferPool = new CommandBufferPool(poolSizeFactor);
}

void DestroyPools() noexcept
{
    delete g_sCommandBufferPool;
}

CommandBufferPool::CommandBufferPool(const size_t poolSizeFactor)
    : _poolSizeFactor( poolSizeFactor )
{
    _pool.resize( _poolSizeFactor, nullptr);
    _freeList.resize( _poolSizeFactor, std::make_pair(true, U8_ZERO));
}

CommandBufferPool::~CommandBufferPool()
{
}

void CommandBufferPool::reset() noexcept
{
    LockGuard<SharedMutex> lock( _mutex );
    for (auto& it : _freeList )
    {
        it.first = true; // is available = true
        ++it.second;     // inc generation
    }
}

Handle<CommandBuffer> CommandBufferPool::allocateBuffer( const char* name, const size_t reservedCmdCount )
{
    LockGuard<SharedMutex> lock(_mutex);
    return allocateBufferLocked(name, reservedCmdCount);
}

Handle<CommandBuffer> CommandBufferPool::allocateBufferLocked( const char* name, size_t reservedCmdCount, bool retry )
{
    Handle<CommandBuffer> ret{};

    bool found = false;
    for (auto& it : _freeList)
    {
        if (it.first)
        {
            it.first = false;
            ret._generation = it.second;
            CommandBuffer*& buf = _pool[ret._index];
            if (buf)
            {
                buf->clear( name, reservedCmdCount );
            }
            else
            {
                buf = _memPool.newElement();
            }
            found = true;
            break;
        }
        ++ret._index;
    }

    if (!found)
    {
        DIVIDE_EXPECTED_CALL(!retry);

        const size_t newSize = _pool.size() + _poolSizeFactor;
        _pool.resize( newSize, nullptr );
        _freeList.resize( newSize, std::make_pair( true, U8_ZERO ) );
        return allocateBufferLocked(name, reservedCmdCount, true);
    }

    return ret;
}

void CommandBufferPool::deallocateBuffer( Handle<CommandBuffer>& handle )
{
    if ( handle != INVALID_HANDLE<CommandBuffer>)
    {
        LockGuard<SharedMutex> lock(_mutex);
        auto& it = _freeList[handle._index];
        if (it.second == handle._generation)
        {
            it.first = true;
            ++it.second;
        }
        else
        {
            // Pool was reset and all command buffers deallocated in bulk
            NOP();
        }

        handle = INVALID_HANDLE<CommandBuffer>;
    }
}

CommandBuffer* CommandBufferPool::get( Handle<CommandBuffer> handle )
{
    if ( handle != INVALID_HANDLE<CommandBuffer> )
    {
        SharedLock<SharedMutex> lock( _mutex );
        if ( _freeList[handle._index].second == handle._generation )
        {
            return _pool[handle._index];
        }
    }

    return nullptr;
}

CommandBuffer* Get( Handle<CommandBuffer> handle )
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    return g_sCommandBufferPool->get( handle );
}

Handle<CommandBuffer> AllocateCommandBuffer(const char* name, const size_t reservedCmdCount)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    return g_sCommandBufferPool->allocateBuffer(name, reservedCmdCount);
}

void DeallocateCommandBuffer( Handle<CommandBuffer>& buffer)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    g_sCommandBufferPool->deallocateBuffer(buffer);
}

}; //namespace GFX
}; //namespace Divide
