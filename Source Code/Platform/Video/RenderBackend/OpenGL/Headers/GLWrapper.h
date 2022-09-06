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
#ifndef _GL_WRAPPER_H_
#define _GL_WRAPPER_H_

#include "GLStateTracker.h"
#include "glHardwareQuery.h"

#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

struct FONScontext;
struct ImDrawData;

namespace CEGUI {
    class OpenGL3Renderer;
};

namespace Divide {

namespace Time {
    class ProfileTimer;
};

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

    /// Prepare the GPU for rendering a frame
    [[nodiscard]] bool beginFrame(DisplayWindow& window, bool global = false) override;
    /// Finish rendering the current frame
    void endFrame(DisplayWindow& window, bool global = false) override;
    void idle(bool fast) override;

    /// Text rendering is handled exclusively by Mikko Mononen's FontStash library
    /// (https://github.com/memononen/fontstash)
    /// with his OpenGL frontend adapted for core context profiles
    void drawText(const TextElementBatch& batch);

    bool draw(const GenericDrawCommand& cmd) const;

    void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    void flushCommand(GFX::CommandBase* cmd) override;

    void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    /// Return the size in pixels that we can render to. This differs from the window size on Retina displays
    vec2<U16> getDrawableSize(const DisplayWindow& window) const noexcept override;

    U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const override;

    void onThreadCreated(const std::thread::id& threadID) override;

    /// Try to find the requested font in the font cache. Load on cache miss.
    I32 getFont(const Str64& fontName);

    /// Reset as much of the GL default state as possible within the limitations given
    void clearStates(const DisplayWindow& window, GLStateTracker& stateTracker, bool global) const;

    [[nodiscard]] bool bindShaderResources(DescriptorSetUsage usage, const DescriptorSet& bindings) override;

    [[nodiscard]] bool makeTextureViewResident(DescriptorSetUsage set, U8 bindingSlot, const ImageView& imageView, size_t samplerHash) const;

    bool setViewport(const Rect<I32>& viewport) override;
    ShaderResult bindPipeline(const Pipeline& pipeline);

    void flushTextureBindQueue();

    [[nodiscard]] GLuint getGLTextureView(ImageView srcView, U8 lifetimeInFrames);

private:
    void endFrameLocal(const DisplayWindow& window);
    void endFrameGlobal(const DisplayWindow& window);

public:
    static [[nodiscard]] const GLStateTracker_uptr& GetStateTracker() noexcept;
    static [[nodiscard]] GLUtil::GLMemory::GLMemoryType GetMemoryTypeForUsage(GLenum usage) noexcept;
    static [[nodiscard]] GLUtil::GLMemory::DeviceAllocator& GetMemoryAllocator(GLUtil::GLMemory::GLMemoryType memoryType) noexcept;

    static void QueueFlush() noexcept;

    static void PushDebugMessage(const char* message, U32 id = std::numeric_limits<U32>::max());
    static void PopDebugMessage();

    static [[nodiscard]] bool DeleteShaderPrograms(GLuint count, GLuint * programs);
    static [[nodiscard]] bool DeleteTextures(GLuint count, GLuint* textures, TextureType texType);
    static [[nodiscard]] bool DeleteSamplers(GLuint count, GLuint* samplers);
    static [[nodiscard]] bool DeleteBuffers(GLuint count, GLuint* buffers);
    static [[nodiscard]] bool DeleteVAOs(GLuint count, GLuint* vaos);
    static [[nodiscard]] bool DeleteFramebuffers(GLuint count, GLuint* framebuffers);

    static [[nodiscard]] GLuint GetSamplerHandle(size_t samplerHash);

    static [[nodiscard]] glHardwareQueryPool* GetHardwareQueryPool() noexcept;

    static [[nodiscard]] GLsync CreateFenceSync();
    static void DestroyFenceSync(GLsync& sync);
    
    static [[nodiscard]] FrameDependendSync CreateFrameFenceSync();
    static void DestroyFrameFenceSync(FrameDependendSync& sync);

private:

    enum class GlobalQueryTypes : U8 {
        VERTICES_SUBMITTED = 0,
        PRIMITIVES_GENERATED,
        TESSELLATION_PATCHES,
        TESSELLATION_EVAL_INVOCATIONS,
        GPU_TIME,
        COUNT
    };

    struct WindowGLContext {
        I64 _windowGUID{-1};
        SDL_GLContext _context{ nullptr };
    };

    struct glHardwareQueryEntry {
        glHardwareQueryRing* _query{ nullptr };
        QueryType _type{ QueryType::COUNT };
        U8 _index{ 0u };
    };

    struct TexBindEntry {
        GLubyte _slot{ INVALID_TEXTURE_BINDING };
        GLuint _handle{ GLUtil::k_invalidObjectID };
        GLuint _sampler{ GLUtil::k_invalidObjectID };
    };

    static eastl::fixed_vector<TexBindEntry, 32, false> s_TexBindQueue;

    using HardwareQueryContext = std::array<glHardwareQueryEntry, to_base(QueryType::COUNT)>;
    HardwareQueryContext _primitiveQueries;
    /// /*sampler hash value*/ /*sampler object*/
    using SamplerObjectMap = hashMap<size_t, GLuint, NoHash<size_t>>;
    using BufferLockQueue = eastl::fixed_vector<BufferLockEntry, 64, true, eastl::dvd_allocator>;

private:
    GFXDevice& _context;
    Time::ProfileTimer& _swapBufferTimer;

    /// A cache of all fonts used
    hashMap<U64, I32> _fonts;
    hashAlg::pair<Str64, I32> _fontCache = {"", -1};

    /// Hardware query objects used for performance measurements
    std::array<glHardwareQueryRing_uptr, to_base(GlobalQueryTypes::COUNT)> _performanceQueries;
    // OpenGL rendering is not thread-safe anyway, so this works
    eastl::stack<HardwareQueryContext> _queryContext;

    WindowGLContext _currentContext{};

    /// FontStash's context
    FONScontext* _fonsContext = nullptr;

    CEGUI::OpenGL3Renderer* _GUIGLrenderer = nullptr;

    //Because GFX::queryPerfStats can change mid-frame, we need to cache that value and apply in beginFrame() properly
    U8 _queryIdxForCurrentFrame = 0u;
    bool _runQueries = false;

private:

    static std::atomic_bool s_glFlushQueued;

    static SharedMutex s_samplerMapLock;
    static SamplerObjectMap s_samplerMap;
    static GLStateTracker_uptr s_stateTracker;

    static std::array<GLUtil::GLMemory::DeviceAllocator, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> s_memoryAllocators;
    static std::array<size_t, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> s_memoryAllocatorSizes;

    static GLUtil::glTextureViewCache s_textureViewCache;

    using VAOMap = hashMap<size_t, GLuint>;
    static VAOMap s_vaoCache;

    static glHardwareQueryPool* s_hardwareQueryPool;

    static U32 s_fenceSyncCounter[s_LockFrameLifetime];
};

};  // namespace Divide
#endif
