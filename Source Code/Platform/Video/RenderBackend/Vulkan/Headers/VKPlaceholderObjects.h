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
#ifndef _VK_PLACEHOLDER_OBJECTS_H_
#define _VK_PLACEHOLDER_OBJECTS_H_

#include "Platform/Video/Buffers/PixelBuffer/Headers/PixelBuffer.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {
    class vkRenderTarget final : public RenderTarget {
      public:
        vkRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
            : RenderTarget(context, descriptor)
        {}

        void clear([[maybe_unused]] const RTClearDescriptor& descriptor) override {
        }

        void setDefaultState([[maybe_unused]] const RTDrawDescriptor& drawPolicy) override {
        }

        void readData([[maybe_unused]] const vec4<U16>& rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] bufferPtr outData) const override {
        }

        void blitFrom([[maybe_unused]] const RTBlitParams& params) override {
        }
    };

    class vkIMPrimitive final : public IMPrimitive {
    public:
        vkIMPrimitive(GFXDevice& context)
            : IMPrimitive(context)
        {}

        void draw([[maybe_unused]] const GenericDrawCommand& cmd) override {
        }

        void beginBatch([[maybe_unused]] bool reserveBuffers, [[maybe_unused]] U32 vertexCount, [[maybe_unused]] U32 attributeCount) override {
        }

        void begin([[maybe_unused]] PrimitiveType type) override {
        }

        void vertex([[maybe_unused]] F32 x, [[maybe_unused]] F32 y, [[maybe_unused]] F32 z) override {
        }

        void attribute1i([[maybe_unused]] U32 attribLocation, [[maybe_unused]] I32 value) override {
        }

        void attribute1f([[maybe_unused]] U32 attribLocation, [[maybe_unused]] F32 value) override {
        }

        void attribute4ub([[maybe_unused]] U32 attribLocation, [[maybe_unused]] U8 x, [[maybe_unused]] U8 y, [[maybe_unused]] U8 z, [[maybe_unused]] U8 w) override {
        }

        void attribute4f([[maybe_unused]] U32 attribLocation, [[maybe_unused]] F32 x, [[maybe_unused]] F32 y, [[maybe_unused]] F32 z, [[maybe_unused]] F32 w) override {
        }

        void end() override {}
        void endBatch() override {}
        void clearBatch() override {}
        bool hasBatch() const override { return false; }
    };

    class vkVertexBuffer final : public VertexBuffer {
      public:
        vkVertexBuffer(GFXDevice& context)
            : VertexBuffer(context)
        {}

        void draw([[maybe_unused]] const GenericDrawCommand& command) override {}

        bool queueRefresh() override { return refresh(); }

      protected:
        bool refresh() override { return true; }
    };


    class vkPixelBuffer final : public PixelBuffer {
    public:
        vkPixelBuffer(GFXDevice& context, const PBType type, const char* name)
            : PixelBuffer(context, type, name)
        {}

        bool create([[maybe_unused]] U16 width,
                    [[maybe_unused]] U16 height,
                    [[maybe_unused]] U16 depth = 0,
                    [[maybe_unused]] GFXImageFormat formatEnum = GFXImageFormat::RGBA,
                    [[maybe_unused]] GFXDataFormat dataTypeEnum = GFXDataFormat::FLOAT_32,
                    [[maybe_unused]]  bool normalized = true) override
        {
            return true;
        }

        void updatePixels([[maybe_unused]] const F32* pixels, [[maybe_unused]] U32 pixelCount) override {
        }
    };


    class vkGenericVertexData final : public GenericVertexData {
    public:
        vkGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
            : GenericVertexData(context, ringBufferLength, name)
        {}

        void create([[maybe_unused]] U8 numBuffers = 1) override {
        }

        void draw([[maybe_unused]] const GenericDrawCommand& command) override {
        }

        void setBuffer([[maybe_unused]] const SetBufferParams& params) override {
        }

        void updateBuffer([[maybe_unused]] U32 buffer,
                          [[maybe_unused]] U32 elementCountOffset,
                          [[maybe_unused]] U32 elementCountRange,
                          [[maybe_unused]] bufferPtr data) override
        {
        }

        void lockBuffers() override {}

        bool waitBufferRange([[maybe_unused]] U32 buffer, [[maybe_unused]] U32 elementCountOffset, [[maybe_unused]] U32 elementCountRange, [[maybe_unused]] bool blockClient) override {
            return false;
        }
    };

    class vkTexture final : public Texture {
    public:
        vkTexture(GFXDevice& context,
                  const size_t descriptorHash,
                  const Str256& name,
                  const ResourcePath& assetNames,
                  const ResourcePath& assetLocations,
                  const bool isFlipped,
                  const bool asyncLoad,
                  const TextureDescriptor& texDescriptor)
            : Texture(context, descriptorHash, name, assetNames, assetLocations, isFlipped, asyncLoad, texDescriptor)
        {}

        [[nodiscard]] SamplerAddress getGPUAddress([[maybe_unused]] size_t samplerHash) override {
            return 0u;
        }

        void bindLayer([[maybe_unused]] U8 slot, [[maybe_unused]] U8 level, [[maybe_unused]] U8 layer, [[maybe_unused]] bool layered, [[maybe_unused]] Image::Flag rw_flag) override {
        }

        void loadData([[maybe_unused]] const ImageTools::ImageData& imageLayers) override {
        }

        void loadData([[maybe_unused]] const std::pair<Byte*, size_t>& ptr, [[maybe_unused]] const vec2<U16>& dimensions) override {
        }

        void clearData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level) const override {
        }

        void clearSubData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level, [[maybe_unused]] const vec4<I32>& rectToClear, [[maybe_unused]] const vec2<I32>& depthRange) const override {
        }

        std::pair<std::shared_ptr<Byte[]>, size_t> readData([[maybe_unused]] U16 mipLevel, [[maybe_unused]] GFXDataFormat desiredFormat) const override {
            return { nullptr, 0 };
        }
    };

    class vkShaderProgram final : public ShaderProgram {
    public:
        vkShaderProgram(GFXDevice& context, 
                        const size_t descriptorHash,
                        const Str256& name,
                        const Str256& assetName,
                        const ResourcePath& assetLocation,
                        const ShaderProgramDescriptor& descriptor,
                        const bool asyncLoad)
            : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor, asyncLoad)
        {}

        bool isValid() const override { return true; }
    };


    class vkUniformBuffer final : public ShaderBuffer {
    public:
        vkUniformBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
            : ShaderBuffer(context, descriptor)
        {}

        void clearBytes([[maybe_unused]] ptrdiff_t offsetInBytes, [[maybe_unused]] ptrdiff_t rangeInBytes) override {
        }

        void writeBytes([[maybe_unused]] ptrdiff_t offsetInBytes, [[maybe_unused]] ptrdiff_t rangeInBytes, [[maybe_unused]] bufferPtr data) override {
        }

        void readBytes([[maybe_unused]] ptrdiff_t offsetInBytes, [[maybe_unused]] ptrdiff_t rangeInBytes, [[maybe_unused]] bufferPtr result) const override {
        }

        bool bindByteRange([[maybe_unused]] U8 bindIndex, [[maybe_unused]] ptrdiff_t offsetInBytes, [[maybe_unused]] ptrdiff_t rangeInBytes) override {
            return true; 
        }
    };
};  // namespace Divide
#endif //_VK_PLACEHOLDER_OBJECTS_H_
