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
#ifndef DVD_GL_STATE_TRACKER_H_
#define DVD_GL_STATE_TRACKER_H_

#include "glResources.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

namespace Divide {

    class Pipeline;
    class glFramebuffer;
    class glShaderProgram;

    struct GLStateTracker {
        static constexpr U8 MAX_BOUND_TEXTURE_UNITS = 32u;

        using AttributeSettings = std::array<AttributeDescriptor, to_base(AttribLocation::COUNT)>;

        enum class BindResult : U8 {
            JUST_BOUND = 0,
            ALREADY_BOUND,
            FAILED,
            COUNT
        };

        void setDefaultState();

        void setBlending(const BlendingSettings& blendingProperties);
        void resetBlending() { setBlending(_blendPropertiesGlobal); setBlendColour({ 0u, 0u, 0u, 0u }); }
        /// Set the blending properties for the specified draw buffer
        void setBlending( gl46core::GLuint drawBufferIdx, const BlendingSettings& blendingProperties);
        void resetBlending(const gl46core::GLuint drawBufferIdx) { setBlending(drawBufferIdx, _blendProperties[drawBufferIdx]); }
        void setBlendColour(const UColour4& blendColour);
        /// A state block should contain all rendering state changes needed for the next draw call.
        /// Some may be redundant, so we check each one individually
        bool activateStateBlock(const RenderStateBlock& newBlock);

        void setPrimitiveTopology( PrimitiveTopology topology );
        void setVertexFormat(const AttributeMap& attributes, const size_t attributeHash);

        /// Single place to change buffer objects for every target available
        [[nodiscard]] BindResult setActiveBuffer( gl46core::GLenum target, gl46core::GLuint bufferHandle);
        /// Single place to change buffer objects for every target available
        [[nodiscard]] BindResult setActiveBuffer( gl46core::GLenum target, gl46core::GLuint bufferHandle, gl46core::GLuint& previousID);

        [[nodiscard]] BindResult setActiveBufferIndex( gl46core::GLenum target, gl46core::GLuint bufferHandle, gl46core::GLuint bindIndex );
        [[nodiscard]] BindResult setActiveBufferIndex( gl46core::GLenum target, gl46core::GLuint bufferHandle, gl46core::GLuint bindIndex, gl46core::GLuint& previousID);
        /// Same as normal setActiveBuffer but handles proper binding of different ranges
        [[nodiscard]] BindResult setActiveBufferIndexRange( gl46core::GLenum target, gl46core::GLuint bufferHandle, gl46core::GLuint bindIndex, size_t offsetInBytes, size_t rangeInBytes );
        [[nodiscard]] BindResult setActiveBufferIndexRange( gl46core::GLenum target, gl46core::GLuint bufferHandle, gl46core::GLuint bindIndex, size_t offsetInBytes, size_t rangeInBytes, gl46core::GLuint& previousID);
        /// Switch the current framebuffer by binding it as either a R/W buffer, read
        /// buffer or write buffer
        [[nodiscard]] BindResult setActiveFB(RenderTarget::Usage usage, gl46core::GLuint ID);
        /// Switch the current framebuffer by binding it as either a R/W buffer, read
        /// buffer or write buffer
        [[nodiscard]] BindResult setActiveFB(RenderTarget::Usage usage, gl46core::GLuint ID, gl46core::GLuint& previousID);
        /// Change the currently active shader program. Returns false if the program was already bound
        [[nodiscard]] BindResult setActiveProgram( gl46core::GLuint programHandle);
        /// Change the currently active shader pipeline. Returns false if the pipeline was already bound
        [[nodiscard]] BindResult setActiveShaderPipeline( gl46core::GLuint pipelineHandle);
        /// Returns true if the texture was bound. If the texture was not bound, no state is changed.
        [[nodiscard]] bool      unbindTexture(TextureType type, gl46core::GLuint handle);
        [[nodiscard]] bool      unbindTextures();
        /// Bind a texture specified by a GL handle and GL type to the specified unit
        /// using the sampler object defined by handle value
        [[nodiscard]] BindResult bindTexture( gl46core::GLubyte unit, gl46core::GLuint handle, gl46core::GLuint samplerHandle = 0u);
        [[nodiscard]] BindResult bindTextureImage( gl46core::GLubyte unit, gl46core::GLuint handle, gl46core::GLint level, bool layered, gl46core::GLint layer, gl46core::GLenum access, gl46core::GLenum format);
        /// Bind multiple textures specified by an array of handles and an offset unit
        [[nodiscard]] BindResult bindTextures( gl46core::GLubyte unitOffset, gl46core::GLuint textureCount, const gl46core::GLuint* textureHandles, const gl46core::GLuint* samplerHandles);
        [[nodiscard]] BindResult setStateBlock(const RenderStateBlock& stateBlock);
        /// Bind multiple samplers described by the array of hash values to the
        /// consecutive texture units starting from the specified offset
        [[nodiscard]] BindResult bindSamplers( gl46core::GLubyte unitOffset, gl46core::GLuint samplerCount, const gl46core::GLuint* samplerHandles);
        /// Modify buffer bindings for the active vao
        [[nodiscard]] BindResult bindActiveBuffer( gl46core::GLuint location, gl46core::GLuint bufferID, size_t offset, size_t stride);
        [[nodiscard]] BindResult bindActiveBuffers( gl46core::GLuint location, gl46core::GLsizei count, gl46core::GLuint* bufferIDs, gl46core::GLintptr* offset, gl46core::GLsizei* strides);

