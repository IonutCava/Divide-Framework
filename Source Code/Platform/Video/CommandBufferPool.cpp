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
    OPTICK_EVENT();

    return ScopedCommandBuffer();
}

CommandBuffer* AllocateCommandBuffer() {
    return g_sCommandBufferPool.allocateBuffer();
}

void DeallocateCommandBuffer(CommandBuffer*& buffer) {
    g_sCommandBufferPool.deallocateBuffer(buffer);
}

}; //namespace GFX
}; //namespace Divide