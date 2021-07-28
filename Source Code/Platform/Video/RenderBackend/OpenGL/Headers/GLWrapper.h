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
#include "Platform/Video/RenderBackend/OpenGL/Buffers/VertexBuffer/Headers/glVAOPool.h"
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

namespace GLUtil {
    struct glVAOCache;
};

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
    friend struct GLUtil::glVAOCache;

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

    GenericVertexData* getOrCreateIMGUIBuffer(I64 windowGUID);

    /// Text rendering is handled exclusively by Mikko Mononen's FontStash library
    /// (https://github.com/memononen/fontstash)
    /// with his OpenGL frontend adapted for core context profiles
    void drawText(const TextElementBatch& batch);

    void drawIMGUI(ImDrawData* data, I64 windowGUID);

    bool draw(const GenericDrawCommand& cmd) const;

    void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    void flushCommand(const GFX::CommandBuffer::CommandEntry& entry, const GFX::CommandBuffer& commandBuffer) override;

    void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;

    [[nodiscard]] PerformanceMetrics getPerformanceMetrics() const noexcept override;

    /// Return the size in pixels that we can render to. This differs from the window size on Retina displays
    vec2<U16> getDrawableSize(const DisplayWindow& window) const override;

    U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const override;

    void onThreadCreated(const std::thread::id& threadID) override;

    /// Try to find the requested font in the font cache. Load on cache miss.
    I32 getFont(const Str64& fontName);

    /// Reset as much of the GL default state as possible within the limitations given
    void clearStates(const DisplayWindow& window, GLStateTracker& stateTracker, bool global) const;

    bool makeTexturesResidentInternal(TextureDataContainer& textureData, U8 offset = 0, U8 count = std::numeric_limits<U8>::max()) const;
    bool makeTextureViewsResidentInternal(const TextureViews& textureViews, U8 offset = 0, U8 count = std::numeric_limits<U8>::max()) const;
    bool makeTexturesResident(TextureDataContainer& textureData, const TextureViews& textureViews, U8 offset = 0, U8 count = std::numeric_limits<U8>::max()) const;
    bool makeImagesResident(const Images& images) const;

    bool setViewport(const Rect<I32>& viewport) override;
    bool bindPipeline(const Pipeline & pipeline, bool& shaderWasReady) const;
public:
    static GLStateTracker& getStateTracker() noexcept;
    static GLUtil::GLMemory::DeviceAllocator& getMemoryAllocator() noexcept;

    static bool MakeTexturesResidentInternal(SamplerAddress address);
    static bool MakeTexturesNonResidentInternal(SamplerAddress address);

    static void QueueFlush();
    /// Queue a mipmap recalculation
    static void QueueComputeMipMap(GLuint textureHandle);
    static void DequeueComputeMipMap(GLuint textureHandle);

    static void PushDebugMessage(const char* message);
    static void PopDebugMessage();

    static bool DeleteShaderPipelines(GLuint count, GLuint* programsPipelines);
    static bool DeleteShaderPrograms(GLuint count, GLuint * programs);
    static bool DeleteTextures(GLuint count, GLuint* textures, TextureType texType);
    static bool DeleteSamplers(GLuint count, GLuint* samplers);
    static bool DeleteBuffers(GLuint count, GLuint* buffers);
    static bool DeleteVAOs(GLuint count, GLuint* vaos);
    static bool DeleteFramebuffers(GLuint count, GLuint* framebuffers);

    static void RegisterBufferBind(BufferLockEntry&& data, bool fenceAfterFirstDraw);

    using IMPrimitivePool = MemoryPool<glIMPrimitive, 2048>;

    static IMPrimitive* NewIMP(Mutex& lock, GFXDevice& parent);
    static bool DestroyIMP(Mutex& lock, IMPrimitive*& primitive);

    /// Return the OpenGL sampler object's handle for the given hash value
    static GLuint GetSamplerHandle(size_t samplerHash);

private:
    static bool InitGLSW(Configuration& config);
    static bool DeInitGLSW();

    /// Use GLSW to append tokens to shaders. Use ShaderType::COUNT to append to all stages
    static void AppendToShaderHeader(ShaderType type, const stringImpl& entry);

protected:
    /// Number of available texture units
    static GLuint s_maxTextureUnits;
    /// Number of available attribute binding indices
    static GLuint s_maxAttribBindings;
    /// Max number of texture attachments to an FBO
    static GLuint s_maxFBOAttachments;

public:
    /// Shader block data
    static GLuint s_UBOffsetAlignment;
    static GLuint s_UBMaxSize;
    static GLuint s_SSBOffsetAlignment;
    static GLuint s_SSBMaxSize;

    static bool s_UseBindlessTextures;

    static glHardwareQueryPool* s_hardwareQueryPool;

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

    I32 _glswState = -1;
    CEGUI::OpenGL3Renderer* _GUIGLrenderer = nullptr;
    hashMap<I64, GenericVertexData*> _IMGUIBuffers;
    std::pair<I64, SDL_GLContext> _currentContext = {-1, nullptr};

private:
    static std::atomic_bool s_glFlushQueued;

    static SharedMutex s_mipmapQueueSetLock;
    static eastl::unordered_set<GLuint> s_mipmapQueue;

    struct ResidentTexture {
        SamplerAddress _address = 0u;
        U8  _frameCount = 0u;
    };

    static vectorEASTL<ResidentTexture> s_residentTextures;

    /// The main VAO pool. We use a pool to avoid multithreading issues with VAO states
    static GLUtil::glVAOPool s_vaoPool;
    /// Used to render points (e.g. to render full screen quads with geometry shaders)
    static GLuint s_dummyVAO;
    /// Maximum anisotropic filtering level
    static GLuint s_maxAnisotropicFilteringLevel;
    /// /*sampler hash value*/ /*sampler object*/
    using SamplerObjectMap = hashMap<size_t, GLuint, NoHash<size_t>>;

    static SharedMutex s_samplerMapLock;
    static SamplerObjectMap s_samplerMap;
    static GLStateTracker  s_stateTracker;
    static GLUtil::GLMemory::DeviceAllocator s_memoryAllocator;

    static GLUtil::glTextureViewCache s_textureViewCache;

    static IMPrimitivePool s_IMPrimitivePool;
    static eastl::fixed_vector<BufferLockEntry, 64, true> s_bufferLockQueueMidFlush;
    static eastl::fixed_vector<BufferLockEntry, 64, true> s_bufferLockQueueEndOfBuffer;
};

};  // namespace Divide
#endif
