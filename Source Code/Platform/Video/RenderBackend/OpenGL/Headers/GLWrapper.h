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
#include "glHardwareQueryPool.h"
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
enum class ShaderBufferLockType : U8;

class IMPrimitive;
class DisplayWindow;
class glHardwareQueryRing;
class glHardwareQueryPool;

struct BufferLockEntry;

FWD_DECLARE_MANAGED_STRUCT(SyncObject);

/// OpenGL implementation of the RenderAPIWrapper
class GL_API final : public RenderAPIWrapper {
    friend class glShader;
    friend class glTexture;
    friend class glIMPrimitive;
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

protected:
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

    void drawIMGUI(const ImDrawData* data, I64 windowGUID);

    bool draw(const GenericDrawCommand& cmd) const;

    void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    void flushCommand(const GFX::CommandBuffer::CommandEntry& entry, const GFX::CommandBuffer& commandBuffer) override;

    void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    [[nodiscard]] const PerformanceMetrics& getPerformanceMetrics() const noexcept override { return _perfMetrics; }

    /// Return the size in pixels that we can render to. This differs from the window size on Retina displays
    vec2<U16> getDrawableSize(const DisplayWindow& window) const noexcept override;

    U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const override;

    void onThreadCreated(const std::thread::id& threadID) override;

    /// Try to find the requested font in the font cache. Load on cache miss.
    I32 getFont(const Str64& fontName);

    /// Reset as much of the GL default state as possible within the limitations given
    void clearStates(const DisplayWindow& window, GLStateTracker* stateTracker, bool global) const;

    [[nodiscard]] GLStateTracker::BindResult makeTexturesResidentInternal(TextureDataContainer& textureData, U8 offset = 0u, U8 count = U8_MAX) const;
    [[nodiscard]] GLStateTracker::BindResult makeTextureViewsResidentInternal(const TextureViews& textureViews, U8 offset = 0u, U8 count = U8_MAX) const;

    bool setViewport(const Rect<I32>& viewport) override;
    ShaderResult bindPipeline(const Pipeline& pipeline) const;

private:
    void endFrameLocal(const DisplayWindow& window);
    void endFrameGlobal(const DisplayWindow& window);

public:
    static [[nodiscard]] GLStateTracker* GetStateTracker() noexcept;
    static [[nodiscard]] GLUtil::GLMemory::GLMemoryType GetMemoryTypeForUsage(GLenum usage) noexcept;
    static [[nodiscard]] GLUtil::GLMemory::DeviceAllocator& GetMemoryAllocator(GLUtil::GLMemory::GLMemoryType memoryType) noexcept;

    static [[nodiscard]] bool MakeTexturesResidentInternal(SamplerAddress address);
    static [[nodiscard]] bool MakeTexturesNonResidentInternal(SamplerAddress address);

    static void QueueFlush() noexcept;

    static [[nodiscard]] SyncObject_uptr& CreateSyncObject(bool isRetry = false);
    static void FlushMidBufferLockQueue(SyncObject_uptr& syncObj);
    static void FlushEndBufferLockQueue(SyncObject_uptr& syncObj);


    static void PushDebugMessage(const char* message);
    static void PopDebugMessage();

    static [[nodiscard]] bool DeleteShaderPrograms(GLuint count, GLuint * programs);
    static [[nodiscard]] bool DeleteTextures(GLuint count, GLuint* textures, TextureType texType);
    static [[nodiscard]] bool DeleteSamplers(GLuint count, GLuint* samplers);
    static [[nodiscard]] bool DeleteBuffers(GLuint count, GLuint* buffers);
    static [[nodiscard]] bool DeleteVAOs(GLuint count, GLuint* vaos);
    static [[nodiscard]] bool DeleteFramebuffers(GLuint count, GLuint* framebuffers);

    static void RegisterBufferLock(const BufferLockEntry&& data, ShaderBufferLockType lockType);

    static [[nodiscard]] IMPrimitive* NewIMP(Mutex& lock, GFXDevice& parent);
    static [[nodiscard]] bool DestroyIMP(Mutex& lock, IMPrimitive*& primitive);

    static [[nodiscard]] GLuint GetSamplerHandle(size_t samplerHash);

    static [[nodiscard]] glHardwareQueryPool* GetHardwareQueryPool() noexcept;

    static [[nodiscard]] GLsync CreateFenceSync();
    static void DestroyFenceSync(GLsync& sync);

private:
    struct WindowGLContext {
        I64 _windowGUID = -1;
        SDL_GLContext _context = nullptr;
    };

    struct ResidentTexture {
        SamplerAddress _address = 0u;
        U8  _frameCount = 0u;
    };

    /// /*sampler hash value*/ /*sampler object*/
    using SamplerObjectMap = hashMap<size_t, GLuint, NoHash<size_t>>;
    using IMPrimitivePool = MemoryPool<glIMPrimitive, 2048>;
    using BufferLockPool = eastl::fixed_vector<SyncObject_uptr, 1024, true>;
    using BufferLockQueue = eastl::fixed_vector<BufferLockEntry, 64, true, eastl::dvd_allocator>;

private:
    GFXDevice& _context;
    Time::ProfileTimer& _swapBufferTimer;

    eastl::fixed_vector<GFX::CommandBuffer::CommandEntry, 512, true> _bufferFlushPoints;

    /// A cache of all fonts used
    hashMap<U64, I32> _fonts;
    hashAlg::pair<Str64, I32> _fontCache = {"", -1};

    /// Hardware query objects used for performance measurements
    std::array<eastl::unique_ptr<glHardwareQueryRing>, to_base(QueryType::COUNT)> _performanceQueries;
    PerformanceMetrics _perfMetrics{};

    WindowGLContext _currentContext{};

    /// FontStash's context
    FONScontext* _fonsContext = nullptr;

    CEGUI::OpenGL3Renderer* _GUIGLrenderer = nullptr;

    //Because GFX::queryPerfStats can change mid-frame, we need to cache that value and apply in beginFrame() properly
    U8 _queryIdxForCurrentFrame = 0u;
    bool _runQueries = false;

private:

    static BufferLockPool s_bufferLockPool;

    static std::atomic_bool s_glFlushQueued;
    static vector<ResidentTexture> s_residentTextures;

    static SharedMutex s_samplerMapLock;
    static SamplerObjectMap s_samplerMap;
    static eastl::unique_ptr<GLStateTracker> s_stateTracker;

    static std::array<GLUtil::GLMemory::DeviceAllocator, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> s_memoryAllocators;
    static std::array<size_t, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> s_memoryAllocatorSizes;

    static GLUtil::glTextureViewCache s_textureViewCache;

    static GLUtil::glVAOCache s_vaoCache;

    static IMPrimitivePool s_IMPrimitivePool;

    static BufferLockQueue s_bufferLockQueueMidFlush;
    static BufferLockQueue s_bufferLockQueueEndOfBuffer;

    static glHardwareQueryPool* s_hardwareQueryPool;

    static U32 s_fenceSyncCounter[s_LockFrameLifetime];

};

};  // namespace Divide
#endif
