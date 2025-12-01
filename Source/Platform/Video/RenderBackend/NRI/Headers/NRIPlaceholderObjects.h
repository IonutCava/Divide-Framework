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
#ifndef DVD_NRI_PLACEHOLDER_OBJECTS_H_
#define DVD_NRI_PLACEHOLDER_OBJECTS_H_

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {
    class nriRenderTarget final : public RenderTarget {
      public:
        nriRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor) : RenderTarget(context, descriptor){}
    };

    class nriGPUBuffer final : public GPUBuffer {
     public:
         nriGPUBuffer(GFXDevice& context, const U16 ringBufferLength, const std::string_view name)
            : GPUBuffer(context, ringBufferLength, name)
        {}

        [[nodiscard]] BufferLock updateBuffer([[maybe_unused]] U32 elementCountOffset,
                                              [[maybe_unused]] U32 elementCountRange,
                                              [[maybe_unused]] bufferPtr data) noexcept override{ return {}; }
    };

    class nriTexture final : public Texture {
    public:
        nriTexture( PlatformContext& context, const ResourceDescriptor<Texture>& descriptor )
            : Texture(context, descriptor)
        {
        }

        [[nodiscard]] ImageReadbackData readData([[maybe_unused]] const U16 mipLevel, [[maybe_unused]] const PixelAlignment& pixelPackAlignment) const noexcept override { return {}; }

        void loadDataInternal([[maybe_unused]] const ImageTools::ImageData& imageData, [[maybe_unused]] const vec3<U16>& offset, [[maybe_unused]] const PixelAlignment& pixelUnpackAlignment ) override { }
        void loadDataInternal([[maybe_unused]] const std::span<const Byte> data, [[maybe_unused]] U16 targetMip, [[maybe_unused]] const vec3<U16>& offset, [[maybe_unused]] const vec3<U16>& dimensions, [[maybe_unused]] const PixelAlignment& pixelUnpackAlignment ) override {}
    };

    class nriShaderProgram final : public ShaderProgram {
    public:
        nriShaderProgram( PlatformContext& context, const ResourceDescriptor<ShaderProgram>& descriptor )
            : ShaderProgram(context, descriptor)
        {
        }
    };

    class nriUniformBuffer final : public ShaderBuffer {
    public:
        nriUniformBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
            : ShaderBuffer(context, descriptor)
        {}

        BufferLock writeBytesInternal([[maybe_unused]] BufferRange<> range, [[maybe_unused]] const bufferPtr data) noexcept override { return {}; }
        void readBytesInternal([[maybe_unused]] BufferRange<> range, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) noexcept override {}

        [[nodiscard]] LockableBuffer* getBufferImpl() override { return nullptr; }
    };

};  // namespace Divide

#endif //DVD_NRI_PLACEHOLDER_OBJECTS_H_
