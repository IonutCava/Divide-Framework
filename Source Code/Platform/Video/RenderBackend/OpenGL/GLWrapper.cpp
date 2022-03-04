#include "stdafx.h"

#include "Headers/GLWrapper.h"

#include "CEGUIOpenGLRenderer/include/Texture.h"
#include "Headers/glHardwareQuery.h"

#include "Platform/File/Headers/FileManagement.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/RenderTarget/Headers/glFramebuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/ShaderBuffer/Headers/glUniformBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/VertexBuffer/Headers/glGenericVertexData.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Rendering/Headers/Renderer.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Geometry/Material/Headers/Material.h"
#include "GUI/Headers/GUIText.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Utility/Headers/Localization.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"

#include <SDL_video.h>

#include <glbinding-aux/Meta.h>

#include "Text/Headers/fontstash.h"

namespace Divide {

namespace {
    // Weird stuff happens if this is enabled (i.e. certain draw calls hang forever)
    constexpr bool g_runAllQueriesInSameFrame = false;
    // Keep resident textures in memory for a max of 30 frames
    constexpr U8 g_maxTextureResidencyFrameCount = Config::TARGET_FRAME_RATE / 2;
}

GLStateTracker GL_API::s_stateTracker;
std::atomic_bool GL_API::s_glFlushQueued;
GLUtil::glTextureViewCache GL_API::s_textureViewCache{};
GL_API::IMPrimitivePool GL_API::s_IMPrimitivePool{};
U32 GL_API::s_fenceSyncCounter = 0u;
eastl::fixed_vector<BufferLockEntry, 64, true, eastl::dvd_allocator> GL_API::s_bufferLockQueueMidFlush;
eastl::fixed_vector<BufferLockEntry, 64, true, eastl::dvd_allocator> GL_API::s_bufferLockQueueEndOfBuffer;

std::array<GLUtil::GLMemory::DeviceAllocator, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> GL_API::s_memoryAllocators = {
    GLUtil::GLMemory::DeviceAllocator(GLUtil::GLMemory::GLMemoryType::SHADER_BUFFER),
    GLUtil::GLMemory::DeviceAllocator(GLUtil::GLMemory::GLMemoryType::VERTEX_BUFFER),
    GLUtil::GLMemory::DeviceAllocator(GLUtil::GLMemory::GLMemoryType::INDEX_BUFFER),
    GLUtil::GLMemory::DeviceAllocator(GLUtil::GLMemory::GLMemoryType::OTHER)
};

#define TO_MEGABYTES(X) (X * 1024u * 1024u)
std::array<size_t, to_base(GLUtil::GLMemory::GLMemoryType::COUNT)> GL_API::s_memoryAllocatorSizes {
    TO_MEGABYTES(512),
    TO_MEGABYTES(1024),
    TO_MEGABYTES(256),
    TO_MEGABYTES(256)
};

GL_API::GL_API(GFXDevice& context, [[maybe_unused]] const bool glES)
    : RenderAPIWrapper(),
      _context(context),
      _swapBufferTimer(Time::ADD_TIMER("Swap Buffer Timer"))
{
    std::atomic_init(&s_glFlushQueued, false);
}

/// Prepare the GPU for rendering a frame
void GL_API::beginFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT();
    // Start a duration query in debug builds
    if (global && _runQueries) {
        if_constexpr(g_runAllQueriesInSameFrame) {
            for (U8 i = 0u; i < to_base(QueryType::COUNT); ++i) {
                _performanceQueries[i]->begin();
            }
        } else {
            _performanceQueries[_queryIdxForCurrentFrame]->begin();
        }
    }

    GLStateTracker& stateTracker = GetStateTracker();

    SDL_GLContext glContext = static_cast<SDL_GLContext>(window.userData());
    const I64 windowGUID = window.getGUID();

    if (glContext != nullptr && (_currentContext._windowGUID != windowGUID || _currentContext._context != glContext)) {
        SDL_GL_MakeCurrent(window.getRawWindow(), glContext);
        _currentContext._windowGUID = windowGUID;
        _currentContext._context = glContext;
    }

    // Clear our buffers
    if (!window.minimized() && !window.hidden()) {
        bool shouldClearColour = false, shouldClearDepth = false, shouldClearStencil = false;
        stateTracker.setClearColour(window.clearColour(shouldClearColour, shouldClearDepth));
        ClearBufferMask mask = ClearBufferMask::GL_NONE_BIT;
        if (shouldClearColour) {
            mask |= ClearBufferMask::GL_COLOR_BUFFER_BIT;
        }
        if (shouldClearDepth) {
            mask |= ClearBufferMask::GL_DEPTH_BUFFER_BIT;
        }
        if (shouldClearStencil) {
            mask |= ClearBufferMask::GL_STENCIL_BUFFER_BIT;
        }
        if (mask != ClearBufferMask::GL_NONE_BIT) {
            glClear(mask);
        }
    }
    // Clears are registered as draw calls by most software, so we do the same
    // to stay in sync with third party software
    _context.registerDrawCall();

    clearStates(window, stateTracker, global);
}

void GL_API::endFrameLocal(const DisplayWindow& window) {
    OPTICK_EVENT();

    // Swap buffers    
    SDL_GLContext glContext = static_cast<SDL_GLContext>(window.userData());
    const I64 windowGUID = window.getGUID();

    if (glContext != nullptr && (_currentContext._windowGUID != windowGUID || _currentContext._context != glContext)) {
        OPTICK_EVENT("GL_API: Swap Context");
        SDL_GL_MakeCurrent(window.getRawWindow(), glContext);
        _currentContext._windowGUID = windowGUID;
        _currentContext._context = glContext;
    }
    {
        OPTICK_EVENT("GL_API: Swap Buffers");
        SDL_GL_SwapWindow(window.getRawWindow());
    }
}

