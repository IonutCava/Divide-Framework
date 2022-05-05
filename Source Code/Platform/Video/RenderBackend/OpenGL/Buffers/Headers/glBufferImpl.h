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
#ifndef _GL_BUFFER_IMPL_H_
#define _GL_BUFFER_IMPL_H_

#include "glMemoryManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glLockManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLStateTracker.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"

namespace Divide {

struct BufferImplParams {
    BufferParams _bufferParams;
    GLenum _target = GL_NONE;
    size_t _dataSize = 0;
    bool _useChunkAllocation = false;
    const char* _name = nullptr;
};

struct BufferMapRange
{
    size_t _offset = 0;
    size_t _range = 0;
};

class glBufferImpl final : public glObject, public GUIDWrapper {
public:
    explicit glBufferImpl(GFXDevice& context, const BufferImplParams& params);
    virtual ~glBufferImpl();

    // Returns false if we encounter an error
    [[nodiscard]] bool lockByteRange(size_t offsetInBytes, size_t rangeInBytes, SyncObject* syncObject);
    [[nodiscard]] bool waitByteRange(size_t offsetInBytes, size_t rangeInBytes, bool blockClient);

    void writeOrClearBytes(size_t offsetInBytes, size_t rangeInBytes, bufferPtr data, bool zeroMem);
    void readBytes(size_t offsetInBytes, size_t rangeInBytes, bufferPtr data);

public:
    PROPERTY_R(BufferImplParams, params);
    
    PROPERTY_R(GLUtil::GLMemory::Block, memoryBlock);

protected:
    GFXDevice& _context;

    GLuint _copyBufferTarget = GLUtil::k_invalidObjectID;
    glLockManager _lockManager;
};
}; //namespace Divide

#endif //_GL_BUFFER_IMPL_H_