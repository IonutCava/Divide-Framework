/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_COMMAND_BUFFER_POOL_H_
#define DVD_COMMAND_BUFFER_POOL_H_

#include "CommandBuffer.h"

namespace Divide {
namespace GFX {

class CommandBufferPool
{
 public:
    CommandBufferPool(size_t poolSizeFactor);
    ~CommandBufferPool();

    Handle<CommandBuffer> allocateBuffer( const char* name, size_t reservedCmdCount );
    void deallocateBuffer(Handle<CommandBuffer>& handle );
    CommandBuffer* get( Handle<CommandBuffer> handle );

    void reset() noexcept;

 private:
    Handle<CommandBuffer> allocateBufferLocked( const char* name, size_t reservedCmdCount, bool retry = false );
 private:

    const size_t _poolSizeFactor;

    SharedMutex _mutex;
    vector<CommandBuffer*> _pool;
    vector<std::pair<bool, U8>> _freeList;

    MemoryPool<CommandBuffer, prevPOW2( sizeof( CommandBuffer ) ) * (1u << 3u)> _memPool;
};

void InitPools(const size_t poolSizeFactor);
void DestroyPools() noexcept;

Handle<CommandBuffer> AllocateCommandBuffer(const char* name, size_t reservedCmdCount = CommandBuffer::COMMAND_BUFFER_INIT_SIZE );
void DeallocateCommandBuffer(Handle<CommandBuffer>& buffer);
CommandBuffer* Get(Handle<CommandBuffer> handle);

}; //namespace GFX
}; //namespace Divide

#endif //DVD_COMMAND_BUFFER_POOL_H_

#include "CommandBufferPool.inl"
