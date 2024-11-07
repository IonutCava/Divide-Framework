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
    glGenericVertexData(GFXDevice& context, U16 ringBufferLength, const std::string_view name);

    void reset() override;

    [[nodiscard]] BufferLock setIndexBuffer(const IndexBuffer& indices) override;
    [[nodiscard]] BufferLock setBuffer(const SetBufferParams& params) override;
    [[nodiscard]] BufferLock updateBuffer(U32 buffer, U32 elementCountOffset, U32 elementCountRange, bufferPtr data) override;

   protected:
    friend class GFXDevice;
    friend class glVertexArray;
    void draw(const GenericDrawCommand& command, VDIUserData* data) override;

   protected:
    void bindBufferInternal(const SetBufferParams::BufferBindConfig& bindConfig);

   private:
    struct GenericBufferImpl
    {
        glBufferImpl_uptr _buffer{ nullptr };

        size_t _ringSizeFactor{ 1u };
        size_t _elementStride{ 0u };
        SetBufferParams::BufferBindConfig _bindConfig{};
    };

    struct IndexBufferEntry : public NonCopyable
    {
        IndexBuffer _data;

        glBufferImpl_uptr _buffer{ nullptr };
        size_t _ringSizeFactor{ 1u };
        size_t _elementStride{ 0u };

        size_t _bufferSize{0u};
        gl46core::GLuint _handle{ GL_NULL_HANDLE };
        gl46core::GLsync _idxBufferSync{ nullptr };
    };

    eastl::fixed_vector<IndexBufferEntry,1,true> _idxBuffers;
    eastl::fixed_vector<GenericBufferImpl,1,true> _bufferObjects;

    SharedMutex _idxBufferLock;
};

};  // namespace Divide
#endif //GL_GENERIC_VERTEX_DATA_H