void GL_API::endFrameGlobal(const DisplayWindow& window) {
    OPTICK_EVENT();

    if (_runQueries) {
        OPTICK_EVENT("End GPU Queries");
        // End the timing query started in beginFrame() in debug builds

        if_constexpr(g_runAllQueriesInSameFrame) {
            for (U8 i = 0; i < to_base(QueryType::COUNT); ++i) {
                _performanceQueries[i]->end();
            }
        } else {
            _performanceQueries[_queryIdxForCurrentFrame]->end();
        }
    }

    if (glGetGraphicsResetStatus() != GL_NO_ERROR) {
        DIVIDE_UNEXPECTED_CALL_MSG("OpenGL Reset Status raised!");
    }

    _swapBufferTimer.start();
    endFrameLocal(window);
    {
        //OPTICK_EVENT("Post-swap delay");
        //SDL_Delay(1);
    }
    _swapBufferTimer.stop();

    OPTICK_EVENT("GL_API: Post-Swap cleanup");
    s_textureViewCache.onFrameEnd();
    s_glFlushQueued.store(false);
    if (ShaderProgram::s_UseBindlessTextures) {
        for (ResidentTexture& texture : s_residentTextures) {
            if (texture._address == 0u) {
                // Most common case
                continue;
            }

            if (++texture._frameCount > g_maxTextureResidencyFrameCount) {
                glMakeTextureHandleNonResidentARB(texture._address);
                texture = {};
            }
        }
    }

    if (_runQueries) {
        OPTICK_EVENT("GL_API: Time Query");
        static std::array<I64, to_base(QueryType::COUNT)> results{};
        if_constexpr(g_runAllQueriesInSameFrame) {
            for (U8 i = 0; i < to_base(QueryType::COUNT); ++i) {
                results[i] = _performanceQueries[i]->getResultNoWait();
                _performanceQueries[i]->incQueue();
            }
        } else {
            results[_queryIdxForCurrentFrame] = _performanceQueries[_queryIdxForCurrentFrame]->getResultNoWait();
            _performanceQueries[_queryIdxForCurrentFrame]->incQueue();
        }

        _queryIdxForCurrentFrame = (_queryIdxForCurrentFrame + 1) % to_base(QueryType::COUNT);

        if (g_runAllQueriesInSameFrame || _queryIdxForCurrentFrame == 0) {
            _perfMetrics._gpuTimeInMS = Time::NanosecondsToMilliseconds<F32>(results[to_base(QueryType::GPU_TIME)]);
            _perfMetrics._verticesSubmitted = to_U64(results[to_base(QueryType::VERTICES_SUBMITTED)]);
            _perfMetrics._primitivesGenerated = to_U64(results[to_base(QueryType::PRIMITIVES_GENERATED)]);
            _perfMetrics._tessellationPatches = to_U64(results[to_base(QueryType::TESSELLATION_PATCHES)]);
            _perfMetrics._tessellationInvocations = to_U64(results[to_base(QueryType::TESSELLATION_CTRL_INVOCATIONS)]);
        }
    }

    _perfMetrics._syncObjectsInFlight = s_fenceSyncCounter;

    _runQueries = _context.queryPerformanceStats();
}

/// Finish rendering the current frame
void GL_API::endFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT();

    if (global) {
        endFrameGlobal(window);
    } else {
        endFrameLocal(window);
    }
}

const PerformanceMetrics& GL_API::getPerformanceMetrics() const noexcept {
    return _perfMetrics;
}

void GL_API::idle([[maybe_unused]] const bool fast) {
    glShaderProgram::Idle(_context.context());
}

/// Try to find the requested font in the font cache. Load on cache miss.
I32 GL_API::getFont(const Str64& fontName) {
    if (_fontCache.first.compare(fontName) != 0) {
        _fontCache.first = fontName;
        const U64 fontNameHash = _ID(fontName.c_str());
        // Search for the requested font by name
        const auto& it = _fonts.find(fontNameHash);
        // If we failed to find it, it wasn't loaded yet
        if (it == std::cend(_fonts)) {
            // Fonts are stored in the general asset directory -> in the GUI
            // subfolder -> in the fonts subfolder
            ResourcePath fontPath(Paths::g_assetsLocation + Paths::g_GUILocation + Paths::g_fontsPath);
            fontPath += fontName.c_str();
            // We use FontStash to load the font file
            _fontCache.second = fonsAddFont(_fonsContext, fontName.c_str(), fontPath.c_str());
            // If the font is invalid, inform the user, but map it anyway, to avoid
            // loading an invalid font file on every request
            if (_fontCache.second == FONS_INVALID) {
                Console::errorfn(Locale::Get(_ID("ERROR_FONT_FILE")), fontName.c_str());
            }
            // Save the font in the font cache
            hashAlg::insert(_fonts, fontNameHash, _fontCache.second);
            
        } else {
            _fontCache.second = it->second;
        }

    }

    // Return the font
    return _fontCache.second;
}

/// Text rendering is handled exclusively by Mikko Mononen's FontStash library (https://github.com/memononen/fontstash)
/// with his OpenGL frontend adapted for core context profiles
void GL_API::drawText(const TextElementBatch& batch) {
    OPTICK_EVENT();

    BlendingSettings textBlend{};
    textBlend.blendSrc(BlendProperty::SRC_ALPHA);
    textBlend.blendDest(BlendProperty::INV_SRC_ALPHA);
    textBlend.blendOp(BlendOperation::ADD);
    textBlend.blendSrcAlpha(BlendProperty::ONE);
    textBlend.blendDestAlpha(BlendProperty::ZERO);
    textBlend.blendOpAlpha(BlendOperation::COUNT);
    textBlend.enabled(true);

    GetStateTracker().setBlending(0, textBlend);
    GetStateTracker().setBlendColour(DefaultColours::BLACK_U8);

    const I32 width = _context.renderingResolution().width;
    const I32 height = _context.renderingResolution().height;
        
    size_t drawCount = 0;
    size_t previousStyle = 0;

    fonsClearState(_fonsContext);
    for (const TextElement& entry : batch.data())
    {
        if (previousStyle != entry.textLabelStyleHash()) {
            const TextLabelStyle& textLabelStyle = TextLabelStyle::get(entry.textLabelStyleHash());
            const UColour4& colour = textLabelStyle.colour();
            // Retrieve the font from the font cache
            const I32 font = getFont(TextLabelStyle::fontName(textLabelStyle.font()));
            // The font may be invalid, so skip this text label
            if (font != FONS_INVALID) {
                fonsSetFont(_fonsContext, font);
            }
            fonsSetBlur(_fonsContext, textLabelStyle.blurAmount());
            fonsSetBlur(_fonsContext, textLabelStyle.spacing());
            fonsSetAlign(_fonsContext, textLabelStyle.alignFlag());
            fonsSetSize(_fonsContext, to_F32(textLabelStyle.fontSize()));
            fonsSetColour(_fonsContext, colour.r, colour.g, colour.b, colour.a);
            previousStyle = entry.textLabelStyleHash();
        }

        const F32 textX = entry.position().d_x.d_scale * width + entry.position().d_x.d_offset;
        const F32 textY = height - (entry.position().d_y.d_scale * height + entry.position().d_y.d_offset);

        F32 lh = 0;
        fonsVertMetrics(_fonsContext, nullptr, nullptr, &lh);
        
        const TextElement::TextType& text = entry.text();
        const size_t lineCount = text.size();
        for (size_t i = 0; i < lineCount; ++i) {
            fonsDrawText(_fonsContext,
                         textX,
                         textY - lh * i,
                         text[i].c_str(),
                         nullptr);
        }
        drawCount += lineCount;
        

        // Register each label rendered as a draw call
        _context.registerDrawCalls(to_U32(drawCount));
    }
}

