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

#include "glHardwareQueryPool.h"
#include "glIMPrimitive.h"
#include "GLStateTracker.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/PixelBuffer/Headers/glPixelBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferLockManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShader.h"
#include "Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glSamplerObject.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"
#include "Rendering/Camera/Headers/Frustum.h"

struct glslopt_ctx;
struct FONScontext;
struct ImDrawData;

namespace CEGUI {
    class OpenGL3Renderer;
};

namespace Divide {

namespace Time {
    class ProfileTimer;
};

enum class WindowType : U8;

class DisplayWindow;
class PlatformContext;
class RenderStateBlock;
class GenericVertexData;
class glHardwareQueryRing;
struct BufferLockEntry;


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
    GL_API(GFXDevice& context, bool glES);

protected:
    /// Try and create a valid OpenGL context taking in account the specified command line arguments
    ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) override;
    /// Clear everything that was setup in initRenderingAPI()
    void closeRenderingAPI() override;

    /// Prepare the GPU for rendering a frame
    void beginFrame(DisplayWindow& window, bool global = false) override;
    /// Finish rendering the current frame
    void endFrame(DisplayWindow& window, bool global = false) override;
    void idle(bool fast) override;

    /// Text rendering is handled exclusively by Mikko Mononen's FontStash library
    /// (https://github.com/memononen/fontstash)
    /// with his OpenGL frontend adapted for core context profiles
    void drawText(const TextElementBatch& batch);

    void drawIMGUI(const ImDrawData* data, I64 windowGUID);

    bool draw(const GenericDrawCommand& cmd) const;

    void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) noexcept override;

    void flushCommand(const GFX::CommandBuffer::CommandEntry& entry, const GFX::CommandBuffer& commandBuffer) override;

    void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    [[nodiscard]] const PerformanceMetrics& getPerformanceMetrics() const noexcept override;

    /// Return the size in pixels that we can render to. This differs from the window size on Retina displays
    vec2<U16> getDrawableSize(const DisplayWindow& window) const noexcept override;

    U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const override;

    void onThreadCreated(const std::thread::id& threadID) override;

    /// Try to find the requested font in the font cache. Load on cache miss.
    I32 getFont(const Str64& fontName);

    /// Reset as much of the GL default state as possible within the limitations given
    void clearStates(const DisplayWindow& window, GLStateTracker& stateTracker, bool global) const;

    [[nodiscard]] GLStateTracker::BindResult makeTexturesResidentInternal(TextureDataContainer& textureData, U8 offset = 0u, U8 count = U8_MAX) const;
    [[nodiscard]] GLStateTracker::BindResult makeTextureViewsResidentInternal(const TextureViews& textureViews, U8 offset = 0u, U8 count = U8_MAX) const;

    bool setViewport(const Rect<I32>& viewport) override;
    ShaderResult bindPipeline(const Pipeline& pipeline) const;

public:
    static GLStateTracker& GetStateTracker() noexcept;
    static GLUtil::GLMemory::GLMemoryType GetMemoryTypeForUsage(GLenum usage) noexcept;
    static GLUtil::GLMemory::DeviceAllocator& GetMemoryAllocator(GLUtil::GLMemory::GLMemoryType memoryType) noexcept;

    static bool MakeTexturesResidentInternal(SamplerAddress address);
    static bool MakeTexturesNonResidentInternal(SamplerAddress address);

    static void QueueFlush() noexcept;

    /// Queue a mipmap recalculation
    static void QueueComputeMipMaps(GLuint textureHandle);
    static void DequeueComputeMipMaps(GLuint textureHandle);
    static void ComputeMipMaps(GLuint textureHandle);
    static [[nodiscard]] bool ComputeMipMapsQueued(GLuint textureHandle);
    static void ProcessMipMapsQueue() noexcept;

    static void PushDebugMessage(const char* message);
    static void PopDebugMessage();

    static bool DeleteShaderPrograms(GLuint count, GLuint * programs);
    static bool DeleteTextures(GLuint count, GLuint* textures, TextureType texType);
    static bool DeleteSamplers(GLuint count, GLuint* samplers);
    static bool DeleteBuffers(GLuint count, GLuint* buffers);
    static bool DeleteVAOs(GLuint count, GLuint* vaos);
    static bool DeleteFramebuffers(GLuint count, GLuint* framebuffers);

    static void RegisterSyncDelete(GLsync fenceSync);
    static bool TryDeleteExpiredSync();
    static void RegisterBufferBind(const BufferLockEntry&& data, bool fenceAfterFirstDraw);

    using IMPrimitivePool = MemoryPool<glIMPrimitive, 2048>;

    static IMPrimitive* NewIMP(Mutex& lock, GFXDevice& parent);
    static bool DestroyIMP(Mutex& lock, IMPrimitive*& primitive);

    /// Return the OpenGL sampler object's handle for the given hash value
    static GLuint GetSamplerHandle(size_t samplerHash);

    static glHardwareQueryPool* s_hardwareQueryPool;

private:
    void endFrameLocal(const DisplayWindow& window);
    void endFrameGlobal(const DisplayWindow& window);

private:
    GFXDevice& _context;
    Time::ProfileTimer& _swapBufferTimer;

    /// A cache of all fonts used
    hashMap<U64, I32> _fonts;
    hashAlg::pair<Str64, I32> _fontCache = {"", -1};

    /// Hardware query objects used for performance measurements
    std::array<eastl::unique_ptr<glHardwareQueryRing>, to_base(QueryType::COUNT)> _performanceQueries;
    PerformanceMetrics _perfMetrics{};
    //Because GFX::queryPerfStats can change mid-frame, we need to cache that value and apply in beginFrame() properly
    bool _runQueries = false;
    U8 _queryIdxForCurrentFrame = 0u;

    /// FontStash's context
    FONScontext* _fonsContext = nullptr;

    CEGUI::OpenGL3Renderer* _GUIGLrenderer = nullptr;

    struct WindowGLContext {
        I64 _windowGUID = -1;
        SDL_GLContext _context = nullptr;
    };

    WindowGLContext _currentContext{};

private:
    static std::atomic_bool s_glFlushQueued;

    static SharedMutex s_mipmapQueueSetLock;
    static eastl::unordered_set<GLuint> s_mipmapQueue;

    struct ResidentTexture {
        SamplerAddress _address = 0u;
        U8  _frameCount = 0u;
    };

    static vector<ResidentTexture> s_residentTextures;

    /// /*sampler hash value*/ /*sampler object*/
    using SamplerObjectMap = hashMap<size_t, GLuint, NoHash<size_t>>;

    static SharedMutex s_samplerMapLock;
    static SamplerObjectMap s_samplerMap;
    static GLStateTracker  s_stateTracker;

    static std::array<GLUtil::GLMemory::DeviceAllocator, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> s_memoryAllocators;
    static std::array<size_t, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> s_memoryAllocatorSizes;

    static GLUtil::glTextureViewCache s_textureViewCache;

    static IMPrimitivePool s_IMPrimitivePool;
    static moodycamel::ConcurrentQueue<GLsync> s_fenceSyncDeletionQueue;
    static eastl::fixed_vector<BufferLockEntry, 64, true, eastl::dvd_allocator> s_bufferLockQueueMidFlush;
    static eastl::fixed_vector<BufferLockEntry, 64, true, eastl::dvd_allocator> s_bufferLockQueueEndOfBuffer;
};

};  // namespace Divide
#endif
