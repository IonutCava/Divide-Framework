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

namespace Divide {

    class Pipeline;
    class glFramebuffer;
    class glPixelBuffer;
    class RenderStateBlock;
    class glBufferLockManager;

    struct GLStateTracker {
        void init() noexcept;

        /// Enable or disable primitive restart and ensure that the correct index size is used
        void togglePrimitiveRestart(bool state);
        /// Enable or disable primitive rasterization
        void toggleRasterization(bool state);
        /// Switch the currently active vertex array object
        bool setActiveVAO(GLuint ID) noexcept;
        /// Switch the currently active vertex array object
        bool setActiveVAO(GLuint ID, GLuint& previousID) noexcept;
        /// Single place to change buffer objects for every target available
        bool setActiveBuffer(GLenum target, GLuint bufferHandle);
        /// Single place to change buffer objects for every target available
        bool setActiveBuffer(GLenum target, GLuint bufferHandle, GLuint& previousID);

        bool setActiveBufferIndex(GLenum target, GLuint bufferHandle, GLuint bindIndex);
        bool setActiveBufferIndex(GLenum target, GLuint bufferHandle, GLuint bindIndex, GLuint& previousID);
        /// Same as normal setActiveBuffer but handles proper binding of different ranges
        bool setActiveBufferIndexRange(GLenum target, GLuint bufferHandle, GLuint bindIndex, size_t offsetInBytes, size_t rangeInBytes);
        bool setActiveBufferIndexRange(GLenum target, GLuint bufferHandle, GLuint bindIndex, size_t offsetInBytes, size_t rangeInBytes, GLuint& previousID);
        /// Switch the current framebuffer by binding it as either a R/W buffer, read
        /// buffer or write buffer
        bool setActiveFB(RenderTarget::RenderTargetUsage usage, GLuint ID);
        /// Set a new depth range. Default is 0 - 1 with 0 mapping to the near plane and 1 to the far plane
        void setDepthRange(F32 nearVal, F32 farVal);
        // Just a wrapper around glClipControl
        void setClippingPlaneState(bool lowerLeftOrigin, bool negativeOneToOneDepth);
        void setBlending(const BlendingProperties& blendingProperties);
        void resetBlending() { setBlending(_blendPropertiesGlobal); }
        /// Set the blending properties for the specified draw buffer
        void setBlending(GLuint drawBufferIdx, const BlendingProperties& blendingProperties);

        void resetBlending(const GLuint drawBufferIdx) { setBlending(drawBufferIdx, _blendProperties[drawBufferIdx]); }

        void setBlendColour(const UColour4& blendColour);
        /// Switch the current framebuffer by binding it as either a R/W buffer, read
        /// buffer or write buffer
        bool setActiveFB(RenderTarget::RenderTargetUsage usage, GLuint ID, GLuint& previousID);
        /// Change the currently active shader program. Returns false if the program was already bound
        bool setActiveProgram(GLuint programHandle);
        /// Change the currently active shader pipeline. Returns false if the pipeline was already bound
        bool setActiveShaderPipeline(GLuint pipelineHandle);
        /// A state block should contain all rendering state changes needed for the next draw call.
        /// Some may be redundant, so we check each one individually
        void activateStateBlock(const RenderStateBlock& newBlock);
        /// Pixel pack and unpack alignment is usually changed by textures, PBOs, etc
        bool setPixelPackUnpackAlignment(const GLint packAlignment = 4, const GLint unpackAlignment = 4) { return setPixelPackAlignment(packAlignment) && setPixelUnpackAlignment(unpackAlignment); }
        /// Pixel pack alignment is usually changed by textures, PBOs, etc
        bool setPixelPackAlignment(GLint packAlignment = 4, GLint rowLength = 0, GLint skipRows = 0, GLint skipPixels = 0);
        /// Pixel unpack alignment is usually changed by textures, PBOs, etc
        bool setPixelUnpackAlignment(GLint unpackAlignment = 4, GLint rowLength = 0, GLint skipRows = 0, GLint skipPixels = 0);
        /// Bind a texture specified by a GL handle and GL type to the specified unit
        /// using the sampler object defined by handle value
        bool bindTexture(GLushort unit, TextureType type, GLuint handle, GLuint samplerHandle = 0u);
        bool bindTextureImage(GLushort unit, GLuint handle, GLint level, bool layered, GLint layer, GLenum access, GLenum format);
        /// Bind multiple textures specified by an array of handles and an offset unit
        bool bindTextures(GLushort unitOffset, GLuint textureCount, TextureType texturesType, GLuint* textureHandles, GLuint* samplerHandles);
        bool bindTexturesNoMipMap(GLushort unitOffset, GLuint textureCount, TextureType texturesType, GLuint* textureHandles, GLuint* samplerHandles);

        void setStateBlock(size_t stateBlockHash);

        /// Bind multiple samplers described by the array of hash values to the
        /// consecutive texture units starting from the specified offset
        bool bindSamplers(GLushort unitOffset, GLuint samplerCount, GLuint* samplerHandles);

        /// Modify buffer bindings for a specific vao
        bool bindActiveBuffer(GLuint vaoID, GLuint location, GLuint bufferID, GLuint instanceDivisor, size_t offset, size_t stride);

