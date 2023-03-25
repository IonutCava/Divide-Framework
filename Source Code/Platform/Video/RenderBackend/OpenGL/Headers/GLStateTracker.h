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
#ifndef _GL_STATE_TRACKER_H_
#define _GL_STATE_TRACKER_H_

#include "glResources.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Headers/DescriptorSets.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

namespace Divide {

    class Pipeline;
    class glFramebuffer;
    class glShaderProgram;
    class RenderStateBlock;

    struct GLStateTracker {
        static constexpr U8 MAX_BOUND_TEXTURE_UNITS = 32u;

        using AttribHashes = std::array<size_t, to_base(AttribLocation::COUNT)>;

        enum class BindResult : U8 {
            JUST_BOUND = 0,
            ALREADY_BOUND,
            FAILED,
            COUNT
        };

        void setDefaultState();

        /// Enable or disable primitive restart and ensure that the correct index size is used
        void togglePrimitiveRestart(bool state);
        /// Enable or disable primitive rasterization
        void toggleRasterization(bool state);
        /// Set a new depth range. Default is 0 - 1 with 0 mapping to the near plane and 1 to the far plane
        void setDepthRange(F32 nearVal, F32 farVal);
        // Just a wrapper around glClipControl
        void setClippingPlaneState(bool lowerLeftOrigin, bool negativeOneToOneDepth);
        void setBlending(const BlendingSettings& blendingProperties);
        void resetBlending() { setBlending(_blendPropertiesGlobal); setBlendColour({ 0u, 0u, 0u, 0u }); }
        /// Set the blending properties for the specified draw buffer
        void setBlending(GLuint drawBufferIdx, const BlendingSettings& blendingProperties);
        void resetBlending(const GLuint drawBufferIdx) { setBlending(drawBufferIdx, _blendProperties[drawBufferIdx]); }
        void setBlendColour(const UColour4& blendColour);
        /// A state block should contain all rendering state changes needed for the next draw call.
        /// Some may be redundant, so we check each one individually
        void activateStateBlock(const RenderStateBlock& newBlock);

        void setPrimitiveTopology( PrimitiveTopology topology );
        void setVertexFormat(bool primitiveRestartEnabled, const AttributeMap& attributes, const size_t attributeHash);

        /// Switch the currently active vertex array object
        [[nodiscard]] BindResult setActiveVAO(GLuint ID);
        /// Switch the currently active vertex array object
        [[nodiscard]] BindResult setActiveVAO(GLuint ID, GLuint& previousID);
        /// Single place to change buffer objects for every target available
        [[nodiscard]] BindResult setActiveBuffer(GLenum target, GLuint bufferHandle);
        /// Single place to change buffer objects for every target available
        [[nodiscard]] BindResult setActiveBuffer(GLenum target, GLuint bufferHandle, GLuint& previousID);

