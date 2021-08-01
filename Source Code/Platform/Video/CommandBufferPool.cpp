#include "stdafx.h"

#include "Headers/CommandBufferPool.h"

namespace Divide {
namespace GFX {

static CommandBufferPool g_sCommandBufferPool;

void InitPools() {
    g_sCommandBufferPool.reset();
}

void DestroyPools() {
    g_sCommandBufferPool.reset();
}

void CommandBufferPool::reset() {
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

ScopedCommandBuffer::ScopedCommandBuffer()
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