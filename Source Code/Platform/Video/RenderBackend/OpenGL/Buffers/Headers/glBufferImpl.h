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
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLStateTracker.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferParams.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferLocks.h"

namespace Divide {

struct BufferImplParams {
    BufferParams _bufferParams;
    GLenum _target{ GL_NONE };
    size_t _dataSize{0};
    bool _useChunkAllocation{ false };
};

inline bool operator==(const BufferImplParams& lhs, const BufferImplParams& rhs) noexcept {
    return lhs._bufferParams == rhs._bufferParams &&
           lhs._target == rhs._target &&
           lhs._dataSize == rhs._dataSize &&
           lhs._useChunkAllocation == rhs._useChunkAllocation;
}

inline bool operator!=(const BufferImplParams& lhs, const BufferImplParams& rhs) noexcept {
    return lhs._bufferParams != rhs._bufferParams ||
           lhs._target != rhs._target ||
           lhs._dataSize != rhs._dataSize ||
           lhs._useChunkAllocation != rhs._useChunkAllocation;
}

class glBufferImpl final : public LockableBuffer {
public:
    explicit glBufferImpl(GFXDevice& context, const BufferImplParams& params, const std::pair<bufferPtr, size_t>& initialData, const char* name);
    virtual ~glBufferImpl();

    BufferLock writeOrClearBytes(size_t offsetInBytes, size_t rangeInBytes, bufferPtr data, bool firstWrite = false);
    void readBytes(size_t offsetInBytes, size_t rangeInBytes, std::pair<bufferPtr, size_t> outData);


public:
    PROPERTY_R(BufferImplParams, params);
    PROPERTY_R(GLUtil::GLMemory::Block, memoryBlock);

protected:
    GFXDevice& _context;

    GLuint _copyBufferTarget{ GL_NULL_HANDLE };
    size_t _copyBufferSize{ 0u };
    mutable Mutex _mapLock;
};

FWD_DECLARE_MANAGED_CLASS(glBufferImpl);

}; //namespace Divide

#endif //GL_BUFFER_IMPL_H_