void GL_API::drawIMGUI(const ImDrawData* data, I64 windowGUID) {
    static I32 s_maxCommandCount = 8u;

    OPTICK_EVENT();

    assert(data != nullptr);
    if (data->Valid) {
        s_maxCommandCount = std::max(s_maxCommandCount, data->CmdListsCount);

        GLStateTracker& stateTracker = GetStateTracker();

        GenericVertexData::IndexBuffer idxBuffer;
        idxBuffer.smallIndices = sizeof(ImDrawIdx) == sizeof(U16);

        GenericDrawCommand cmd = {};

        GenericVertexData* buffer = _context.getOrCreateIMGUIBuffer(windowGUID, s_maxCommandCount);
        assert(buffer != nullptr);

        for (I32 n = 0; n < data->CmdListsCount; ++n) {

            const ImDrawList* cmd_list = data->CmdLists[n];

            idxBuffer.count = to_U32(cmd_list->IdxBuffer.Size);
            idxBuffer.data = cmd_list->IdxBuffer.Data;

            buffer->incQueue();
            buffer->updateBuffer(0u, 0u, to_U32(cmd_list->VtxBuffer.size()), cmd_list->VtxBuffer.Data);
            buffer->updateIndexBuffer(idxBuffer);

            for (const ImDrawCmd& pcmd : cmd_list->CmdBuffer) {

                if (pcmd.UserCallback) {
                    // User callback (registered via ImDrawList::AddCallback)
                    pcmd.UserCallback(cmd_list, &pcmd);
                } else {
                    Rect<I32> clip_rect = {
                        pcmd.ClipRect.x - data->DisplayPos.x,
                        pcmd.ClipRect.y - data->DisplayPos.y,
                        pcmd.ClipRect.z - data->DisplayPos.x,
                        pcmd.ClipRect.w - data->DisplayPos.y
                    };

                    const Rect<I32>& viewport = stateTracker._activeViewport;
                    if (clip_rect.x < viewport.z &&
                        clip_rect.y < viewport.w &&
                        clip_rect.z >= 0 &&
                        clip_rect.w >= 0)
                    {
                        const I32 tempW = clip_rect.w;
                        clip_rect.z -= clip_rect.x;
                        clip_rect.w -= clip_rect.y;
                        clip_rect.y  = viewport.w - tempW;

                        stateTracker.setScissor(clip_rect);
                        if (stateTracker.bindTexture(to_U8(TextureUsage::UNIT0),
                                                     TextureType::TEXTURE_2D,
                                                     static_cast<GLuint>(reinterpret_cast<intptr_t>(pcmd.TextureId))) == GLStateTracker::BindResult::FAILED) {
                            DIVIDE_UNEXPECTED_CALL();
                        }

                        cmd._cmd.indexCount = pcmd.ElemCount;
                        cmd._cmd.firstIndex = pcmd.IdxOffset;
                        buffer->draw(cmd);
                     }
                }
            }
        }
    }
}

ShaderResult GL_API::bindPipeline(const Pipeline& pipeline) const {
    OPTICK_EVENT();
    GLStateTracker& stateTracker = GetStateTracker();

    if (stateTracker._activePipeline && *stateTracker._activePipeline == pipeline) {
        return ShaderResult::OK;
    }
    stateTracker._activePipeline = &pipeline;

    const PipelineDescriptor& pipelineDescriptor = pipeline.descriptor();
    {
        OPTICK_EVENT("Set Vertex Format");
        stateTracker.setVertexFormat(pipelineDescriptor._primitiveTopology,
                                     pipeline.vertexFormatHash(),
                                     pipelineDescriptor._vertexFormat);
    }
    {
        OPTICK_EVENT("Set Raster State");
        // Set the proper render states
        const size_t stateBlockHash = pipelineDescriptor._stateHash == 0u ? _context.getDefaultStateBlock(false) : pipelineDescriptor._stateHash;
        // Passing 0 is a perfectly acceptable way of enabling the default render state block
        if (stateTracker.setStateBlock(stateBlockHash) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
    {
        OPTICK_EVENT("Set Blending");
        U16 i = 0u;
        stateTracker.setBlendColour(pipelineDescriptor._blendStates._blendColour);
        for (const BlendingSettings& blendState : pipelineDescriptor._blendStates._settings) {
            stateTracker.setBlending(i++, blendState);
        }
    }

    ShaderResult ret = ShaderResult::Failed;
    {
        OPTICK_EVENT("Set Shader Program");
        ShaderProgram* program = ShaderProgram::FindShaderProgram(pipelineDescriptor._shaderProgramHandle);
        if (program != nullptr) {
            glShaderProgram& glProgram = static_cast<glShaderProgram&>(*program);
            // We need a valid shader as no fixed function pipeline is available
            // Try to bind the shader program. If it failed to load, or isn't loaded yet, cancel the draw request for this frame
            ret = Attorney::GLAPIShaderProgram::bind(glProgram);
        }

        if (ret != ShaderResult::OK) {
            if (stateTracker.setActiveProgram(0u) == GLStateTracker::BindResult::FAILED) {
                DIVIDE_UNEXPECTED_CALL();
            }
            if (stateTracker.setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED) {
                DIVIDE_UNEXPECTED_CALL();
            }
            stateTracker._activePipeline = nullptr;
        }
    }
    return ret;
}

bool GL_API::draw(const GenericDrawCommand& cmd) const {
    OPTICK_EVENT();

    if (cmd._sourceBuffer._id == 0) {
        U32 indexCount = 0u;
        switch (GL_API::GetStateTracker()._activeTopology) {
            case PrimitiveTopology::COUNT     : DIVIDE_UNEXPECTED_CALL();         break;
            case PrimitiveTopology::TRIANGLES : indexCount = cmd._drawCount * 3;  break;
            case PrimitiveTopology::POINTS    : indexCount = cmd._drawCount * 1;  break;
            default                           : indexCount = cmd._cmd.indexCount; break;
        }

        glDrawArrays(GLUtil::glPrimitiveTypeTable[to_base(GL_API::GetStateTracker()._activeTopology)], cmd._cmd.firstIndex, indexCount);
    } else {
        // Because this can only happen on the main thread, try and avoid costly lookups for hot-loop drawing
        static VertexDataInterface::Handle s_lastID = { U16_MAX, 0u };
        static VertexDataInterface* s_lastBuffer = nullptr;

        if (s_lastID != cmd._sourceBuffer) {
            s_lastID = cmd._sourceBuffer;
            s_lastBuffer = VertexDataInterface::s_VDIPool.find(s_lastID);
        }

        DIVIDE_ASSERT(s_lastBuffer != nullptr);
        s_lastBuffer->draw(cmd);
    }

    return true;
}

void GL_API::PushDebugMessage(const char* message) {
    OPTICK_EVENT();

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, static_cast<GLuint>(_ID(message)), -1, message);
    }
    assert(GetStateTracker()._debugScopeDepth < GetStateTracker()._debugScope.size());
    GetStateTracker()._debugScope[GetStateTracker()._debugScopeDepth++] = message;
}

