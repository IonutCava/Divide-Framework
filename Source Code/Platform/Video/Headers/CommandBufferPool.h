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
#ifndef _COMMAND_BUFFER_POOL_H_
#define _COMMAND_BUFFER_POOL_H_

#include "CommandBuffer.h"

namespace Divide {
namespace GFX {

class CommandBufferPool {
 public:
    static constexpr size_t BufferSize = 8192 * 2;

    CommandBuffer* allocateBuffer();
    void deallocateBuffer(CommandBuffer*& buffer);

    void reset() noexcept;

 private:
    Mutex _mutex;
    MemoryPool<CommandBuffer, BufferSize> _pool;
};

class ScopedCommandBuffer : NonCopyable, NonMovable {
  public:
    ~ScopedCommandBuffer() noexcept;
    CommandBuffer& operator()() noexcept { return *_buffer; }
    const CommandBuffer& operator()() const noexcept { return *_buffer; }

  protected:
    friend ScopedCommandBuffer AllocateScopedCommandBuffer();
    ScopedCommandBuffer() noexcept;

  private:
    CommandBuffer* _buffer;
};

void InitPools() noexcept;
void DestroyPools() noexcept;
ScopedCommandBuffer AllocateScopedCommandBuffer();
CommandBuffer* AllocateCommandBuffer();
void DeallocateCommandBuffer(CommandBuffer*& buffer);

}; //namespace GFX
}; //namespace Divide

#endif //_COMMAND_BUFFER_POOL_H_

#include "CommandBufferPool.inl"