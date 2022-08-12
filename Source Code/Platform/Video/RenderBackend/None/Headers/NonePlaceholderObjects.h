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
        noRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
          : RenderTarget(context, descriptor)
        {}
  
        void clear([[maybe_unused]] const RTClearDescriptor& descriptor) noexcept override {
        }

        void setDefaultState([[maybe_unused]] const RTDrawDescriptor& drawPolicy) noexcept override {
        }

        void readData([[maybe_unused]] const vec4<U16>& rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept override {
        }

        void blitFrom([[maybe_unused]] const RTBlitParams& params) noexcept override {
        }
    };

    class noGenericVertexData final : public GenericVertexData {
     public:
        noGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
            : GenericVertexData(context, ringBufferLength, name)
        {}

        void reset() override {

        }

        void draw([[maybe_unused]] const GenericDrawCommand& command) noexcept override {
        }

        void setBuffer([[maybe_unused]] const SetBufferParams& params) noexcept override {
        }

        void setIndexBuffer([[maybe_unused]] const IndexBuffer& indices) override {
        }

        void updateBuffer([[maybe_unused]] U32 buffer,
                          [[maybe_unused]] U32 elementCountOffset,
                          [[maybe_unused]] U32 elementCountRange,
                          [[maybe_unused]] bufferPtr data) noexcept override 
        {
        }
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
            _data._textureType = _descriptor.texType();
            _data._textureHandle = s_textureHandle.fetch_add(1u);
        }

        void bindLayer([[maybe_unused]] U8 slot, [[maybe_unused]] U8 level, [[maybe_unused]] U8 layer, [[maybe_unused]] bool layered, [[maybe_unused]] Image::Flag rw_flag) noexcept override {
        }

        void clearData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level) const noexcept override {
        }

        void clearSubData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level, [[maybe_unused]] const vec4<I32>& rectToClear, [[maybe_unused]] const vec2<I32>& depthRange) const noexcept override {
        }

        TextureReadbackData readData([[maybe_unused]] U16 mipLevel, [[maybe_unused]] GFXDataFormat desiredFormat) const noexcept override {
            TextureReadbackData data{};
            return MOV(data);
        }

        void loadDataCompressed([[maybe_unused]] const ImageTools::ImageData& imageData) {
        }

        void loadDataUncompressed([[maybe_unused]] const ImageTools::ImageData& imageData) {
        }

        void prepareTextureData([[maybe_unused]] U16 width, [[maybe_unused]] U16 height) {
        }

        void submitTextureData() {
        }
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

        BufferLock clearBytes([[maybe_unused]] BufferRange range) noexcept override {
            return {};
        }

        BufferLock writeBytes([[maybe_unused]] BufferRange range, [[maybe_unused]] bufferPtr data) noexcept override {
            return {};
        }

        void readBytes([[maybe_unused]] BufferRange range, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept override {
        }

        bool bindByteRange([[maybe_unused]] U8 bindIndex, [[maybe_unused]] BufferRange range) noexcept override {
            return true;
        }

        bool lockByteRange([[maybe_unused]] BufferRange range, [[maybe_unused]] SyncObject* sync) const override {
            return true;
        }
    };

};  // namespace Divide
#endif //_NONE_PLACEHOLDER_OBJECTS_H_
