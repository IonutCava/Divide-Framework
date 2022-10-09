#include "stdafx.h"

#include "Headers/CommandBufferPool.h"

namespace Divide {
namespace GFX {

static CommandBufferPool g_sCommandBufferPool;

void InitPools() noexcept {
    g_sCommandBufferPool.reset();
}

void DestroyPools() noexcept {
    g_sCommandBufferPool.reset();
}

void CommandBufferPool::reset()  noexcept {
    _pool = {};
}

CommandBuffer* CommandBufferPool::allocateBuffer() {
    ScopedLock<Mutex> lock(_mutex);
    return _pool.newElement();
}

void CommandBufferPool::deallocateBuffer(CommandBuffer*& buffer) {
    if (buffer != nullptr) {
        ScopedLock<Mutex> lock(_mutex);
        _pool.deleteElement(buffer);
        buffer = nullptr;
    }
}

ScopedCommandBuffer::ScopedCommandBuffer() noexcept
    : _buffer(AllocateCommandBuffer())
{
}

ScopedCommandBuffer::~ScopedCommandBuffer()
{
    DeallocateCommandBuffer(_buffer);
}


ScopedCommandBuffer AllocateScopedCommandBuffer() {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    return ScopedCommandBuffer();
}

CommandBuffer* AllocateCommandBuffer() {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    return g_sCommandBufferPool.allocateBuffer();
}

void DeallocateCommandBuffer(CommandBuffer*& buffer) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    g_sCommandBufferPool.deallocateBuffer(buffer);
}

}; //namespace GFX
}; //namespace Divide