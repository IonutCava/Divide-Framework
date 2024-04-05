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
#ifndef DVD_GL_WRAPPER_H_
#define DVD_GL_WRAPPER_H_

#include "GLStateTracker.h"
#include "glHardwareQuery.h"

#include "Platform/Video/Headers/CommandsImpl.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

struct FONScontext;
struct ImDrawData;

namespace Divide
{

namespace Time
{
    class ProfileTimer;
}

enum class ShaderResult : U8;

class DisplayWindow;
class glHardwareQueryRing;
class glHardwareQueryPool;

struct BufferLockEntry;

FWD_DECLARE_MANAGED_STRUCT(SyncObject);

/// OpenGL implementation of the RenderAPIWrapper
class GL_API final : public RenderAPIWrapper {
    friend class glShader;
    friend class glTexture;
    friend class glFramebuffer;
    friend class glVertexArray;
    friend class glShaderProgram;
    friend class glSamplerObject;
    friend class glGenericVertexData;

    friend struct GLStateTracker;

public:
    // Auto-delete locks older than this number of frames
    static constexpr U32 s_LockFrameLifetime = 3u; //(APP->Driver->GPU)

public:
    GL_API(GFXDevice& context);

private:
    /// Try and create a valid OpenGL context taking in account the specified command line arguments
    ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) override;
    /// Clear everything that was setup in initRenderingAPI()
    void closeRenderingAPI() override;

    [[nodiscard]] bool drawToWindow( DisplayWindow& window ) override;
                  void flushWindow( DisplayWindow& window, bool isRenderThread ) override;
    [[nodiscard]] bool frameStarted() override;
    [[nodiscard]] bool frameEnded() override;

    void endPerformanceQueries();

    void idle(bool fast) override;

    void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    void flushCommand(GFX::CommandBase* cmd) override;

    void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    void onThreadCreated(const std::thread::id& threadID, bool isMainRenderThread ) override;

    /// Reset as much of the GL default state as possible within the limitations given
    void clearStates(GLStateTracker& stateTracker) const;

    [[nodiscard]] bool bindShaderResources( const DescriptorSetEntries& descriptorSetEntries ) override;

    [[nodiscard]] bool makeTextureViewResident( gl::GLubyte bindingSlot, const ImageView& imageView, SamplerDescriptor sampler, size_t& samplerHashInOut ) const;

    bool setViewportInternal(const Rect<I32>& viewport) override;
    bool setScissorInternal( const Rect<I32>& scissor ) override;


    void flushTextureBindQueue();

    [[nodiscard]] gl::GLuint getGLTextureView(ImageView srcView, U8 lifetimeInFrames) const;

    void initDescriptorSets() override;

    void flushPushConstantsLocks();

    [[nodiscard]] RenderTarget_uptr     newRT( const RenderTargetDescriptor& descriptor ) const override;
    [[nodiscard]] GenericVertexData_ptr newGVD( U32 ringBufferLength, bool renderIndirect, const std::string_view name ) const override;
    [[nodiscard]] Texture_ptr           newTexture( size_t descriptorHash, std::string_view resourceName, std::string_view assetNames, const ResourcePath& assetLocations, const TextureDescriptor& texDescriptor, ResourceCache& parentCache ) const override;
    [[nodiscard]] ShaderProgram_ptr     newShaderProgram( size_t descriptorHash, std::string_view resourceName, std::string_view assetName, const ResourcePath& assetLocation, const ShaderProgramDescriptor& descriptor, ResourceCache& parentCache ) const override;
    [[nodiscard]] ShaderBuffer_uptr     newSB( const ShaderBufferDescriptor& descriptor ) const override;

public:
    [[nodiscard]] static GLStateTracker& GetStateTracker() noexcept;
    [[nodiscard]] static GLUtil::GLMemory::GLMemoryType GetMemoryTypeForUsage(gl::GLenum usage) noexcept;
    [[nodiscard]] static GLUtil::GLMemory::DeviceAllocator& GetMemoryAllocator(GLUtil::GLMemory::GLMemoryType memoryType) noexcept;

    static void QueueFlush() noexcept;

    static void AddDebugMessage( const char* message, U32 id = U32_MAX );
    static void PushDebugMessage( const char* message, U32 id = U32_MAX );
    static void PopDebugMessage();

    [[nodiscard]] static bool DeleteShaderPrograms( gl::GLuint count, gl::GLuint * programs);
    [[nodiscard]] static bool DeleteSamplers( gl::GLuint count, gl::GLuint* samplers);
    [[nodiscard]] static bool DeleteBuffers( gl::GLuint count, gl::GLuint* buffers);
    [[nodiscard]] static bool DeleteFramebuffers( gl::GLuint count, gl::GLuint* framebuffers);

    [[nodiscard]] static gl::GLuint GetSamplerHandle(SamplerDescriptor sampler, size_t& samplerHashInOut);

    [[nodiscard]] static glHardwareQueryPool* GetHardwareQueryPool() noexcept;

    [[nodiscard]] static gl::GLsync CreateFenceSync();
    static void DestroyFenceSync( gl::GLsync& sync);

protected:
    static ShaderResult BindPipeline(GFXDevice& context, const Pipeline& pipeline);
    static bool Draw( const GenericDrawCommand& cmd );

private:

    enum class GlobalQueryTypes : U8 {
        VERTICES_SUBMITTED = 0,
        PRIMITIVES_GENERATED,
        TESSELLATION_PATCHES,
        TESSELLATION_EVAL_INVOCATIONS,
        GPU_TIME,
        COUNT
    };

    struct glHardwareQueryEntry {
        glHardwareQueryRing* _query{ nullptr };
        QueryType _type{ QueryType::COUNT };
        U8 _index{ 0u };
    };

    struct TexBindEntry {
        gl::GLubyte _slot{ INVALID_TEXTURE_BINDING };
        gl::GLuint _handle{ GL_NULL_HANDLE };
        gl::GLuint _sampler{ GL_NULL_HANDLE };
    };

    static eastl::fixed_vector<TexBindEntry, 32, false> s_TexBindQueue;

    using HardwareQueryContext = std::array<glHardwareQueryEntry, to_base(QueryType::COUNT)>;
    using SamplerObjectMap = hashMap<size_t, gl::GLuint>;

private:
    GFXDevice& _context;
    Time::ProfileTimer& _swapBufferTimer;

    HardwareQueryContext _primitiveQueries;
    /// Hardware query objects used for performance measurements
    std::array<glHardwareQueryRing_uptr, to_base(GlobalQueryTypes::COUNT)> _performanceQueries;
    // OpenGL rendering is not thread-safe anyway, so this works
    eastl::stack<HardwareQueryContext> _queryContext;
    bool _runQueries{false};

    bool _pushConstantsNeedLock{false};
    GFX::MemoryBarrierCommand _pushConstantsMemCommand{};

    gl::GLuint _dummyVAO{ GL_NULL_HANDLE };

private:

    static std::atomic_bool s_glFlushQueued;

    static SharedMutex s_samplerMapLock;
    static SamplerObjectMap s_samplerMap;
    static GLStateTracker s_stateTracker;

    static std::array<GLUtil::GLMemory::DeviceAllocator, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> s_memoryAllocators;
    static std::array<size_t, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> s_memoryAllocatorSizes;

    static GLUtil::glTextureViewCache s_textureViewCache;

    static glHardwareQueryPool* s_hardwareQueryPool;

    static U32 s_fenceSyncCounter[s_LockFrameLifetime];
};

};  // namespace Divide

#endif //DVD_GL_WRAPPER_H_
