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

#include <NRI.h>

namespace Divide {

// -----------------------------------------------------------------
// nriRenderTarget
// -----------------------------------------------------------------
class nriRenderTarget final : public RenderTarget
{
public:
    nriRenderTarget( GFXDevice& context, const RenderTargetDescriptor& descriptor )
        : RenderTarget( context, descriptor )
    {}

    // NRI colour attachment textures and their descriptor (view) handles.
    // Populated by the backend when creating attachment resources.
    // This upper bound matches the minimum guaranteed by NRI (D3D11 max, 8 colour attachments).
    // At runtime the real device limit is stored in DeviceInformation::_maxRTColourAttachments.
    static constexpr U8 MAX_COLOUR_ATTACHMENTS = 8u;
    std::array<nri::Texture*, MAX_COLOUR_ATTACHMENTS>    _colourTextures{};
    std::array<nri::Descriptor*, MAX_COLOUR_ATTACHMENTS> _colourViews{};

    nri::Texture*    _depthTexture { nullptr };
    nri::Descriptor* _depthView    { nullptr };

    // NRI memory backing the attachment textures
    std::array<nri::Memory*, MAX_COLOUR_ATTACHMENTS> _colourMemory{};
    nri::Memory*                                     _depthMemory { nullptr };
};

// -----------------------------------------------------------------
// nriGPUBuffer
// -----------------------------------------------------------------
class nriGPUBuffer final : public GPUBuffer
{
public:
    nriGPUBuffer( GFXDevice& context, const U16 ringBufferLength, const std::string_view name )
        : GPUBuffer( context, ringBufferLength, name )
    {}

    [[nodiscard]] BufferLock updateBuffer( [[maybe_unused]] U32 elementCountOffset,
                                           [[maybe_unused]] U32 elementCountRange,
                                           [[maybe_unused]] bufferPtr data ) noexcept override
    {
        return {};
    }

    // Underlying NRI buffer object and its device-memory allocation
    nri::Buffer* _buffer { nullptr };
    nri::Memory* _memory { nullptr };
};

// -----------------------------------------------------------------
// nriTexture
// -----------------------------------------------------------------
class nriTexture final : public Texture
{
public:
    nriTexture( PlatformContext& context, const ResourceDescriptor<Texture>& descriptor )
        : Texture( context, descriptor )
    {}

    [[nodiscard]] ImageReadbackData readData( [[maybe_unused]] const U8 mipLevel,
                                              [[maybe_unused]] const PixelAlignment& pixelPackAlignment ) const noexcept override
    { return {}; }

    void loadDataInternal( [[maybe_unused]] const ImageTools::ImageData& imageData,
                           [[maybe_unused]] const PixelAlignment& pixelUnpackAlignment ) override {}
    void loadDataInternal( [[maybe_unused]] const std::span<const Byte> data,
                           [[maybe_unused]] const vec3<U16>& offset,
                           [[maybe_unused]] const vec3<U16>& dimensions,
                           [[maybe_unused]] const PixelAlignment& pixelUnpackAlignment ) override {}

    // NRI backing objects
    nri::Texture*    _texture { nullptr };
    nri::Memory*     _memory  { nullptr };

    // Cached views (SRV, UAV, RTV / DSV depending on usage)
    nri::Descriptor* _shaderResourceView { nullptr };
    nri::Descriptor* _unorderedAccessView{ nullptr };
};

// -----------------------------------------------------------------
// nriShaderProgram
// -----------------------------------------------------------------
class nriShaderProgram final : public ShaderProgram
{
public:
    nriShaderProgram( PlatformContext& context, const ResourceDescriptor<ShaderProgram>& descriptor )
        : ShaderProgram( context, descriptor )
    {}

    // SPIRV bytecode blobs per stage (populated during compile).
    // The NRI pipeline creation step will consume these.
    struct StageData
    {
        vector<uint32_t> spirv;
    };
    std::array<StageData, to_base( ShaderType::COUNT )> _stageData{};
};

// -----------------------------------------------------------------
// nriUniformBuffer  (ShaderBuffer backed by an NRI buffer)
// -----------------------------------------------------------------
class nriUniformBuffer final : public ShaderBuffer
{
public:
    nriUniformBuffer( GFXDevice& context, const ShaderBufferDescriptor& descriptor )
        : ShaderBuffer( context, descriptor )
    {}

    BufferLock writeBytesInternal( [[maybe_unused]] BufferRange<> range,
                                   [[maybe_unused]] const bufferPtr data ) noexcept override { return {}; }
    void readBytesInternal( [[maybe_unused]] BufferRange<> range,
                            [[maybe_unused]] std::pair<bufferPtr, size_t> outData ) noexcept override {}

    [[nodiscard]] LockableBuffer* getBufferImpl() override { return nullptr; }

    // Underlying NRI constant buffer
    nri::Buffer* _buffer { nullptr };
    nri::Memory* _memory { nullptr };
};

} // namespace Divide

#endif // DVD_NRI_PLACEHOLDER_OBJECTS_H_

