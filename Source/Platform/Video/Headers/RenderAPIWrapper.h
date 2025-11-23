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
#ifndef DVD_RENDER_API_H_
#define DVD_RENDER_API_H_

#include "Platform/Video/Headers/DescriptorSetsFwd.h"

namespace CEGUI {
    class Texture;
};

namespace Divide {

namespace GFX {
    class CommandBuffer;
    struct CommandBase;
    enum class CommandType : U8;
};

enum class ErrorCode : I8;

template <typename T> class vec4;

class Texture;
class ShaderProgram;
class ResourceCache;
class DisplayWindow;

struct Configuration;
struct TextElementBatch;
struct RenderTargetDescriptor;
struct ShaderBufferDescriptor;

FWD_DECLARE_MANAGED_CLASS( Renderer );
FWD_DECLARE_MANAGED_CLASS( GPUBuffer );
FWD_DECLARE_MANAGED_CLASS( RenderTarget );
FWD_DECLARE_MANAGED_CLASS( ShaderBuffer );

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
    /// Total buffers created
    size_t _gpuBufferCount{ 0u };
};

struct DeviceInformation
{
    struct VersionInformation {
        U8 _major   { 0u };
        U8 _minor   { 0u };
        U8 _patch   { 0u };
        U8 _variant { 0u };
    };

    U32 _maxWorkgroupCount[3] = {65535u, 65535u, 65535u};
    U32 _maxWorkgroupSize[3] = {1024u, 1024u, 64u};
    U32 _maxWorkgroupInvocations = 1024u;

    U32 _maxMeshWorkgroupCount[3] = { 65535u, 65535u, 65535u };
    U32 _maxMeshWorkgroupSize[3] = { 1024u, 1024u, 64u };
    U32 _maxMeshWorkgroupInvocations = 1024u;

    U32 _maxTaskWorkgroupCount[3] = { 65535u, 65535u, 65535u };
    U32 _maxTaskWorkgroupSize[3] = { 1024u, 1024u, 64u };
    U32 _maxTaskWorkgroupInvocations = 1024u;

    size_t _maxBufferSizeBytes = 0u;
    size_t _maxSizeBytesUBO = 64 * 1024;
    size_t _maxSizeBytesSSBO = 1024 * 1024 * 1024u;
    size_t _maxComputeSharedMemoryBytes = 1024 * 1024 * 1024;
    size_t _offsetAlignmentBytesVBO = sizeof(U32);
    size_t _offsetAlignmentBytesIBO = sizeof(U32);
    size_t _offsetAlignmentBytesUBO = 256u;
    size_t _offsetAlignmentBytesSSBO = 16u;
    U32 _maxMeshShaderOutputVertices = 256u;
    U32 _maxMeshShaderOutputPrimitives = 512u;
    U32 _maxVertAttributeBindings = 16u;
    U32 _maxVertAttributes = 16u;
    U32 _maxVertOutputComponents = 16u;
    U32 _maxSSBOBufferBindings = 32u;
    U32 _shaderCompilerThreads = 1u;
    U32 _maxTextureUnits = 32u;
    U32 _maxTextureSize = 4096u;
    U32 _max3DTextureSize = 4096u;
    U32 _maxRTColourAttachments = 4u;
    U32 _maxAnisotropy = 0u;
    U32 _maxClipDistances = Config::MAX_CLIP_DISTANCES;
    U32 _maxCullDistances = Config::MAX_CULL_DISTANCES;
    U32 _maxClipAndCullDistances = Config::MAX_CLIP_DISTANCES + Config::MAX_CULL_DISTANCES;
    U32 _maxDrawIndirectCount = std::numeric_limits<U16>::max();
    VersionInformation _versionInfo = { 4u, 6u };
    GPUVendor _vendor = GPUVendor::COUNT;
    GPURenderer _renderer = GPURenderer::COUNT;
    bool _meshShadingSupported = false;
};

struct DebugScope
{
    const char* _name = nullptr;
    U32 _id{ U32_MAX };
};

/// Renderer Programming Interface
NOINITVTABLE_CLASS(RenderAPIWrapper) : NonCopyable
{
public:
    virtual ~RenderAPIWrapper() = default;

protected:
    friend class GFXDevice;

    [[nodiscard]] virtual bool drawToWindow(DisplayWindow& window) = 0;
                  virtual void onRenderThreadLoopStart() = 0;
                  virtual void onRenderThreadLoopEnd() = 0;
                  virtual void prepareFlushWindow(DisplayWindow& window) = 0;
                  virtual void flushWindow(DisplayWindow& window) = 0;
    [[nodiscard]] virtual bool frameStarted() = 0;
    [[nodiscard]] virtual bool frameEnded() = 0;

    virtual void idle(bool fast) = 0;

    virtual ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) = 0;
    virtual void closeRenderingAPI() = 0;

    virtual void preFlushCommandBuffer(Handle<GFX::CommandBuffer> commandBuffer) = 0;
    virtual void flushCommand(GFX::CommandBase* cmd) = 0;
    virtual void postFlushCommandBuffer(Handle<GFX::CommandBuffer> commandBuffer) = 0;

    virtual bool setViewportInternal(const Rect<I32>& newViewport) = 0;
    virtual bool setScissorInternal(const Rect<I32>& newScissor) = 0;

    virtual void onThreadCreated( const size_t threadIndex, const std::thread::id& threadID, bool isMainRenderThread ) = 0;

    virtual bool bindShaderResources( const DescriptorSetEntries& descriptorSetEntries ) = 0;

    virtual void initDescriptorSets() = 0;
    
    virtual RenderTarget_uptr newRenderTarget( const RenderTargetDescriptor& descriptor ) const = 0;
    virtual GPUBuffer_uptr    newGPUBuffer( U32 ringBufferLength, std::string_view name ) const = 0;
    virtual ShaderBuffer_uptr newShaderBuffer( const ShaderBufferDescriptor& descriptor ) const = 0;
};

FWD_DECLARE_MANAGED_CLASS(RenderAPIWrapper);

};  // namespace Divide

#endif //DVD_RENDER_API_H_