void GL_API::PopDebugMessage() {
    OPTICK_EVENT();

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glPopDebugGroup();
    }
    GetStateTracker()._debugScope[GetStateTracker()._debugScopeDepth--] = "";
}

void GL_API::FlushMidBufferLockQueue() {
    OPTICK_EVENT();
    
    for (const BufferLockEntry& lockEntry : s_bufferLockQueueMidFlush) {
        if (!lockEntry._buffer->lockByteRange(lockEntry._offset, lockEntry._length, GFXDevice::FrameCount())) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
    s_bufferLockQueueMidFlush.resize(0);
}

void GL_API::FlushEndBufferLockQueue() {
    OPTICK_EVENT();

    for (const BufferLockEntry& lockEntry : s_bufferLockQueueEndOfBuffer) {
        if (!lockEntry._buffer->lockByteRange(lockEntry._offset, lockEntry._length, GFXDevice::FrameCount())) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
    s_bufferLockQueueEndOfBuffer.resize(0);
}

void GL_API::preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) {
    OPTICK_EVENT();

    s_IsFlushingCommandBuffer = true;

    _bufferFlushPoints.resize(0);
    GFX::CommandType prevCmdType = GFX::CommandType::COUNT;
    const GFX::CommandBuffer::CommandOrderContainer& commands = commandBuffer();
    for (const GFX::CommandBuffer::CommandEntry& cmd : commands) {
        const GFX::CommandType cmdType = static_cast<GFX::CommandType>(cmd._typeIndex);
        if (cmdType != prevCmdType) {
            if (prevCmdType == GFX::CommandType::DRAW_COMMANDS ||
                prevCmdType == GFX::CommandType::DISPATCH_COMPUTE ||
                prevCmdType == GFX::CommandType::EXTERNAL ||
                prevCmdType == GFX::CommandType::DRAW_TEXT ||
                prevCmdType == GFX::CommandType::DRAW_IMGUI)
            {
                _bufferFlushPoints.push_back(cmd);
            }
            prevCmdType = cmdType;
        }
    }
}

void GL_API::flushCommand(const GFX::CommandBuffer::CommandEntry& entry, const GFX::CommandBuffer& commandBuffer) {
    OPTICK_EVENT();

    const GFX::CommandType cmdType = static_cast<GFX::CommandType>(entry._typeIndex);

    OPTICK_TAG("Type", to_base(cmdType));

    switch (cmdType) {
        case GFX::CommandType::BEGIN_RENDER_PASS: {
            OPTICK_EVENT("BEGIN_RENDER_PASS");

            const GFX::BeginRenderPassCommand* crtCmd = commandBuffer.get<GFX::BeginRenderPassCommand>(entry);

            glFramebuffer& rt = static_cast<glFramebuffer&>(_context.renderTargetPool().renderTarget(crtCmd->_target));
            Attorney::GLAPIRenderTarget::begin(rt, crtCmd->_descriptor);
            GetStateTracker()._activeRenderTarget = &rt;
            PushDebugMessage(crtCmd->_name.c_str());
        }break;
        case GFX::CommandType::END_RENDER_PASS: {
            OPTICK_EVENT("END_RENDER_PASS");

            const GFX::EndRenderPassCommand* crtCmd = commandBuffer.get<GFX::EndRenderPassCommand>(entry);

            assert(GL_API::GetStateTracker()._activeRenderTarget != nullptr);
            PopDebugMessage();
            const glFramebuffer& fb = *GetStateTracker()._activeRenderTarget;
            Attorney::GLAPIRenderTarget::end(fb, crtCmd->_setDefaultRTState);
        }break;
        case GFX::CommandType::BEGIN_PIXEL_BUFFER: {
            OPTICK_EVENT("BEGIN_PIXEL_BUFFER");

            const GFX::BeginPixelBufferCommand* crtCmd = commandBuffer.get<GFX::BeginPixelBufferCommand>(entry);

            assert(crtCmd->_buffer != nullptr);
            glPixelBuffer* buffer = static_cast<glPixelBuffer*>(crtCmd->_buffer);
            const bufferPtr data = Attorney::GLAPIPixelBuffer::begin(*buffer);
            if (crtCmd->_command) {
                crtCmd->_command(data);
            }
            GetStateTracker()._activePixelBuffer = buffer;
        }break;
        case GFX::CommandType::END_PIXEL_BUFFER: {
            OPTICK_EVENT("END_PIXEL_BUFFER");

            assert(GL_API::GetStateTracker()._activePixelBuffer != nullptr);
            Attorney::GLAPIPixelBuffer::end(*GetStateTracker()._activePixelBuffer);
        }break;
        case GFX::CommandType::BEGIN_RENDER_SUB_PASS: {
            OPTICK_EVENT("BEGIN_RENDER_SUB_PASS");

            const GFX::BeginRenderSubPassCommand* crtCmd = commandBuffer.get<GFX::BeginRenderSubPassCommand>(entry);

            assert(GL_API::GetStateTracker()._activeRenderTarget != nullptr);
            for (const RenderTarget::DrawLayerParams& params : crtCmd->_writeLayers) {
                GetStateTracker()._activeRenderTarget->drawToLayer(params);
            }

            GetStateTracker()._activeRenderTarget->setMipLevel(crtCmd->_mipWriteLevel);
        }break;
        case GFX::CommandType::END_RENDER_SUB_PASS: {
            OPTICK_EVENT("END_RENDER_SUB_PASS");
        }break;
        case GFX::CommandType::COPY_TEXTURE: {
            OPTICK_EVENT("COPY_TEXTURE");

            const GFX::CopyTextureCommand* crtCmd = commandBuffer.get<GFX::CopyTextureCommand>(entry);
            glTexture::copy(crtCmd->_source, crtCmd->_destination, crtCmd->_params);
        }break;
        case GFX::CommandType::BIND_DESCRIPTOR_SETS: {
            OPTICK_EVENT("BIND_DESCRIPTOR_SETS");

            GFX::BindDescriptorSetsCommand* crtCmd = commandBuffer.get<GFX::BindDescriptorSetsCommand>(entry);
            if (!crtCmd->_set._textureViews.empty() &&
                makeTextureViewsResidentInternal(crtCmd->_set._textureViews, 0u, U8_MAX) == GLStateTracker::BindResult::FAILED)
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            if (!crtCmd->_set._textureData.empty() &&
                makeTexturesResidentInternal(crtCmd->_set._textureData, 0u, U8_MAX) == GLStateTracker::BindResult::FAILED)
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }break;
        case GFX::CommandType::BIND_PIPELINE: {
            OPTICK_EVENT("BIND_PIPELINE");

            const Pipeline* pipeline = commandBuffer.get<GFX::BindPipelineCommand>(entry)->_pipeline;
            assert(pipeline != nullptr);
            if (bindPipeline(*pipeline) == ShaderResult::Failed) {
                Console::errorfn(Locale::Get(_ID("ERROR_GLSL_INVALID_BIND")), pipeline->descriptor()._shaderProgramHandle);
            }
        } break;
        case GFX::CommandType::SEND_PUSH_CONSTANTS: {
            OPTICK_EVENT("SEND_PUSH_CONSTANTS");

            const auto dumpLogs = [this]() {
                Console::d_errorfn(Locale::Get(_ID("ERROR_GLSL_INVALID_PUSH_CONSTANTS")));
                if (Config::ENABLE_GPU_VALIDATION) {
                    // Shader failed to compile probably. Dump all shader caches for inspection.
                    glShaderProgram::Idle(_context.context());
                    Console::flush();
                }
            };

            const Pipeline* activePipeline = GetStateTracker()._activePipeline;
            if (activePipeline == nullptr) {
                dumpLogs();
                break;
            }

            ShaderProgram* program = ShaderProgram::FindShaderProgram(activePipeline->descriptor()._shaderProgramHandle);
            if (program == nullptr) {
                // Should we skip the upload?
                dumpLogs();
                break;
            }

            const PushConstants& pushConstants = commandBuffer.get<GFX::SendPushConstantsCommand>(entry)->_constants;
            static_cast<glShaderProgram*>(program)->uploadPushConstants(pushConstants);
        } break;
        case GFX::CommandType::SET_SCISSOR: {
            OPTICK_EVENT("SET_SCISSOR");

            GetStateTracker().setScissor(commandBuffer.get<GFX::SetScissorCommand>(entry)->_rect);
        }break;
        case GFX::CommandType::SET_TEXTURE_RESIDENCY: {
            OPTICK_EVENT("SET_TEXTURE_RESIDENCY");

            const GFX::SetTexturesResidencyCommand* crtCmd = commandBuffer.get<GFX::SetTexturesResidencyCommand>(entry);
            if (crtCmd->_state) {
                for (const SamplerAddress address : crtCmd->_addresses) {
                    MakeTexturesResidentInternal(address);
                }
            } else {
                for (const SamplerAddress address : crtCmd->_addresses) {
                    MakeTexturesNonResidentInternal(address);
                }
            }
        }break;
        case GFX::CommandType::BEGIN_DEBUG_SCOPE: {
            OPTICK_EVENT("BEGIN_DEBUG_SCOPE");

             const GFX::BeginDebugScopeCommand* crtCmd = commandBuffer.get<GFX::BeginDebugScopeCommand>(entry);

             PushDebugMessage(crtCmd->_scopeName.c_str());
        } break;
        case GFX::CommandType::END_DEBUG_SCOPE: {
            OPTICK_EVENT("END_DEBUG_SCOPE");

             PopDebugMessage();
        } break;
        case GFX::CommandType::ADD_DEBUG_MESSAGE: {
            OPTICK_EVENT("ADD_DEBUG_MESSAGE");

            const GFX::AddDebugMessageCommand* crtCmd = commandBuffer.get<GFX::AddDebugMessageCommand>(entry);

            PushDebugMessage(crtCmd->_msg.c_str());
            PopDebugMessage();
        }break;
        case GFX::CommandType::COMPUTE_MIPMAPS: {
            OPTICK_EVENT("COMPUTE_MIPMAPS");

            const GFX::ComputeMipMapsCommand* crtCmd = commandBuffer.get<GFX::ComputeMipMapsCommand>(entry);

            if (crtCmd->_layerRange.x == 0 && crtCmd->_layerRange.y == crtCmd->_texture->descriptor().layerCount()) {
                OPTICK_EVENT("GL: In-place computation - Full");
                glGenerateTextureMipmap(crtCmd->_texture->data()._textureHandle);
            } else {
                OPTICK_EVENT("GL: View-based computation");

                const TextureDescriptor& descriptor = crtCmd->_texture->descriptor();
                const GLenum glInternalFormat = GLUtil::internalFormat(descriptor.baseFormat(), descriptor.dataType(), descriptor.srgb(), descriptor.normalized());

                TextureView view = {};
                view._textureData = crtCmd->_texture->data();
                view._layerRange.set(crtCmd->_layerRange);
                view._targetType = view._textureData._textureType;

                if (crtCmd->_mipRange.max == 0u) {
                    view._mipLevels.set(0, crtCmd->_texture->mipCount());
                } else {
                    view._mipLevels.set(crtCmd->_mipRange);
                }
                assert(IsValid(view._textureData));

                if (IsArrayTexture(view._targetType) && view._layerRange.max == 1) {
                    switch (view._targetType) {
                        case TextureType::TEXTURE_2D_ARRAY:
                            view._targetType = TextureType::TEXTURE_2D;
                            break;
                        case TextureType::TEXTURE_2D_ARRAY_MS:
                            view._targetType = TextureType::TEXTURE_2D_MS;
                            break;
                        case TextureType::TEXTURE_CUBE_ARRAY:
                            view._targetType = TextureType::TEXTURE_CUBE_MAP;
                            break;
                        default: break;
                    }
                }

                if (IsCubeTexture(view._targetType)) {
                    view._layerRange *= 6; //offset and count
                }

                auto[handle, cacheHit] = s_textureViewCache.allocate(view.getHash());

                if (!cacheHit)
                {
                    OPTICK_EVENT("GL: cache miss  - Image");
                    glTextureView(handle,
                                    GLUtil::glTextureTypeTable[to_base(view._targetType)],
                                    view._textureData._textureHandle,
                                    glInternalFormat,
                                    static_cast<GLuint>(view._mipLevels.x),
                                    static_cast<GLuint>(view._mipLevels.y),
                                    static_cast<GLuint>(view._layerRange.x),
                                    static_cast<GLuint>(view._layerRange.y));
                }
                if (view._mipLevels.x != 0u || view._mipLevels.y != 0u) {
                    OPTICK_EVENT("GL: In-place computation - Image");
                    glGenerateTextureMipmap(handle);
                }
                s_textureViewCache.deallocate(handle, 3);
            }
        }break;
        case GFX::CommandType::DRAW_TEXT: {
            OPTICK_EVENT("DRAW_TEXT");

            if (GetStateTracker()._activePipeline != nullptr) {
                const GFX::DrawTextCommand* crtCmd = commandBuffer.get<GFX::DrawTextCommand>(entry);
                drawText(crtCmd->_batch);
            }
        }break;
        case GFX::CommandType::DRAW_IMGUI: {
            OPTICK_EVENT("DRAW_IMGUI");

            if (GetStateTracker()._activePipeline != nullptr) {
                const GFX::DrawIMGUICommand* crtCmd = commandBuffer.get<GFX::DrawIMGUICommand>(entry);
                drawIMGUI(crtCmd->_data, crtCmd->_windowGUID);
            }
        }break;
        case GFX::CommandType::DRAW_COMMANDS : {
            OPTICK_EVENT("DRAW_COMMANDS");

            const GLStateTracker& stateTracker = GetStateTracker();
            const GFX::DrawCommand::CommandContainer& drawCommands = commandBuffer.get<GFX::DrawCommand>(entry)->_drawCommands;

            U32 drawCount = 0u;

            DIVIDE_ASSERT(drawCount == 0u || stateTracker._activePipeline != nullptr);

            for (const GenericDrawCommand& currentDrawCommand : drawCommands) {
                if (draw(currentDrawCommand)) {
                    drawCount += isEnabledOption(currentDrawCommand, CmdRenderOptions::RENDER_WIREFRAME) 
                                       ? 2 
                                       : isEnabledOption(currentDrawCommand, CmdRenderOptions::RENDER_GEOMETRY) ? 1 : 0;
                }
            }
            _context.registerDrawCalls(drawCount);
        }break;
        case GFX::CommandType::DISPATCH_COMPUTE: {
            OPTICK_EVENT("DISPATCH_COMPUTE");

            const GFX::DispatchComputeCommand* crtCmd = commandBuffer.get<GFX::DispatchComputeCommand>(entry);

            if(GetStateTracker()._activePipeline != nullptr) {
                OPTICK_EVENT("GL: Dispatch Compute");
                const vec3<U32>& workGroupCount = crtCmd->_computeGroupSize;
                DIVIDE_ASSERT(workGroupCount.x < GFXDevice::GetDeviceInformation()._maxWorgroupCount[0] &&
                              workGroupCount.y < GFXDevice::GetDeviceInformation()._maxWorgroupCount[1] &&
                              workGroupCount.z < GFXDevice::GetDeviceInformation()._maxWorgroupCount[2]);
                glDispatchCompute(crtCmd->_computeGroupSize.x, crtCmd->_computeGroupSize.y, crtCmd->_computeGroupSize.z);
            }
        }break;
        case GFX::CommandType::SET_CLIPING_STATE: {
            OPTICK_EVENT("SET_CLIPING_STATE");

            const GFX::SetClippingStateCommand* crtCmd = commandBuffer.get<GFX::SetClippingStateCommand>(entry);

            GetStateTracker().setClippingPlaneState(crtCmd->_lowerLeftOrigin, crtCmd->_negativeOneToOneDepth);
        } break;
        case GFX::CommandType::MEMORY_BARRIER: {
            OPTICK_EVENT("MEMORY_BARRIER");

            const GFX::MemoryBarrierCommand* crtCmd = commandBuffer.get<GFX::MemoryBarrierCommand>(entry);

            MemoryBarrierMask glMask = MemoryBarrierMask::GL_NONE_BIT;
            const U32 barrierMask = crtCmd->_barrierMask;
            if (barrierMask != 0) {
                if (BitCompare(barrierMask, to_base(MemoryBarrierType::TEXTURE_BARRIER))) {
                    glTextureBarrier();
                } 
                if (barrierMask == to_base(MemoryBarrierType::ALL_MEM_BARRIERS)) {
                    glMemoryBarrier(MemoryBarrierMask::GL_ALL_BARRIER_BITS);
                } else {
                    for (U8 i = 0; i < to_U8(MemoryBarrierType::COUNT) + 1; ++i) {
                        if (BitCompare(barrierMask, 1u << i)) {
                            switch (static_cast<MemoryBarrierType>(1 << i)) {
                            case MemoryBarrierType::BUFFER_UPDATE:
                                glMask |= MemoryBarrierMask::GL_BUFFER_UPDATE_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::SHADER_STORAGE:
                                glMask |= MemoryBarrierMask::GL_SHADER_STORAGE_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::COMMAND_BUFFER:
                                glMask |= MemoryBarrierMask::GL_COMMAND_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::ATOMIC_COUNTER:
                                glMask |= MemoryBarrierMask::GL_ATOMIC_COUNTER_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::QUERY:
                                glMask |= MemoryBarrierMask::GL_QUERY_BUFFER_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::RENDER_TARGET:
                                glMask |= MemoryBarrierMask::GL_FRAMEBUFFER_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::TEXTURE_UPDATE:
                                glMask |= MemoryBarrierMask::GL_TEXTURE_UPDATE_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::TEXTURE_FETCH:
                                glMask |= MemoryBarrierMask::GL_TEXTURE_FETCH_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::SHADER_IMAGE:
                                glMask |= MemoryBarrierMask::GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::TRANSFORM_FEEDBACK:
                                glMask |= MemoryBarrierMask::GL_TRANSFORM_FEEDBACK_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::VERTEX_ATTRIB_ARRAY:
                                glMask |= MemoryBarrierMask::GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::INDEX_ARRAY:
                                glMask |= MemoryBarrierMask::GL_ELEMENT_ARRAY_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::UNIFORM_DATA:
                                glMask |= MemoryBarrierMask::GL_UNIFORM_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::PIXEL_BUFFER:
                                glMask |= MemoryBarrierMask::GL_PIXEL_BUFFER_BARRIER_BIT;
                                break;
                            case MemoryBarrierType::PERSISTENT_BUFFER:
                                glMask |= MemoryBarrierMask::GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT;
                                break;
                            default:
                                NOP();
                                break;
                            }
                        }
                    }
                    glMemoryBarrier(glMask);
               }
            }
        } break;
        default: break;
    }

    if (!s_bufferLockQueueMidFlush.empty()) {
        for (const GFX::CommandBuffer::CommandEntry flushPoint : _bufferFlushPoints) {
            if (entry == flushPoint) {
                FlushMidBufferLockQueue();
                break;
            }
        }
    }
}

void GL_API::RegisterBufferLock(const BufferLockEntry&& data, const ShaderBufferLockType lockType) {
    assert(Runtime::isMainThread());
    if (lockType == ShaderBufferLockType::IMMEDIATE) {
        if (!data._buffer->lockByteRange(data._offset, data._length, GFXDevice::FrameCount())) {
            NOP();
        }
    } else if (lockType == ShaderBufferLockType::AFTER_DRAW_COMMANDS) {
        for (BufferLockEntry& lockEntry : s_bufferLockQueueMidFlush) {
            if (lockEntry._buffer->getGUID() == data._buffer->getGUID()) {
                lockEntry._offset = std::min(lockEntry._offset, data._offset);
                lockEntry._length = std::max(lockEntry._length, data._length);
                return;
            }
        }
        s_bufferLockQueueMidFlush.push_back(data);
    } else /* if (lockType == ShaderBufferLockType::LockType::AFTER_COMMAND_BUFFER_FLUSH*/ {
        for (BufferLockEntry& lockEntry : s_bufferLockQueueEndOfBuffer) {
            if (lockEntry._buffer->getGUID() == data._buffer->getGUID()) {
                lockEntry._offset = std::min(lockEntry._offset, data._offset);
                lockEntry._length = std::max(lockEntry._length, data._length);
                return;
            }
        }
        s_bufferLockQueueEndOfBuffer.push_back(data);
    }
}

void GL_API::postFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    OPTICK_EVENT();

    s_IsFlushingCommandBuffer = false;

    FlushMidBufferLockQueue();
    FlushEndBufferLockQueue();

    bool expected = true;
    if (s_glFlushQueued.compare_exchange_strong(expected, false)) {
        OPTICK_EVENT("GL_FLUSH");
        glFlush();
    }
}

