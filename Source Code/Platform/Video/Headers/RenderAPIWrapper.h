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
#ifndef _RENDER_API_H_
#define _RENDER_API_H_

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Platform/Video/Headers/DescriptorSets.h"

namespace CEGUI {
    class Texture;
};

namespace Divide {

namespace GFX {
    class CommandBuffer;
    struct CommandBase;
};

enum class ErrorCode : I8;

template <typename T>
class vec4;

class DisplayWindow;

struct Configuration;
struct TextElementBatch;

FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

struct VideoModes {
    // Video resolution
    I32 Width, Height;
    // Red bits per pixel
    I32 RedBits;
    // Green bits per pixel
    I32 GreenBits;
    // Blue bits per pixel
    I32 BlueBits;
};

/// Queries are expensive, so this result MAY BE SEVERAL frames out of date!
struct PerformanceMetrics
{
    F32 _gpuTimeInMS{ 0.f };
    /// Returns the time in milliseconds that it took to render one frame
    U64 _verticesSubmitted{ 0u };
    /// Returns the total number of vertices submitted between frame start and end (before swap buffers)
    /// Includes all vertices, including GUI and debug stuff (but the delta should still be useful)
    U64 _primitivesGenerated{ 0u };
    ///  Number of patches processed by the tessellation control shader
    U64 _tessellationPatches{ 0u };
    /// Number of times the tessellation control shader has been invoked
    U64 _tessellationInvocations{ 0u };
    U32 _generatedRenderTargetCount{ 0u };
    /// How many frames are still queued up for execution on the GPU
    U64 _queuedGPUFrames{ 0u };
    /// Number of active sync objects
    U32 _syncObjectsInFlight[3]{};
    /// Scratch buffer queue usage
    U32 _scratchBufferQueueUsage[2]{};
    /// Total VRAM used for shader uniform storage across all used shader programs
    size_t _uniformBufferVRAMUsage{0u};
    /// Total VRAM usage for all shader buffers
    size_t _bufferVRAMUsage{ 0u };
};

struct DeviceInformation
{
    struct VersionInformation {
        U8 _major   { 0u };
        U8 _minor   { 0u };
        U8 _patch   { 0u };
        U8 _variant { 0u };
    };

    U32 _maxWorgroupCount[3] = {65535u, 65535u, 65535u};
    U32 _maxWorgroupSize[3] = {1024u, 1024u, 64u};
    size_t _UBOMaxSizeBytes = 64 * 1024;
    size_t _SSBOMaxSizeBytes = 1024 * 1024 * 1024u;
    size_t _maxComputeSharedMemoryBytes = 1024 * 1024 * 1024;
    size_t _UBOffsetAlignmentBytes = 256u;
    size_t _SSBOffsetAlignmentBytes = 16u;
    size_t _VBOffsetAlignmentBytes = 4u;
    U32 _maxWorgroupInvocations = 1024u;
    U32 _maxVertAttributeBindings = 16u;
    U32 _maxVertAttributes = 16u;
    U32 _maxVertOutputComponents = 16u;
    U32 _maxSSBOBufferBindings = 32u;
    U32 _shaderCompilerThreads = 1u;
    U32 _maxTextureUnits = 32u;
    U32 _maxRTColourAttachments = 4u;
    U32 _maxAnisotropy = 0u;
    U32 _maxClipDistances = 2u;
    U32 _maxCullDistances = 6u;
    U32 _maxClipAndCullDistances = 8u;
    VersionInformation _versionInfo = { 4u, 6u };
    GPUVendor _vendor = GPUVendor::COUNT;
    GPURenderer _renderer = GPURenderer::COUNT;
};

/// Renderer Programming Interface
class NOINITVTABLE RenderAPIWrapper : NonCopyable {
public:

    static constexpr U32 MaxFrameQueueSize = 2;
    static_assert(MaxFrameQueueSize > 0, "FrameQueueSize is invalid!");

public:
    virtual ~RenderAPIWrapper() = default;

protected:
    friend class GFXDevice;

    /// Clear buffers, set default states, etc
    virtual [[nodiscard]] bool beginFrame(DisplayWindow& window, bool global = false) = 0;
    /// Clear shaders, restore active texture units, etc
    virtual void endFrame(DisplayWindow& window, bool global = false) = 0;
    virtual void idle(bool fast) = 0;

    virtual ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) = 0;
    virtual void closeRenderingAPI() = 0;

    virtual void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) = 0;
    virtual void flushCommand(GFX::CommandBase* cmd) = 0;
    virtual void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) = 0;

    [[nodiscard]] virtual vec2<U16> getDrawableSize(const DisplayWindow& window) const = 0;

    // The definition of a hack. Feel free to quote this. -Ionut
    /// Convert a CEGUI texture handle to something that our current rendering API can use
    [[nodiscard]] virtual U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const = 0;

    virtual bool setViewport(const Rect<I32>& newViewport) = 0;

    virtual void onThreadCreated(const std::thread::id& threadID) = 0;

    virtual bool bindShaderResources(DescriptorSetUsage usage, const DescriptorSet& bindings) = 0;
};

FWD_DECLARE_MANAGED_CLASS(RenderAPIWrapper);

};  // namespace Divide

#endif
