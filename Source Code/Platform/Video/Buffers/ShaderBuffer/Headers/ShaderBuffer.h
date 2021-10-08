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
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"
#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"

namespace Divide {

struct ShaderBufferDescriptor;

class ShaderProgram;
class NOINITVTABLE ShaderBuffer : public GUIDWrapper,
                                  public GraphicsResource,
                                  public RingBufferSeparateWrite
{
   public:
       enum class Usage : U8 {
           CONSTANT_BUFFER = 0,
           UNBOUND_BUFFER,
           COMMAND_BUFFER,
           ATOMIC_COUNTER,
           COUNT
       };

       enum class Flags : U8 {
           NONE = 0,
           EXPLICIT_RANGE_FLUSH = toBit(1),  ///< Flush the freshly written buffer region after a write (Will use coherent mapping if not set. Coherent mapping MAY be slower)
           NO_SYNC = toBit(2),               ///< Skip any kind of sync between reads and writes. Useful if using stuff that auto-syncs like glBufferSubData in OpenGL
           COUNT = 4
       };

   public:
    explicit ShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor);

    virtual ~ShaderBuffer() = default;

    virtual void clearData(U32 offsetElementCount,
                           U32 rangeElementCount);

    virtual void writeData(bufferPtr data);

    virtual void writeData(U32 offsetElementCount,
                           U32 rangeElementCount,
                           bufferPtr data);

    virtual void readData(U32 offsetElementCount,
                          U32 rangeElementCount,
                          bufferPtr result) const;

    virtual void clearBytes(ptrdiff_t offsetInBytes,
                            ptrdiff_t rangeInBytes) = 0;

    virtual void writeBytes(ptrdiff_t offsetInBytes,
                            ptrdiff_t rangeInBytes,
                            bufferPtr data) = 0;

    virtual void readBytes(ptrdiff_t offsetInBytes,
                           ptrdiff_t rangeInBytes,
                           bufferPtr result) const = 0;

    virtual bool bindByteRange(U8 bindIndex,
                               ptrdiff_t offsetInBytes,
                               ptrdiff_t rangeInBytes) = 0;

    /// Bind return false if the buffer was already bound
    virtual bool bind(U8 bindIndex);

    bool bind(ShaderBufferLocation bindIndex);

    bool bindRange(U8 bindIndex,
                   U32 offsetElementCount,
                   U32 rangeElementCount);

    bool bindRange(ShaderBufferLocation bindIndex,
                   U32 offsetElementCount,
                   U32 rangeElementCount);

    U32 getPrimitiveCount() const noexcept { return _params._elementCount; }
    size_t getPrimitiveSize() const noexcept { return _params._elementSize; }

    static size_t alignmentRequirement(Usage usage);

   protected:
    BufferParams _params;
    size_t _maxSize = 0;

    static size_t s_boundAlignmentRequirement;
    static size_t s_unboundAlignmentRequirement;

    const U32 _flags;
    const Usage _usage;

    string _name;
};

/// If initialData is NULL, the buffer contents are undefined (good for CPU -> GPU transfers),
/// however for GPU->GPU buffers, we may want a sane initial state to work with.
/// If _initialData is not NULL, we zero out whatever empty space is left available
/// determined by comparing the data size to the buffer size
/// 
/// _bufferParams._sync will be overriden by the NO_SYNC flag or UpdateFrequency::ONCE if it is set to true only!
/// if _bufferParams._sync is set to false, the buffer will not use sync mechanism regardless of the NO_SYNC flag (see ShaderBuffer constructor)
struct ShaderBufferDescriptor {
    BufferParams _bufferParams;
    string _name = "";
    U32 _flags = to_U32(ShaderBuffer::Flags::NONE);
    U32 _ringBufferLength = 1u;
    ShaderBuffer::Usage _usage = ShaderBuffer::Usage::COUNT;
    bool _separateReadWrite = false; ///< Use a separate read/write index based on queue length
};
};  // namespace Divide
#endif //_SHADER_BUFFER_H_