        [[nodiscard]] BindResult setActiveBufferIndex(GLenum target, GLuint bufferHandle, GLuint bindIndex);
        [[nodiscard]] BindResult setActiveBufferIndex(GLenum target, GLuint bufferHandle, GLuint bindIndex, GLuint& previousID);
        /// Same as normal setActiveBuffer but handles proper binding of different ranges
        [[nodiscard]] BindResult setActiveBufferIndexRange(GLenum target, GLuint bufferHandle, GLuint bindIndex, size_t offsetInBytes, size_t rangeInBytes);
        [[nodiscard]] BindResult setActiveBufferIndexRange(GLenum target, GLuint bufferHandle, GLuint bindIndex, size_t offsetInBytes, size_t rangeInBytes, GLuint& previousID);
        /// Switch the current framebuffer by binding it as either a R/W buffer, read
        /// buffer or write buffer
        [[nodiscard]] BindResult setActiveFB(RenderTarget::Usage usage, GLuint ID);
        /// Switch the current framebuffer by binding it as either a R/W buffer, read
        /// buffer or write buffer
        [[nodiscard]] BindResult setActiveFB(RenderTarget::Usage usage, GLuint ID, GLuint& previousID);
        /// Change the currently active shader program. Returns false if the program was already bound
        [[nodiscard]] BindResult setActiveProgram(GLuint programHandle);
        /// Change the currently active shader pipeline. Returns false if the pipeline was already bound
        [[nodiscard]] BindResult setActiveShaderPipeline(GLuint pipelineHandle);
        /// Returns true if the texture was bound. If the texture was not bound, no state is changed.
        [[nodiscard]] bool      unbindTexture(TextureType type, GLuint handle);
        [[nodiscard]] bool      unbindTextures();
        /// Bind a texture specified by a GL handle and GL type to the specified unit
        /// using the sampler object defined by handle value
        [[nodiscard]] BindResult bindTexture(GLubyte unit, GLuint handle, GLuint samplerHandle = 0u);
        [[nodiscard]] BindResult bindTextureImage(GLubyte unit, GLuint handle, GLint level, bool layered, GLint layer, GLenum access, GLenum format);
        /// Bind multiple textures specified by an array of handles and an offset unit
        [[nodiscard]] BindResult bindTextures(GLubyte unitOffset, GLuint textureCount, const GLuint* textureHandles, const GLuint* samplerHandles);
        [[nodiscard]] BindResult setStateBlock(size_t stateBlockHash);
        /// Bind multiple samplers described by the array of hash values to the
        /// consecutive texture units starting from the specified offset
        [[nodiscard]] BindResult bindSamplers(GLubyte unitOffset, GLuint samplerCount, const GLuint* samplerHandles);
        /// Modify buffer bindings for the active vao
        [[nodiscard]] BindResult bindActiveBuffer(GLuint location, GLuint bufferID, size_t offset, size_t stride);
        [[nodiscard]] BindResult bindActiveBuffers(GLuint location, GLsizei count, GLuint* bufferIDs, GLintptr* offset, GLsizei* strides);

        /// Pixel pack and unpack alignment is usually changed by textures, PBOs, etc
        bool setPixelPackUnpackAlignment(const GLint packAlignment = 4, const GLint unpackAlignment = 4) { return setPixelPackAlignment(packAlignment) && setPixelUnpackAlignment(unpackAlignment); }
        /// Pixel pack alignment is usually changed by textures, PBOs, etc
        bool setPixelPackAlignment(GLint packAlignment = 4, GLint rowLength = 0, GLint skipRows = 0, GLint skipPixels = 0);
        /// Pixel unpack alignment is usually changed by textures, PBOs, etc
        bool setPixelUnpackAlignment(GLint unpackAlignment = 4, GLint rowLength = 0, GLint skipRows = 0, GLint skipPixels = 0);
        bool setScissor(const Rect<I32>& newScissorRect);
        bool setScissor(const I32 x, const I32 y, const I32 width, const I32 height) { return setScissor({ x, y, width, height }); }
        /// Change the current viewport area. Redundancy check is performed in GFXDevice class
        bool setViewport(const Rect<I32>& viewport);
        bool setViewport(const I32 x, const I32 y, const I32 width, const I32 height) { return setViewport({ x, y, width, height }); }
        bool setClearColour(const FColour4& colour);
        bool setClearColour(const UColour4& colour) { return setClearColour(Util::ToFloatColour(colour)); }
        bool setClearDepth(F32 value);

        bool setAlphaToCoverage(bool state);

        [[nodiscard]] GLuint getBoundTextureHandle(U8 slot) const noexcept;
        [[nodiscard]] GLuint getBoundSamplerHandle(U8 slot) const noexcept;
        [[nodiscard]] GLuint getBoundProgramHandle() const noexcept;
        [[nodiscard]] GLuint getBoundBuffer(GLenum target, GLuint bindIndex) const noexcept;
        [[nodiscard]] GLuint getBoundBuffer(GLenum target, GLuint bindIndex, size_t& offsetOut, size_t& rangeOut) const noexcept;

        void getActiveViewport(GLint* vp) const noexcept;

        PROPERTY_RW(bool, assertOnAPIError, false);

      private:
        void setAttributesInternal(GLuint vaoID, const AttributeMap& attributes);
        bool getOrCreateVAO(const size_t attributeHash, GLuint& vaoOut);

