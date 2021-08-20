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
    class noRenderTarget final : public RenderTarget {
      public:
        noRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
          : RenderTarget(context, descriptor)
        {}
  
        void clear(const RTClearDescriptor& descriptor) override {
            ACKNOWLEDGE_UNUSED(descriptor);
        }

        void setDefaultState(const RTDrawDescriptor& drawPolicy) override {
            ACKNOWLEDGE_UNUSED(drawPolicy);
        }

        void readData(const vec4<U16>& rect, GFXImageFormat imageFormat, GFXDataFormat dataType, bufferPtr outData) const override {
            ACKNOWLEDGE_UNUSED(rect);
            ACKNOWLEDGE_UNUSED(imageFormat);
            ACKNOWLEDGE_UNUSED(dataType);
            ACKNOWLEDGE_UNUSED(outData);
        }

        void blitFrom(const RTBlitParams& params) override {
            ACKNOWLEDGE_UNUSED(params);
        }
    };

    class noIMPrimitive final : public IMPrimitive {
    public:
        noIMPrimitive(GFXDevice& context)
            : IMPrimitive(context)
        {}

        void draw(const GenericDrawCommand& cmd) override {
            ACKNOWLEDGE_UNUSED(cmd);
        }

        void beginBatch(bool reserveBuffers, U32 vertexCount, U32 attributeCount) override {
            ACKNOWLEDGE_UNUSED(reserveBuffers);
            ACKNOWLEDGE_UNUSED(vertexCount);
            ACKNOWLEDGE_UNUSED(attributeCount);
        }

        void begin(PrimitiveType type) override {
            ACKNOWLEDGE_UNUSED(type);
        }

        void vertex(F32 x, F32 y, F32 z) override {
            ACKNOWLEDGE_UNUSED(x);
            ACKNOWLEDGE_UNUSED(y);
            ACKNOWLEDGE_UNUSED(z);
        }

        void attribute1i(U32 attribLocation, I32 value) override {
            ACKNOWLEDGE_UNUSED(attribLocation);
            ACKNOWLEDGE_UNUSED(value);
        }

        void attribute1f(U32 attribLocation, F32 value) override {
            ACKNOWLEDGE_UNUSED(attribLocation);
            ACKNOWLEDGE_UNUSED(value);
        }

        void attribute4ub(U32 attribLocation, U8 x, U8 y, U8 z, U8 w) override {
            ACKNOWLEDGE_UNUSED(attribLocation);
            ACKNOWLEDGE_UNUSED(x);
            ACKNOWLEDGE_UNUSED(y);
            ACKNOWLEDGE_UNUSED(z);
            ACKNOWLEDGE_UNUSED(w);
        }

        void attribute4f(U32 attribLocation, F32 x, F32 y, F32 z, F32 w) override {
            ACKNOWLEDGE_UNUSED(attribLocation);
            ACKNOWLEDGE_UNUSED(x);
            ACKNOWLEDGE_UNUSED(y);
            ACKNOWLEDGE_UNUSED(z);
            ACKNOWLEDGE_UNUSED(w);
        }

        void end() override {}
        void endBatch() override {}
        void clearBatch() override {}
        bool hasBatch() const override { return false; }
    };

    class noVertexBuffer final : public VertexBuffer {
      public:
        noVertexBuffer(GFXDevice& context)
          : VertexBuffer(context)
        {}

        void draw(const GenericDrawCommand& command) override {
            ACKNOWLEDGE_UNUSED(command);
        }

        bool queueRefresh() override { return refresh(); }

      protected:
        bool refresh() override { return true; }
    };


    class noPixelBuffer final : public PixelBuffer {
    public:
        noPixelBuffer(GFXDevice& context, const PBType type, const char* name)
            : PixelBuffer(context, type, name)
        {}

        bool create(U16 width, U16 height, U16 depth = 0,
            GFXImageFormat formatEnum = GFXImageFormat::RGBA,
            GFXDataFormat dataTypeEnum = GFXDataFormat::FLOAT_32,
            bool normalized = true) override         {
            ACKNOWLEDGE_UNUSED(width);
            ACKNOWLEDGE_UNUSED(height);
            ACKNOWLEDGE_UNUSED(depth);
            ACKNOWLEDGE_UNUSED(formatEnum);
            ACKNOWLEDGE_UNUSED(dataTypeEnum);
            ACKNOWLEDGE_UNUSED(normalized);
            return true;
        }

        void updatePixels(const F32* pixels, U32 pixelCount) override {
            ACKNOWLEDGE_UNUSED(pixels);
            ACKNOWLEDGE_UNUSED(pixelCount);
        }
    };


    class noGenericVertexData final : public GenericVertexData {
    public:
        noGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
            : GenericVertexData(context, ringBufferLength, name)
        {}

        void create(U8 numBuffers = 1) override {
            ACKNOWLEDGE_UNUSED(numBuffers);
        }

        void draw(const GenericDrawCommand& command) override {
            ACKNOWLEDGE_UNUSED(command);
        }

        void setBuffer(const SetBufferParams& params) override {
            ACKNOWLEDGE_UNUSED(params);
        }

        void updateBuffer(U32 buffer,
                          U32 elementCountOffset,
                          U32 elementCountRange,
                          bufferPtr data) override {
            ACKNOWLEDGE_UNUSED(buffer);
            ACKNOWLEDGE_UNUSED(elementCountOffset);
            ACKNOWLEDGE_UNUSED(elementCountRange);
            ACKNOWLEDGE_UNUSED(data);
        }

        void lockBuffers() override {}

        bool waitBufferRange(U32 buffer, U32 elementCountOffset, U32 elementCountRange, bool blockClient) override {
            ACKNOWLEDGE_UNUSED(buffer);
            ACKNOWLEDGE_UNUSED(elementCountOffset);
            ACKNOWLEDGE_UNUSED(elementCountRange);
            ACKNOWLEDGE_UNUSED(blockClient);

            return false;
        }
    };

    class noTexture final : public Texture {
    public:
        noTexture(GFXDevice& context,
                  const size_t descriptorHash,
                  const Str256& name,
                  const ResourcePath& assetNames,
                  const ResourcePath& assetLocations,
                  const bool isFlipped,
                  const bool asyncLoad,
                  const TextureDescriptor& texDescriptor)
            : Texture(context, descriptorHash, name, assetNames, assetLocations, isFlipped, asyncLoad, texDescriptor)
        {}

        [[nodiscard]] SamplerAddress getGPUAddress(size_t samplerHash) override {
            ACKNOWLEDGE_UNUSED(samplerHash);
            return 0u;
        }

        void bindLayer(U8 slot, U8 level, U8 layer, bool layered, Image::Flag rw_flag) override {
            ACKNOWLEDGE_UNUSED(slot);
            ACKNOWLEDGE_UNUSED(level);
            ACKNOWLEDGE_UNUSED(layer);
            ACKNOWLEDGE_UNUSED(layered);
            ACKNOWLEDGE_UNUSED(rw_flag);
        }

        void loadData(const ImageTools::ImageData& imageLayers) override {
            ACKNOWLEDGE_UNUSED(imageLayers);
        }

        void loadData(const std::pair<Byte*, size_t>& ptr, const vec2<U16>& dimensions) override {
            ACKNOWLEDGE_UNUSED(ptr);
            ACKNOWLEDGE_UNUSED(dimensions);
        }

        void clearData(const UColour4& clearColour, U8 level) const override {
            ACKNOWLEDGE_UNUSED(clearColour);
            ACKNOWLEDGE_UNUSED(level);
        }

        void clearSubData(const UColour4& clearColour, U8 level, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const override {
            ACKNOWLEDGE_UNUSED(clearColour);
            ACKNOWLEDGE_UNUSED(level);
            ACKNOWLEDGE_UNUSED(rectToClear);
            ACKNOWLEDGE_UNUSED(depthRange);
        }

        std::pair<std::shared_ptr<Byte[]>, size_t> readData(U16 mipLevel, GFXDataFormat desiredFormat) const override {
            ACKNOWLEDGE_UNUSED(mipLevel);
            ACKNOWLEDGE_UNUSED(desiredFormat);
            return { nullptr, 0 };
        }
    };

    class noShaderProgram final : public ShaderProgram {
    public:
        noShaderProgram(GFXDevice& context, const size_t descriptorHash,
                        const Str256& name,
                        const Str256& assetName,
                        const ResourcePath& assetLocation,
                        const ShaderProgramDescriptor& descriptor,
                        const bool asyncLoad)
            : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor, asyncLoad)
        {}

        bool isValid() const override { return true; }
    };

    class noUniformBuffer final : public ShaderBuffer {
    public:
        noUniformBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
            : ShaderBuffer(context, descriptor)
        {}

        void clearBytes(ptrdiff_t offsetInBytes, ptrdiff_t rangeInBytes) override {
            ACKNOWLEDGE_UNUSED(offsetInBytes);
            ACKNOWLEDGE_UNUSED(rangeInBytes);
        }

        void writeBytes(ptrdiff_t offsetInBytes, ptrdiff_t rangeInBytes, bufferPtr data) override {
            ACKNOWLEDGE_UNUSED(offsetInBytes);
            ACKNOWLEDGE_UNUSED(rangeInBytes);
            ACKNOWLEDGE_UNUSED(data);
        }

        void readBytes(ptrdiff_t offsetInBytes, ptrdiff_t rangeInBytes, bufferPtr result) const override {
            ACKNOWLEDGE_UNUSED(offsetInBytes);
            ACKNOWLEDGE_UNUSED(rangeInBytes);
            ACKNOWLEDGE_UNUSED(result);
        }

        bool bindByteRange(U8 bindIndex, ptrdiff_t offsetInBytes, ptrdiff_t rangeInBytes) override {
            ACKNOWLEDGE_UNUSED(bindIndex);
            ACKNOWLEDGE_UNUSED(offsetInBytes);
            ACKNOWLEDGE_UNUSED(rangeInBytes);
            return true;
        }
    };
};  // namespace Divide
#endif //_NONE_PLACEHOLDER_OBJECTS_H_
