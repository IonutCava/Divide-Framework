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

        void clear([[maybe_unused]] const RTClearDescriptor& descriptor) noexcept override {
        }

        void setDefaultState([[maybe_unused]] const RTDrawDescriptor& drawPolicy) noexcept override {
        }

        void readData([[maybe_unused]] const vec4<U16>& rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept override {
        }

        void blitFrom([[maybe_unused]] const RTBlitParams& params) noexcept override {
        }
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
                    [[maybe_unused]]  bool normalized = true) noexcept override
        {
            return true;
        }

        void updatePixels([[maybe_unused]] const F32* pixels, [[maybe_unused]] U32 pixelCount) noexcept override {
        }
    };


    class vkGenericVertexData final : public GenericVertexData {
    public:
        vkGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
            : GenericVertexData(context, ringBufferLength, name)
        {}

        void create([[maybe_unused]] U8 numBuffers = 1) noexcept override {
        }

        void reset() override {

        }

        void draw([[maybe_unused]] const GenericDrawCommand& command) noexcept override {
        }

        void setBuffer([[maybe_unused]] const SetBufferParams& params) noexcept override {
        }

        void updateBuffer([[maybe_unused]] U32 buffer,
                          [[maybe_unused]] U32 elementCountOffset,
                          [[maybe_unused]] U32 elementCountRange,
                          [[maybe_unused]] bufferPtr data) noexcept override
        {
        }
    };

    class vkTexture final : public Texture {
    public:
        vkTexture(GFXDevice& context,
                  const size_t descriptorHash,
                  const Str256& name,
                  const ResourcePath& assetNames,
                  const ResourcePath& assetLocations,
                  const TextureDescriptor& texDescriptor)
            : Texture(context, descriptorHash, name, assetNames, assetLocations, texDescriptor)
        {}

        [[nodiscard]] SamplerAddress getGPUAddress([[maybe_unused]] size_t samplerHash) noexcept override {
            return 0u;
        }

        void bindLayer([[maybe_unused]] U8 slot, [[maybe_unused]] U8 level, [[maybe_unused]] U8 layer, [[maybe_unused]] bool layered, [[maybe_unused]] Image::Flag rw_flag) noexcept override {
        }

        void loadData([[maybe_unused]] const ImageTools::ImageData& imageLayers) noexcept override {
        }

        void loadData([[maybe_unused]] const Byte* data, [[maybe_unused]] const size_t dataSize, [[maybe_unused]] const vec2<U16>& dimensions) noexcept override {
        }

        void clearData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level) const noexcept override {
        }

        void clearSubData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level, [[maybe_unused]] const vec4<I32>& rectToClear, [[maybe_unused]] const vec2<I32>& depthRange) const noexcept override {
        }

        TextureReadbackData readData([[maybe_unused]] U16 mipLevel, [[maybe_unused]] GFXDataFormat desiredFormat) const noexcept override {
            TextureReadbackData data{};
            return MOV(data);
        }
    };

    class vkShaderProgram final : public ShaderProgram {
    public:
        vkShaderProgram(GFXDevice& context, 
                        const size_t descriptorHash,
                        const Str256& name,
                        const Str256& assetName,
                        const ResourcePath& assetLocation,
                        const ShaderProgramDescriptor& descriptor)
            : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor)
        {}
    };


    class vkUniformBuffer final : public ShaderBuffer {
    public:
        vkUniformBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
            : ShaderBuffer(context, descriptor)
        {}

        void clearBytes([[maybe_unused]] ptrdiff_t offsetInBytes, [[maybe_unused]] ptrdiff_t rangeInBytes) noexcept override {
        }

        void writeBytes([[maybe_unused]] ptrdiff_t offsetInBytes, [[maybe_unused]] ptrdiff_t rangeInBytes, [[maybe_unused]] bufferPtr data) noexcept override {
        }

        void readBytes([[maybe_unused]] ptrdiff_t offsetInBytes, [[maybe_unused]] ptrdiff_t rangeInBytes, [[maybe_unused]] bufferPtr result) const noexcept override {
        }

        bool bindByteRange([[maybe_unused]] U8 bindIndex, [[maybe_unused]] ptrdiff_t offsetInBytes, [[maybe_unused]] ptrdiff_t rangeInBytes) noexcept override {
            return true; 
        }

        bool lockByteRange([[maybe_unused]] const ptrdiff_t offsetInBytes, [[maybe_unused]] const ptrdiff_t rangeInBytes, [[maybe_unused]] const ShaderBufferLockType lockType) override {
            return true;
        }
    };

    class vkIMPrimitive final : public IMPrimitive {
    public:
        vkIMPrimitive(GFXDevice& context) : IMPrimitive(context) {}

    public:
        /// Begins defining one piece of geometry that can later be rendered with
        /// one set of states.
        void beginBatch(bool reserveBuffers, U32 vertexCount, U32 attributeCount) override {}
        /// Ends defining the batch. After this call "RenderBatch" can be called to
        /// actually render it.
        void endBatch() noexcept override {}
        /// Resets the batch so that the primitive has nothing left to draw
        void clearBatch() override {}
        /// Return true if this primitive contains drawable geometry data
        bool hasBatch() const noexcept override { return true; }
        /// Begins gathering information about the given type of primitives.
        void begin(PrimitiveTopology type) override {}
        /// Ends gathering information about the primitives.
        void end() override {}
        /// Specify the position of a vertex belonging to this primitive
        void vertex(F32 x, F32 y, F32 z) override {}
        /// Specify each attribute at least once(even with dummy values) before
        /// calling begin!
        /// Specify an attribute that will be applied to all vertex calls after this
        void attribute1i(U32 attribLocation, I32 value) override {}
        void attribute1f(U32 attribLocation, F32 value) override {}
        void attribute2f(U32 attribLocation, vec2<F32> value) override {}
        void attribute3f(U32 attribLocation, vec3<F32> value) override {}
        /// Specify an attribute that will be applied to all vertex calls after this
        void attribute4ub(U32 attribLocation, U8 x, U8 y, U8 z, U8 w) override {}
        /// Specify an attribute that will be applied to all vertex calls after this
        void attribute4f(U32 attribLocation, F32 x, F32 y, F32 z, F32 w) override {}
        /// Submit the created batch to the GPU for rendering
        void draw(const GenericDrawCommand& cmd) override {}
    };
};  // namespace Divide
#endif //_VK_PLACEHOLDER_OBJECTS_H_