      public:
          struct BindConfigEntry
          {
              U32 _handle{ 0u };
              size_t _offset{ 0u };
              size_t _range{ 0u };
          };

        RenderStateBlock _activeState{};

        std::array<std::pair<Str256, U32>, 32> _debugScope;
        U8 _debugScopeDepth{ 0u };

        Pipeline const* _activePipeline{ nullptr };
        glShaderProgram* _activeShaderProgram{ nullptr };

        PrimitiveTopology _activeTopology{ PrimitiveTopology::COUNT };
        glFramebuffer* _activeRenderTarget{ nullptr };
        RenderTargetID _activeRenderTargetID{ INVALID_RENDER_TARGET_ID };
        vec2<U16> _activeRenderTargetDimensions{1u};
        /// Current active vertex array object's handle
        GLuint _activeVAOID{ GLUtil::k_invalidObjectID };
        /// 0 - current framebuffer, 1 - current read only framebuffer, 2 - current write only framebuffer
        GLuint _activeFBID[3] { GLUtil::k_invalidObjectID,
                                GLUtil::k_invalidObjectID,
                                GLUtil::k_invalidObjectID };
        /// VB, IB, SB, TB, UB, PUB, DIB
        std::array<GLuint, 13> _activeBufferID = create_array<13, GLuint>(GLUtil::k_invalidObjectID);
        hashMap<GLuint, GLuint> _activeVAOIB;
        size_t _drawIndirectBufferOffset{0u};

        GLint  _activePackUnpackAlignments[2] { 1 , 1 };
        GLint  _activePackUnpackRowLength[2]  { 0 , 0 };
        GLint  _activePackUnpackSkipPixels[2] { 0 , 0 };
        GLint  _activePackUnpackSkipRows[2]   { 0 , 0 };
        GLuint _activeShaderProgramHandle{ 0u }; //GLUtil::_invalidObjectID;
        GLuint _activeShaderPipelineHandle{ 0u };//GLUtil::_invalidObjectID;
        GLfloat _depthNearVal{ -1.f };
        GLfloat _depthFarVal{ -1.f };
        bool _lowerLeftOrigin{ true };
        bool _negativeOneToOneDepth{ true };
        bool _alphatoCoverageEnabled{ false };
        BlendingSettings _blendPropertiesGlobal;
        GLboolean _blendEnabledGlobal{ GL_FALSE };

        // 32 buffer bindings for now
        using BindConfig = std::array<BindConfigEntry, 32>;
        using PerBufferConfig = std::array<BindConfig, 14>;
        PerBufferConfig _currentBindConfig;

        vector<BlendingSettings> _blendProperties;
        vector<GLboolean> _blendEnabled;
        UColour4  _blendColour{ 0, 0, 0, 0 };
        Rect<I32> _activeViewport{ -1, -1, -1, -1 };
        Rect<I32> _activeScissor{ -1, -1, -1, -1 };
        FColour4  _activeClearColour{ DefaultColours::BLACK_U8 };
        F32       _clearDepthValue{ 1.f };
        /// Boolean value used to verify if primitive restart index is enabled or disabled
        bool _primitiveRestartEnabled{ false };
        bool _rasterizationEnabled{ true };

        using TextureBoundMapDef = std::array<GLuint, MAX_BOUND_TEXTURE_UNITS>;
        TextureBoundMapDef _textureBoundMap;

        using ImageBoundMapDef = vector<ImageBindSettings>;
        ImageBoundMapDef _imageBoundMap;

        using SamplerBoundMapDef = std::array<GLuint, MAX_BOUND_TEXTURE_UNITS>;
        SamplerBoundMapDef _samplerBoundMap;

        VAOBindings _vaoBufferData;
        eastl::queue<std::pair<GLsync, U64>> _endFrameFences;
        U64 _lastSyncedFrameNumber{ 0u };

    }; //struct GLStateTracker

    FWD_DECLARE_MANAGED_STRUCT(GLStateTracker);
}; //namespace Divide


#endif //_GL_STATE_TRACKER_H_