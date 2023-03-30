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
#ifndef _NONE_PLACEHOLDER_OBJECTS_H_
#define _NONE_PLACEHOLDER_OBJECTS_H_

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {
    class noRenderTarget final : public RenderTarget {
      public:
        noRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor) : RenderTarget(context, descriptor){}
    };

    class noGenericVertexData final : public GenericVertexData {
     public:
        noGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
            : GenericVertexData(context, ringBufferLength, name)
        {}

        void reset() override {}
        void draw([[maybe_unused]] const GenericDrawCommand& command, [[maybe_unused]] VDIUserData* data) noexcept override {}

        [[nodiscard]] BufferLock setIndexBuffer([[maybe_unused]] const IndexBuffer& indices) override { return {}; }
        [[nodiscard]] BufferLock setBuffer([[maybe_unused]] const SetBufferParams& params) noexcept override { return {}; }
        [[nodiscard]] BufferLock updateBuffer([[maybe_unused]] U32 buffer,
                                              [[maybe_unused]] U32 elementCountOffset,
                                              [[maybe_unused]] U32 elementCountRange,
                                              [[maybe_unused]] bufferPtr data) noexcept override{ return {}; }
    };

    class noTexture final : public Texture {
    public:
        noTexture(GFXDevice& context,
                  const size_t descriptorHash,
                  const Str256& name,
                  const ResourcePath& assetNames,
                  const ResourcePath& assetLocations,
                  const TextureDescriptor& texDescriptor,
                  ResourceCache& parentCache)
            : Texture(context, descriptorHash, name, assetNames, assetLocations, texDescriptor, parentCache)
        {
            static std::atomic_uint s_textureHandle = 1u;
        }

        void clearData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] vec2<U16> layerRange, [[maybe_unused]] U8 mipLevel ) const noexcept override {}

        [[nodiscard]] ImageReadbackData readData([[maybe_unused]] const U8 mipLevel, [[maybe_unused]] const PixelAlignment& pixelPackAlignment) const noexcept override { return {}; }

        void loadDataInternal([[maybe_unused]] const ImageTools::ImageData& imageData, [[maybe_unused]] const vec3<U16>& offset, [[maybe_unused]] const PixelAlignment& pixelUnpackAlignment ) override { }
        void loadDataInternal([[maybe_unused]] const Byte* data, [[maybe_unused]] size_t size, [[maybe_unused]] U8 targetMip, [[maybe_unused]] const vec3<U16>& offset, [[maybe_unused]] const vec3<U16>& dimensions, [[maybe_unused]] const PixelAlignment& pixelUnpackAlignment ) override {}
        void submitTextureData() override {}
    };

    class noShaderProgram final : public ShaderProgram {
    public:
        noShaderProgram(GFXDevice& context, const size_t descriptorHash,
                        const Str256& name,
                        const Str256& assetName,
                        const ResourcePath& assetLocation,
                        const ShaderProgramDescriptor& descriptor,
                        ResourceCache& parentCache)
            : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor, parentCache)
        {}
    };

    class noUniformBuffer final : public ShaderBuffer {
    public:
        noUniformBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
            : ShaderBuffer(context, descriptor)
        {}

        BufferLock writeBytesInternal([[maybe_unused]] BufferRange range, [[maybe_unused]] bufferPtr data) noexcept override { return {}; }
        void readBytesInternal([[maybe_unused]] BufferRange range, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) noexcept override {}

        [[nodiscard]] LockableBuffer* getBufferImpl() override { return nullptr; }
    };

};  // namespace Divide
#endif //_NONE_PLACEHOLDER_OBJECTS_H_
