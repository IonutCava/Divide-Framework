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
#ifndef DVD_SHADER_BUFFER_H_
#define DVD_SHADER_BUFFER_H_

#include "Core/Headers/RingBuffer.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferLocks.h"
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
    explicit ShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor);

                  void       readData(BufferRange range, std::pair<bufferPtr, size_t> outData);
                  void       readBytes(BufferRange range, std::pair<bufferPtr, size_t> outData);
    [[nodiscard]] BufferLock clearData(BufferRange range);
    [[nodiscard]] BufferLock clearBytes(BufferRange range);
    [[nodiscard]] BufferLock writeData(BufferRange range, const bufferPtr data);
    [[nodiscard]] BufferLock writeBytes(BufferRange range, const bufferPtr data);

    
    [[nodiscard]] FORCE_INLINE I32                   getStartIndex(const bool read)  const noexcept { return (read ? queueReadIndex() : queueWriteIndex()); }
    [[nodiscard]] FORCE_INLINE size_t                getStartOffset(const bool read) const noexcept { return to_size(std::max(0, getStartIndex(read))) * _alignedBufferSize; }
    [[nodiscard]] FORCE_INLINE U32                   getPrimitiveCount()             const noexcept { return _params._elementCount; }
    [[nodiscard]] FORCE_INLINE size_t                getPrimitiveSize()              const noexcept { return _params._elementSize; }
    [[nodiscard]] FORCE_INLINE BufferUsageType       getUsage()                      const noexcept { return _params._usageType;  }
    [[nodiscard]] FORCE_INLINE BufferUpdateFrequency getUpdateFrequency()            const noexcept { return _params._updateFrequency; }


    FORCE_INLINE BufferLock writeData(const bufferPtr data) { return writeData({ 0u, _params._elementCount }, data); }
    FORCE_INLINE BufferLock clearData() { return clearData({ 0u, _params._elementCount }); }

    [[nodiscard]] virtual LockableBuffer* getBufferImpl() = 0;

    [[nodiscard]] static size_t AlignmentRequirement( BufferUsageType usage) noexcept;

    PROPERTY_R(size_t, alignedBufferSize, 0u);
    PROPERTY_R(size_t, alignmentRequirement, sizeof(U32));

    PROPERTY_R(string, name);
    PROPERTY_R(U64, lastWrittenFrame, 0u);
    PROPERTY_R(U64, lastReadFrame, 0u);
   protected:
     virtual void       readBytesInternal(BufferRange range, std::pair<bufferPtr, size_t> outData) = 0;
     virtual BufferLock writeBytesInternal(BufferRange range, const bufferPtr data) = 0;

   protected:
    BufferParams _params;
    size_t _maxSize{ 0u };
    U64 _lastWriteFrameNumber{ 0u };
};

/// If initialData is NULL, the buffer contents are undefined (good for CPU -> GPU transfers),
/// however for GPU->GPU buffers, we may want a sane initial state to work with.
/// If _initialData is not NULL, we zero out whatever empty space is left available
/// determined by comparing the data size to the buffer size
struct ShaderBufferDescriptor {
    BufferParams _bufferParams;
    std::pair<bufferPtr, size_t> _initialData{nullptr, 0u};
    string _name{ "" };
    U16 _ringBufferLength{ 1u };
    bool _separateReadWrite{ false }; ///< Use a separate read/write index based on queue length
};

FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

};  // namespace Divide
#endif //DVD_SHADER_BUFFER_H_
