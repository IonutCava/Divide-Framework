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
#ifndef GL_SHADER_BUFFER_OBJECT_H_
#define GL_SHADER_BUFFER_OBJECT_H_

#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

namespace Divide {

FWD_DECLARE_MANAGED_CLASS(glBufferImpl);

/// Base class for shader uniform blocks
class glShaderBuffer final : public ShaderBuffer {
    public:
        glShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor);
        ~glShaderBuffer() = default;

        BufferLock clearBytes(BufferRange range) override;
        void readBytes(BufferRange range, std::pair<bufferPtr, size_t> outData) const override;
        BufferLock writeBytes(BufferRange range, bufferPtr data) override;

        [[nodiscard]] bool bindByteRange(DescriptorSetUsage set, U8 bindIndex, BufferRange range) override;
        [[nodiscard]] inline glBufferImpl* bufferImpl() const { return _bufferImpl.get(); }

    private:
        glBufferImpl_uptr _bufferImpl{ nullptr };
};

};  // namespace Divide

#endif //GL_SHADER_BUFFER_OBJECT_H_
