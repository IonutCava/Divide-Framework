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
#ifndef GL_GENERIC_VERTEX_DATA_H
#define GL_GENERIC_VERTEX_DATA_H

#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

class glGenericVertexData final : public GenericVertexData {
   public:
    glGenericVertexData(GFXDevice& context, U32 ringBufferLength, const char* name = nullptr);
    ~glGenericVertexData();

    void reset() override;

    void setIndexBuffer(const IndexBuffer& indices) override;

    void setBuffer(const SetBufferParams& params) override;

    void insertFencesIfNeeded() override;

    void updateBuffer(U32 buffer, U32 elementCountOffset, U32 elementCountRange, bufferPtr data) override;

   protected:
    friend class GFXDevice;
    friend class glVertexArray;
    void draw(const GenericDrawCommand& command) override;

   protected:
    void bindBufferInternal(const SetBufferParams::BufferBindConfig& bindConfig);

    void lockBuffersInternal(bool force);

    void rebuildCountAndIndexData(U32 drawCount,
                                  GLsizei indexCount,
                                  U32 firstIndex,
                                  size_t indexBufferSize);

   private:
    struct genericBufferImpl {
        glBufferImpl_uptr _buffer{ nullptr };
        size_t _ringSizeFactor{ 1u };
        size_t _elementStride{ 0u };
        BufferRange _writtenRange{};
        SetBufferParams::BufferBindConfig _bindConfig{};
        bool _usedAfterWrite{ false };
        bool _useAutoSyncObjects{ true };
    };

    struct glVertexDataIndexContainer {
        vector_fast<GLsizei> _countData;
        vector_fast<GLuint> _indexOffsetData;
    } _indexInfo;

    vector<std::pair<IndexBuffer, GLuint>> _idxBuffers;
    vector<genericBufferImpl> _bufferObjects;
    GLuint _indexBufferSize{ 0u };
    GLuint _lastDrawCount{ 0u };
    GLsizei _lastIndexCount{ 0 };
    GLuint _lastFirstIndex{ 0u };
    glLockManager _lockManager;
};

};  // namespace Divide
#endif //GL_GENERIC_VERTEX_DATA_H