GLStateTracker::BindResult GL_API::makeTexturesResidentInternal(TextureDataContainer& textureData, const U8 offset, U8 count) const {
    // All of the complicate and fragile code bellow does actually provide a measurable performance increase 
    // (micro second range for a typical scene, nothing amazing, but still ...)
    // CPU cost is comparable to the multiple glBind calls on some specific driver + GPU combos.

    constexpr GLuint k_textureThreshold = 3;
    GLStateTracker& stateTracker = GetStateTracker();

    const size_t totalTextureCount = textureData.count();

    count = std::min(count, to_U8(totalTextureCount - offset));
    assert(to_size(offset) + count <= totalTextureCount);
    const auto& textures = textureData._entries;

    GLStateTracker::BindResult result = GLStateTracker::BindResult::FAILED;
    if (count > 1) {
        // If we have 3 or more textures, there's a chance we might get a binding gap, so just sort
        if (totalTextureCount > 2) {
            textureData.sortByBinding();
        }

        U8 prevBinding = textures.front()._binding;
        const TextureType targetType = textures.front()._data._textureType;

        U8 matchingTexCount = 0u;
        U8 startBinding = U8_MAX;
        U8 endBinding = 0u; 

        for (U8 idx = offset; idx < offset + count; ++idx) {
            const TextureEntry& entry = textures[idx];
            assert(IsValid(entry._data));
            if (entry._binding != INVALID_TEXTURE_BINDING && targetType != entry._data._textureType) {
                break;
            }
            // Avoid large gaps between bindings. It's faster to just bind them individually.
            if (matchingTexCount > 0 && entry._binding - prevBinding > k_textureThreshold) {
                break;
            }
            // We mainly want to handle ONLY consecutive units
            prevBinding = entry._binding;
            startBinding = std::min(startBinding, entry._binding);
            endBinding = std::max(endBinding, entry._binding);
            ++matchingTexCount;
        }

        if (matchingTexCount >= k_textureThreshold) {
            static vector<GLuint> handles{};
            static vector<GLuint> samplers{};
            static bool init = false;
            if (!init) {
                init = true;
                handles.resize(GFXDevice::GetDeviceInformation()._maxTextureUnits, GLUtil::k_invalidObjectID);
                samplers.resize(GFXDevice::GetDeviceInformation()._maxTextureUnits, GLUtil::k_invalidObjectID);
            } else {
                std::memset(&handles[startBinding], GLUtil::k_invalidObjectID, (to_size(endBinding - startBinding) + 1) * sizeof(GLuint));
            }

            for (U8 idx = offset; idx < offset + matchingTexCount; ++idx) {
                const TextureEntry& entry = textures[idx];
                if (entry._binding != INVALID_TEXTURE_BINDING) {
                    handles[entry._binding]  = entry._data._textureHandle;
                    samplers[entry._binding] = GetSamplerHandle(entry._sampler);
                }
            }

            
            for (U8 binding = startBinding; binding < endBinding; ++binding) {
                if (handles[binding] == GLUtil::k_invalidObjectID) {
                    const TextureType crtType = stateTracker.getBoundTextureType(binding);
                    samplers[binding] = stateTracker.getBoundSamplerHandle(binding);
                    handles[binding] = stateTracker.getBoundTextureHandle(binding, crtType);
                }
            }

            result = stateTracker.bindTextures(startBinding, endBinding - startBinding + 1, targetType, &handles[startBinding], &samplers[startBinding]);
        } else {
            matchingTexCount = 1;
            result = makeTexturesResidentInternal(textureData, offset, 1);
            if (result == GLStateTracker::BindResult::FAILED) {
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        // Recurse to try and get more matches
        result = makeTexturesResidentInternal(textureData, offset + matchingTexCount, count - matchingTexCount);
    } else if (count == 1) {
        // Normal usage. Bind a single texture at a time
        const TextureEntry& entry = textures[offset];
        if (entry._binding != INVALID_TEXTURE_BINDING) {
            assert(IsValid(entry._data));
            const GLuint handle = entry._data._textureHandle;
            const GLuint sampler = GetSamplerHandle(entry._sampler);
            result = stateTracker.bindTextures(entry._binding, 1, entry._data._textureType, &handle, &sampler);
        }
    } else {
        result = GLStateTracker::BindResult::ALREADY_BOUND;
    }

    return result;
}

GLStateTracker::BindResult GL_API::makeTextureViewsResidentInternal(const TextureViews& textureViews, const U8 offset, U8 count) const {
    count = std::min(count, to_U8(textureViews.count()));

    GLStateTracker::BindResult result = GLStateTracker::BindResult::FAILED;
    for (U8 i = offset; i < count + offset; ++i) {
        const auto& it = textureViews._entries[i];
        const size_t viewHash = it.getHash();
        TextureView view = it._view;
        if (view._targetType == TextureType::COUNT) {
            view._targetType = view._textureData._textureType;
        }
        const TextureData& data = view._textureData;
        assert(IsValid(data));

        auto [textureID, cacheHit] = s_textureViewCache.allocate(viewHash);
        DIVIDE_ASSERT(textureID != 0u);

        if (!cacheHit)
        {
            const GLenum glInternalFormat = GLUtil::internalFormat(it._descriptor.baseFormat(), it._descriptor.dataType(), it._descriptor.srgb(), it._descriptor.normalized());

            if (IsCubeTexture(view._targetType)) {
                view._layerRange *= 6;
            }

            glTextureView(textureID,
                GLUtil::glTextureTypeTable[to_base(view._targetType)],
                data._textureHandle,
                glInternalFormat,
                static_cast<GLuint>(it._view._mipLevels.x),
                static_cast<GLuint>(it._view._mipLevels.y),
                view._layerRange.min,
                view._layerRange.max);
        }

        const GLuint samplerHandle = GetSamplerHandle(it._view._samplerHash);
        result = GL_API::GetStateTracker().bindTextures(static_cast<GLushort>(it._binding), 1, view._targetType, &textureID, &samplerHandle);
        if (result == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
        // Self delete after 3 frames unless we use it again
        s_textureViewCache.deallocate(textureID, 3u);
    }

    return result;
}

bool GL_API::MakeTexturesResidentInternal(const SamplerAddress address) {
    if (!ShaderProgram::s_UseBindlessTextures) {
        return true;
    }

    if (address > 0u) {
        bool valid = false;
        // Check for existing resident textures
        for (ResidentTexture& texture : s_residentTextures) {
            if (texture._address == address) {
                texture._frameCount = 0u;
                valid = true;
                break;
            }
        }

        if (!valid) {
            // Register a new resident texture
            for (ResidentTexture& texture : s_residentTextures) {
                if (texture._address == 0u) {
                    texture._address = address;
                    texture._frameCount = 0u;
                    glMakeTextureHandleResidentARB(address);
                    valid = true;
                    break;
                }
            }
        }

        return valid;
    }

    return true;
}

bool GL_API::MakeTexturesNonResidentInternal(const SamplerAddress address) {
    if (!ShaderProgram::s_UseBindlessTextures) {
        return true;
    }

    if (address > 0u) {
        for (ResidentTexture& texture : s_residentTextures) {
            if (texture._address == address) {
                texture._address = 0u;
                texture._frameCount = 0u;
                glMakeTextureHandleNonResidentARB(address);
                return true;
            }
        }
        return false;
    }

    return true;
}

bool GL_API::setViewport(const Rect<I32>& viewport) {
    return GetStateTracker().setViewport(viewport);
}

/// Return the OpenGL sampler object's handle for the given hash value
GLuint GL_API::GetSamplerHandle(const size_t samplerHash) {
    // If the hash value is 0, we assume the code is trying to unbind a sampler object
    if (samplerHash > 0) {
        {
            SharedLock<SharedMutex> r_lock(s_samplerMapLock);
            // If we fail to find the sampler object for the given hash, we print an error and return the default OpenGL handle
            const SamplerObjectMap::const_iterator it = s_samplerMap.find(samplerHash);
            if (it != std::cend(s_samplerMap)) {
                // Return the OpenGL handle for the sampler object matching the specified hash value
                return it->second;
            }
        }
        {

            ScopedLock<SharedMutex> w_lock(s_samplerMapLock);
            // Check again
            const SamplerObjectMap::const_iterator it = s_samplerMap.find(samplerHash);
            if (it == std::cend(s_samplerMap)) {
                // Cache miss. Create the sampler object now.
                // Create and store the newly created sample object. GL_API is responsible for deleting these!
                const GLuint sampler = glSamplerObject::construct(SamplerDescriptor::get(samplerHash));
                emplace(s_samplerMap, samplerHash, sampler);
                return sampler;
            }
        }
    }

    return 0u;
}

U32 GL_API::getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const {
    return to_U32(static_cast<const CEGUI::OpenGLTexture&>(textureIn).getOpenGLTexture());
}

IMPrimitive* GL_API::NewIMP(Mutex& lock, GFXDevice& parent) {
    ScopedLock<Mutex> w_lock(lock);
    return s_IMPrimitivePool.newElement(parent);
}

bool GL_API::DestroyIMP(Mutex& lock, IMPrimitive*& primitive) {
    if (primitive != nullptr) {
        ScopedLock<Mutex> w_lock(lock);
        s_IMPrimitivePool.deleteElement(static_cast<glIMPrimitive*>(primitive));
        primitive = nullptr;
        return true;
    }

    return false;
}

};
