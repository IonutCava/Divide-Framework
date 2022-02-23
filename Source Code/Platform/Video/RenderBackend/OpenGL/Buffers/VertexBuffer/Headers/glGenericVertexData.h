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
#ifndef _GL_GENERIC_VERTEX_DATA_H
#define _GL_GENERIC_VERTEX_DATA_H

#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"

#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glGenericBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

class glGenericVertexData final : public GenericVertexData {
    struct BufferBindConfig {
        BufferBindConfig() noexcept : BufferBindConfig(0, 0, 0)
        {
        }

        BufferBindConfig(const GLuint buffer,
                         const size_t offset,
                         const size_t stride) noexcept 
            : _stride(stride),
              _offset(offset),
              _buffer(buffer)
        {
        }

        void set(const BufferBindConfig& other) noexcept {
            _buffer = other._buffer;
            _offset = other._offset;
            _stride = other._stride;
        }

        bool operator==(const BufferBindConfig& other) const noexcept {
            return _buffer == other._buffer &&
                   _offset == other._offset &&
                   _stride == other._stride;
        }

        size_t _stride;
        size_t _offset;
        GLuint _buffer;
    };
    struct glVertexDataIndexContainer {
        vector_fast<size_t> _countData;
        vector_fast<GLuint> _indexOffsetData;
    };
   public:
    glGenericVertexData(GFXDevice& context, U32 ringBufferLength, const char* name = nullptr);
    ~glGenericVertexData();

    void create(U8 numBuffers = 1) override;
    void reset() override;

    void setIndexBuffer(const IndexBuffer& indices, BufferUpdateFrequency updateFrequency) override;

    void updateIndexBuffer(const IndexBuffer& indices) override;

    void setBuffer(const SetBufferParams& params) override;

    void updateBuffer(U32 buffer, U32 elementCountOffset, U32 elementCountRange, bufferPtr data) override;

   protected:
    friend class GFXDevice;
    friend class glVertexArray;
    void draw(const GenericDrawCommand& command) override;

   protected:
    void bindBufferInternal(U32 bufferIdx, U32 location);
    void bindBuffersInternal();
    void setAttributeInternal(const GenericDrawCommand& command, AttributeDescriptor& descriptor);

    void rebuildCountAndIndexData(U32 drawCount,
                                  U32 indexCount,
                                  U32 firstIndex,
                                  size_t indexBufferSize);

   private:
    glVertexDataIndexContainer _indexInfo;
    vector<glGenericBuffer*> _bufferObjects;
    GLuint _indexBuffer = 0u;
    GLuint _indexBufferSize = 0u;
    GLuint _lastDrawCount = 0u;
    GLuint _lastIndexCount = 0u;
    GLuint _lastFirstIndex = 0u;
    GLenum _indexBufferUsage = GL_NONE;
    GLenum _indexDataType = GL_NONE;
};

};  // namespace Divide
#endif
