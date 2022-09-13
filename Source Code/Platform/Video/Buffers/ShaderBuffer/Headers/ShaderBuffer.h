/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _SHADER_BUFFER_H_
#define _SHADER_BUFFER_H_

#include "Core/Headers/RingBuffer.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferLocks.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferParams.h"
#include "Platform/Video/Headers/GraphicsResource.h"
namespace Divide {

struct ShaderBufferDescriptor;

namespace Attorney {
    class ShaderBufferBind;
};

class ShaderProgram;
class NOINITVTABLE ShaderBuffer : public GUIDWrapper,
                                  public GraphicsResource,
                                  public RingBufferSeparateWrite
                                  
{
    friend class Attorney::ShaderBufferBind;

   public:
       enum class Usage : U8 {
           CONSTANT_BUFFER = 0,
           UNBOUND_BUFFER,
           COMMAND_BUFFER,
           COUNT
       };

   public:
    explicit ShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor);

    virtual ~ShaderBuffer() = default;

                  void       readData(BufferRange range, std::pair<bufferPtr, size_t> outData);
                  void       readBytes(BufferRange range, std::pair<bufferPtr, size_t> outData);
    [[nodiscard]] BufferLock clearData(BufferRange range);
    [[nodiscard]] BufferLock clearBytes(BufferRange range);
    [[nodiscard]] BufferLock writeData(BufferRange range, bufferPtr data);
    [[nodiscard]] BufferLock writeBytes(BufferRange range, bufferPtr data);

    
    [[nodiscard]] FORCE_INLINE size_t getStartOffset(const bool read) const noexcept { return (read ? queueReadIndex() : queueWriteIndex()) * _alignedBufferSize; }
    [[nodiscard]] FORCE_INLINE U32    getPrimitiveCount()             const noexcept { return _params._elementCount; }
    [[nodiscard]] FORCE_INLINE size_t getPrimitiveSize()              const noexcept { return _params._elementSize; }
    [[nodiscard]] FORCE_INLINE Usage  getUsage()                      const noexcept { return _usage;  }

    FORCE_INLINE BufferLock writeData(bufferPtr data) { return writeData({ 0u, _params._elementCount }, data); }
    FORCE_INLINE BufferLock clearData() { return clearData({ 0u, _params._elementCount }); }

    [[nodiscard]] static size_t AlignmentRequirement(Usage usage) noexcept;

    PROPERTY_R(size_t, alignedBufferSize, 0u);
    PROPERTY_R(string, name);
    PROPERTY_R(U64, lastWrittenFrame, 0u);
    PROPERTY_R(U64, lastReadFrame, 0u);
   protected:
     virtual void readBytesInternal(BufferRange range, std::pair<bufferPtr, size_t> outData) = 0;
     virtual void writeBytesInternal(BufferRange range, bufferPtr data) = 0;

   protected:
    BufferParams _params;
    size_t _maxSize{ 0u };
    U64 _lastWriteFrameNumber{ 0u };
    const Usage _usage;
};

/// If initialData is NULL, the buffer contents are undefined (good for CPU -> GPU transfers),
/// however for GPU->GPU buffers, we may want a sane initial state to work with.
/// If _initialData is not NULL, we zero out whatever empty space is left available
/// determined by comparing the data size to the buffer size
struct ShaderBufferDescriptor {
    BufferParams _bufferParams;
    std::pair<bufferPtr, size_t> _initialData{nullptr, 0u};
    string _name{ "" };
    U32 _ringBufferLength{ 1u };
    ShaderBuffer::Usage _usage{ ShaderBuffer::Usage::COUNT };
    bool _separateReadWrite{ false }; ///< Use a separate read/write index based on queue length
};

FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

};  // namespace Divide
#endif //_SHADER_BUFFER_H_