        bool setScissor(const Rect<I32>& newScissorRect);
        bool setScissor(const I32 x, const I32 y, const I32 width, const I32 height) { return setScissor({ x, y, width, height }); }

        /// Change the current viewport area. Redundancy check is performed in GFXDevice class
        bool setViewport(const Rect<I32>& viewport);
        bool setViewport(const I32 x, const I32 y, const I32 width, const I32 height) { return setViewport({ x, y, width, height }); }

        bool setClearColour(const FColour4& colour);
        bool setClearColour(const UColour4& colour) { return setClearColour(Util::ToFloatColour(colour)); }

        [[nodiscard]] GLuint getBoundTextureHandle(U8 slot, TextureType type) const;
        [[nodiscard]] GLuint getBoundSamplerHandle(U8 slot) const;
        [[nodiscard]] GLuint getBoundProgramHandle() const;
        [[nodiscard]] GLuint getBoundBuffer(GLenum target, GLuint bindIndex) const;
        [[nodiscard]] GLuint getBoundBuffer(GLenum target, GLuint bindIndex, size_t& offsetOut, size_t& rangeOut) const;
        [[nodiscard]] TextureType getBoundTextureType(U8 slot) const;

        void getActiveViewport(GLint* vp) const;

        static void ProcessMipMapQueue(GLuint textureCount, const GLuint* textureHandles);

      public:
          struct BindConfigEntry
          {
              U32 _handle = 0;
              size_t _offset = 0;
              size_t _range = 0;
          };

        RenderStateBlock _activeState{};

        Str64 _debugScope;
        bool _flushingCommandBuffer = false;
        Pipeline const* _activePipeline = nullptr;
        glFramebuffer*  _activeRenderTarget = nullptr;
        glPixelBuffer*  _activePixelBuffer = nullptr;
        /// Current active vertex array object's handle
        GLuint _activeVAOID = GLUtil::k_invalidObjectID;
        /// 0 - current framebuffer, 1 - current read only framebuffer, 2 - current write only framebuffer
        GLuint _activeFBID[3] = { GLUtil::k_invalidObjectID,
                                  GLUtil::k_invalidObjectID,
                                  GLUtil::k_invalidObjectID };
        /// VB, IB, SB, TB, UB, PUB, DIB
        GLuint _activeBufferID[6] = { GLUtil::k_invalidObjectID,
                                      GLUtil::k_invalidObjectID,
                                      GLUtil::k_invalidObjectID,
                                      GLUtil::k_invalidObjectID,
                                      GLUtil::k_invalidObjectID,
                                      GLUtil::k_invalidObjectID };
        hashMap<GLuint, GLuint> _activeVAOIB;

        GLint  _activePackUnpackAlignments[2] = { 1 , 1 };
        GLint  _activePackUnpackRowLength[2] = { 0 , 0 };
        GLint  _activePackUnpackSkipPixels[2] = { 0 , 0 };
        GLint  _activePackUnpackSkipRows[2] = {0 , 0};
        GLuint _activeShaderProgram = 0; //GLUtil::_invalidObjectID;
        GLuint _activeShaderPipeline = 0;//GLUtil::_invalidObjectID;
        GLfloat _depthNearVal = -1.f;
        GLfloat _depthFarVal = -1.f;
        bool _lowerLeftOrigin = true;
        bool _negativeOneToOneDepth = true;
        BlendingProperties _blendPropertiesGlobal = { BlendProperty::ONE,
                                                      BlendProperty::ONE,
                                                      BlendOperation::ADD };
        GLboolean _blendEnabledGlobal = GL_FALSE;

        using BindConfig = hashMap<U32 /*bindIndex*/, BindConfigEntry>;
        using PerBufferConfig = hashMap<GLenum /*target*/, BindConfig>;
        PerBufferConfig g_currentBindConfig;

        vector<BlendingProperties> _blendProperties;
        vector<GLboolean> _blendEnabled;
        GLenum    _currentCullMode = GL_BACK;
        GLenum    _currentFrontFace = GL_CCW;
        UColour4  _blendColour = UColour4(0, 0, 0, 0);
        Rect<I32> _activeViewport = Rect<I32>(-1);
        Rect<I32> _activeScissor = Rect<I32>(-1);
        FColour4  _activeClearColour = DefaultColours::BLACK_U8;

        /// Boolean value used to verify if primitive restart index is enabled or disabled
        bool _primitiveRestartEnabled = false;
        bool _rasterizationEnabled = true;

        /// /*hash: texture slot  - array /*texture handle - texture type*/ hash
        using TextureBoundMapDef = std::array<vector<GLuint>, to_base(TextureType::COUNT)>;
        TextureBoundMapDef _textureBoundMap = {};

        using ImageBoundMapDef = vector<ImageBindSettings>;
        ImageBoundMapDef _imageBoundMap = {};

        /// /*texture slot*/ /*sampler handle*/
        using SamplerBoundMapDef = vector<GLuint>;
        SamplerBoundMapDef _samplerBoundMap = {};

        using TextureTypeBoundMapDef = vector<TextureType>;
        TextureTypeBoundMapDef _textureTypeBoundMap = {};

        VAOBindings _vaoBufferData;
        bool _opengl46Supported = false;
    }; //class GLStateTracker
}; //namespace Divide


#endif //_GL_STATE_TRACKER_H_