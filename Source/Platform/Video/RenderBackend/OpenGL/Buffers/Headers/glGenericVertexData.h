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
#include "Platform/Video/Buffers/VertexBuffer/Headers/GPUBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

class glGPUBuffer final : public GPUBuffer
{
    struct GenericBufferImpl;

   public:
    glGPUBuffer(GFXDevice& context, U16 ringBufferLength, const std::string_view name);

    [[nodiscard]] BufferLock setBuffer(const SetBufferParams& params) override;
    [[nodiscard]] BufferLock updateBuffer(U32 elementCountOffset,
                                          U32 elementCountRange,
                                          bufferPtr data) override;

   private:
    friend class GL_API;
    glBufferImpl_uptr _internalBuffer{ nullptr };

    SharedMutex _bufferLock;
};

};  // namespace Divide
#endif //GL_GENERIC_VERTEX_DATA_H
