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
#ifndef VK_UNIFORM_BUFFER_OBJECT_H_
#define VK_UNIFORM_BUFFER_OBJECT_H_

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

#include "vkBufferImpl.h"

namespace Divide {

    class vkShaderBuffer final : public ShaderBuffer {
    public:
        vkShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor);

        BufferLock clearBytes(BufferRange range) noexcept override;

        BufferLock writeBytes(BufferRange range, [[maybe_unused]] bufferPtr data) noexcept override;

        void readBytes(BufferRange range, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept override;

        bool bindByteRange(U8 bindIndex, [[maybe_unused]] BufferRange range) noexcept override;

    private:
        [[nodiscard]] inline AllocatedBuffer* bufferImpl() const { return _bufferImpl.get(); }

    private:
        eastl::unique_ptr<AllocatedBuffer> _bufferImpl{ nullptr };
    };
} //namespace Divide

#endif //VK_UNIFORM_BUFFER_OBJECT_H_