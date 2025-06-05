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
#ifndef GL_BUFFER_IMPL_H_
#define GL_BUFFER_IMPL_H_

#include "glMemoryManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glLockManager.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferParams.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferLocks.h"

namespace Divide {

struct BufferImplParams : BufferParams
{
    gl46core::GLenum _target{ gl46core::GL_NONE };
    size_t _dataSize{0};

    bool operator==(const BufferImplParams& other) const = default;
};

class GFXDevice;
class glBufferImpl final : public LockableBuffer {
public:
    explicit glBufferImpl(GFXDevice& context, const BufferImplParams& params, const std::pair<const bufferPtr, size_t>& initialData, std::string_view name);
    virtual ~glBufferImpl() override;

    BufferLock writeOrClearBytes(size_t offsetInBytes, size_t rangeInBytes, const bufferPtr data);
    void readBytes(size_t offsetInBytes, size_t rangeInBytes, std::pair<bufferPtr, size_t> outData);

    size_t getDataOffset() const;
    size_t getDataSize() const;
    bufferPtr getDataPtr() const;
    gl46core::GLuint getBufferHandle() const;

    BufferImplParams _params{};

protected:
    GFXDevice& _context;
    mutable Mutex _dataLock;

    GLUtil::GLMemory::Block _memoryBlock;
    GLUtil::GLMemory::DeviceAllocator* _allocator{nullptr};
    size_t _copyBufferSize{ 0u };
    gl46core::GLuint _copyBufferTarget{ GL_NULL_HANDLE };
};

FWD_DECLARE_MANAGED_CLASS(glBufferImpl);

}; //namespace Divide

#endif //GL_BUFFER_IMPL_H_