        /// Pixel pack alignment is usually changed by textures, PBOs, etc
        bool setPixelPackAlignment( const PixelAlignment& pixelPackAlignment );
        /// Pixel unpack alignment is usually changed by textures, PBOs, etc
        bool setPixelUnpackAlignment(  const PixelAlignment& pixelUnpackAlignment );
        bool setScissor(const Rect<I32>& newScissorRect);
        bool setScissor(const I32 x, const I32 y, const I32 width, const I32 height) { return setScissor({ x, y, width, height }); }
        /// Change the current viewport area. Redundancy check is performed in GFXDevice class
        bool setViewport(const Rect<I32>& viewport);
        bool setViewport(const I32 x, const I32 y, const I32 width, const I32 height) { return setViewport({ x, y, width, height }); }
        bool setClearColour(const FColour4& colour);
        bool setClearColour(const UColour4& colour) { return setClearColour(Util::ToFloatColour(colour)); }
        bool setClearDepth(F32 value);

        inline const RenderStateBlock& getActiveState() const { return _activeState; }

        bool setAlphaToCoverage(bool state);
        bool setDepthWrite(bool state);

        [[nodiscard]] gl46core::GLuint getBoundTextureHandle(U8 slot) const noexcept;
        [[nodiscard]] gl46core::GLuint getBoundSamplerHandle(U8 slot) const noexcept;
        [[nodiscard]] gl46core::GLuint getBoundProgramHandle() const noexcept;
        [[nodiscard]] gl46core::GLuint getBoundBuffer( gl46core::GLenum target, gl46core::GLuint bindIndex) const noexcept;
        [[nodiscard]] gl46core::GLuint getBoundBuffer( gl46core::GLenum target, gl46core::GLuint bindIndex, size_t& offsetOut, size_t& rangeOut) const noexcept;

        void getActiveViewport(Rect<I32>& viewportOut) const noexcept;

        bool* _enabledAPIDebugging{nullptr};
        bool* _assertOnAPIError{nullptr};

      public:
          struct BindConfigEntry
          {
              U32 _handle{ 0u };
              size_t _offset{ 0u };
              size_t _range{ 0u };
          };

        RenderStateBlock _activeState{};

        AttributeSettings _currentAttributes;


        Pipeline const* _activePipeline{ nullptr };
        glShaderProgram* _activeShaderProgram{ nullptr };

        PrimitiveTopology _activeTopology{ PrimitiveTopology::COUNT };
        glFramebuffer* _activeRenderTarget{ nullptr };
        RenderTargetID _activeRenderTargetID{ INVALID_RENDER_TARGET_ID };
        vec2<U16> _activeRenderTargetDimensions{1u};
        /// 0 - current framebuffer, 1 - current read only framebuffer, 2 - current write only framebuffer
        gl46core::GLuint _activeFBID[3] { GL_NULL_HANDLE,
                                          GL_NULL_HANDLE,
                                          GL_NULL_HANDLE };
        /// VB, IB, SB, TB, UB, PUB, DIB
        std::array<gl46core::GLuint, 13> _activeBufferID = create_array<13, gl46core::GLuint>(GL_NULL_HANDLE);
        gl46core::GLuint _activeVAOIB{GL_NULL_HANDLE};
        size_t _drawIndirectBufferOffset{0u};

        PixelAlignment _packAlignment{};
        PixelAlignment _unpackAlignment{};

        gl46core::GLuint _activeShaderProgramHandle{ 0u }; //GLUtil::_invalidObjectID;
        gl46core::GLuint _activeShaderPipelineHandle{ 0u };//GLUtil::_invalidObjectID;
        bool _alphaToCoverageEnabled{ false };
        BlendingSettings _blendPropertiesGlobal;
        gl46core::GLboolean _blendEnabledGlobal{ gl46core::GL_FALSE };

        // 32 buffer bindings for now
        using BindConfig = std::array<BindConfigEntry, 32>;
        using PerBufferConfig = std::array<BindConfig, 14>;
        PerBufferConfig _currentBindConfig;

        vector<BlendingSettings> _blendProperties;
        vector<gl46core::GLboolean> _blendEnabled;
        UColour4  _blendColour{ 0, 0, 0, 0 };
        Rect<I32> _activeViewport{ -1, -1, -1, -1 };
        Rect<I32> _activeScissor{ -1, -1, -1, -1 };
        FColour4  _activeClearColour{ DefaultColours::BLACK_U8 };
        F32       _clearDepthValue{ 1.f };

        using TextureBoundMapDef = std::array<gl46core::GLuint, MAX_BOUND_TEXTURE_UNITS>;
        TextureBoundMapDef _textureBoundMap;

        using ImageBoundMapDef = vector<ImageBindSettings>;
        ImageBoundMapDef _imageBoundMap;

        using SamplerBoundMapDef = std::array<gl46core::GLuint, MAX_BOUND_TEXTURE_UNITS>;
        SamplerBoundMapDef _samplerBoundMap;

        VAOBindings _vaoBufferData;
        eastl::queue<std::pair<gl46core::GLsync, U64>> _endFrameFences;
        U64 _lastSyncedFrameNumber{ 0u };

        size_t _attributeHash{ 0u };

    }; //struct GLStateTracker

    FWD_DECLARE_MANAGED_STRUCT(GLStateTracker);
}; //namespace Divide


#endif //DVD_GL_STATE_TRACKER_H_
