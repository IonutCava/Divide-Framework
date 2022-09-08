#include "stdafx.h"

#include "config.h"

#include "Headers/glFramebuffer.h"

#include "Core/Headers/StringHelper.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

namespace {
    FORCE_INLINE bool IsValid(const DepthBlitEntry& entry) noexcept {
        return entry._inputLayer != INVALID_DEPTH_LAYER &&
               entry._outputLayer != INVALID_DEPTH_LAYER;
    }
    FORCE_INLINE U32 ColorAttachmentToIndex(const GLenum colorAttachmentEnum) noexcept {
        switch (colorAttachmentEnum) {
            case GL_DEPTH_ATTACHMENT  : return GFXDevice::GetDeviceInformation()._maxRTColourAttachments + 1u;
            case GL_STENCIL_ATTACHMENT: return GFXDevice::GetDeviceInformation()._maxRTColourAttachments + 2u;
            default: { //GL_COLOR_ATTACHMENTn
                constexpr U32 offset = to_U32(GL_COLOR_ATTACHMENT0);
                const U32 enumValue = to_U32(colorAttachmentEnum);
                if (enumValue >= offset) {
                    const U32 diff = enumValue - offset;
                    assert(diff < GFXDevice::GetDeviceInformation()._maxRTColourAttachments);
                    return diff;
                }
            } break;
        };

        DIVIDE_UNEXPECTED_CALL();
        return U32_MAX;
    }
};

bool operator==(const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs) noexcept {
    return lhs._attState == rhs._attState &&
        lhs._writeLevel == rhs._writeLevel &&
        lhs._writeLayer == rhs._writeLayer &&
        lhs._layeredRendering == rhs._layeredRendering;
}

bool operator!=(const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs) noexcept {
    return lhs._attState != rhs._attState ||
        lhs._writeLevel != rhs._writeLevel ||
        lhs._writeLayer != rhs._writeLayer ||
        lhs._layeredRendering != rhs._layeredRendering;
}

glFramebuffer::glFramebuffer(GFXDevice& context, const RenderTargetDescriptor& descriptor)
    : RenderTarget(context, descriptor),
      _activeColourBuffers{},
      _prevViewport(-1),
      _debugMessage("Render Target: [ " + name() + " ]")
{
    glCreateFramebuffers(1, &_framebufferHandle);
    assert((_framebufferHandle != 0 && _framebufferHandle != GLUtil::k_invalidObjectID) &&
           "glFramebuffer error: Tried to bind an invalid framebuffer!");

    _isLayeredDepth = false;

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        // label this FB to be able to tell that it's internally created and nor from a 3rd party lib
        glObjectLabel(GL_FRAMEBUFFER,
                      _framebufferHandle,
                      -1,
                      name().empty() ? Util::StringFormat("DVD_FB_%d", _framebufferHandle).c_str() : name().c_str());
    }

    // Everything disabled so that the initial "begin" will override this
    DisableAll(_previousPolicy._drawMask);
    _activeColourBuffers.fill(GL_NONE);
    _attachmentState.resize(GFXDevice::GetDeviceInformation()._maxRTColourAttachments + 2u); //colours + depth + stencil
}

glFramebuffer::~glFramebuffer()
{
    GL_API::DeleteFramebuffers(1, &_framebufferHandle);
}

bool glFramebuffer::initAttachment(RTAttachment* att, const RTAttachmentType type, const U8 index, const bool isExternal) {
    if (RenderTarget::initAttachment(att, type, index, isExternal)) {
        if (!isExternal && att->texture()->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO) {
            // We do this here to avoid any undefined data if we use this attachment as a texture before we actually draw to it
            glGenerateTextureMipmap(static_cast<glTexture*>(att->texture().get())->textureHandle());
        }

        // Find the appropriate binding point
        U32 binding = to_U32(GL_COLOR_ATTACHMENT0) + index;
        if (type == RTAttachmentType::Depth_Stencil) {
            binding = to_U32(GL_DEPTH_ATTACHMENT);

            const TextureType texType = att->texture()->descriptor().texType();
            // Most of these aren't even valid, but hey, doesn't hurt to check
            _isLayeredDepth = texType == TextureType::TEXTURE_1D_ARRAY ||
                                texType == TextureType::TEXTURE_2D_ARRAY ||
                                texType == TextureType::TEXTURE_3D ||
                                IsCubeTexture(texType);
        }

        att->binding(binding);
        setAttachmentState(static_cast<GLenum>(binding), {});
        return true;
    }

    return false;
}

void glFramebuffer::toggleAttachment(const RTAttachment_uptr& attachment, const AttachmentState state, bool layeredRendering) {
    OPTICK_EVENT();

    const Texture_ptr& tex = attachment->texture();
    if (tex == nullptr) {
        return;
    }

    if (layeredRendering && tex->numLayers() == 1 && !IsCubeTexture(tex->descriptor().texType())) {
        layeredRendering = false;
    }

    const GLenum binding = static_cast<GLenum>(attachment->binding());
    const BindingState bState{ state,
                               attachment->mipWriteLevel(),
                               attachment->writeLayer(),
                               layeredRendering };
    // Compare with old state
    if (bState != getAttachmentState(binding)) {
        if (state == AttachmentState::STATE_DISABLED) {
            glNamedFramebufferTexture(_framebufferHandle, binding, 0u, 0);
        } else {
            const GLuint handle = static_cast<glTexture*>(tex.get())->textureHandle();
            if (layeredRendering) {
                glNamedFramebufferTextureLayer(_framebufferHandle, binding, handle, bState._writeLevel, bState._writeLayer);
            } else {
                glNamedFramebufferTexture(_framebufferHandle, binding, handle, bState._writeLevel);
            }
        }
        queueCheckStatus();
        setAttachmentState(binding, bState);
    }
}

bool glFramebuffer::create() {
    if (!RenderTarget::create()) {
        return false;
    }

    setDefaultState({});
    RTClearDescriptor initialClear{};
    initialClear._resetToDefault = false;
    clear(initialClear);
    return checkStatus();
}

void glFramebuffer::blitFrom(const RTBlitParams& params) {
    OPTICK_EVENT();

    if (!params._inputFB || (!params.hasBlitColours() && !IsValid(params._blitDepth))) {
        return;
    }

    const auto prepareAttachments = [](glFramebuffer* fbo, const RTAttachment_uptr& att, const U16 layer) {
        const bool layerChanged = att->writeLayer(layer);
        if (layerChanged || att->numLayers() > 0) {
            const glFramebuffer::BindingState& state = fbo->getAttachmentState(static_cast<GLenum>(att->binding()));
            fbo->toggleAttachment(att, state._attState, att->numLayers() > 0u || layer > 0u);
        }
    };

    glFramebuffer* input = static_cast<glFramebuffer*>(params._inputFB);
    glFramebuffer* output = this;
    const vec2<U16>& inputDim = input->_descriptor._resolution;
    const vec2<U16>& outputDim = output->_descriptor._resolution;

    bool blittedDepth = false;

    const bool depthMisMatch = input->hasDepth() != output->hasDepth();
    if (depthMisMatch) {
        OPTICK_EVENT("Depth mismatch");

        if (input->hasDepth()) {
            input->toggleAttachment(input->_attachments[RT_DEPTH_ATTACHMENT_IDX], AttachmentState::STATE_DISABLED, false);
        }

        if (output->hasDepth()) {
            output->toggleAttachment(output->_attachments[RT_DEPTH_ATTACHMENT_IDX], AttachmentState::STATE_DISABLED, false);
        }
    }

    // Multiple attachments, multiple layers, multiple everything ... what a mess ... -Ionut
    if (params.hasBlitColours() && hasColour()) {
        OPTICK_EVENT("Blit Colours");

        GLuint prevReadAtt = 0;
        GLuint prevWriteAtt = 0;

        const std::array<GLenum, MAX_RT_COLOUR_ATTACHMENTS> currentOutputBuffers = output->_activeColourBuffers;
        for (ColourBlitEntry entry : params._blitColours) {
            if (entry.input()._layer == INVALID_COLOUR_LAYER && entry.input()._index == INVALID_COLOUR_LAYER) {
                continue;
            }
            const I32 inputLayer = std::max(entry.input()._layer, to_I16(0));
            const I32 inputIndex = std::max(entry.input()._index, to_I16(0));

            const I32 outputLayer = entry.output()._layer == INVALID_COLOUR_LAYER ? inputLayer : entry.output()._layer;
            const I32 outputIndex = entry.output()._index == INVALID_COLOUR_LAYER ? inputIndex : entry.output()._index;

            const RTAttachment_uptr& inAtt = input->_attachments[to_U8(inputIndex)];
            const RTAttachment_uptr& outAtt = output->_attachments[to_U8(outputIndex)];

            const GLuint crtReadAtt = inAtt->binding();
            const GLenum readBuffer = static_cast<GLenum>(crtReadAtt);
            if (prevReadAtt != readBuffer) {
                if (readBuffer != input->_activeReadBuffer) {
                    input->_activeReadBuffer = readBuffer;
                    glNamedFramebufferReadBuffer(input->_framebufferHandle, readBuffer);
                }
                prevReadAtt = crtReadAtt;
            }

            const GLuint crtWriteAtt = outAtt->binding();
            if (prevWriteAtt != crtWriteAtt) {
                const GLenum colourAttOut = static_cast<GLenum>(crtWriteAtt);
                bool set = _activeColourBuffers[0] != colourAttOut;
                if (!set) {
                    for (size_t i = 1; i < _activeColourBuffers.size(); ++i) {
                        if (_activeColourBuffers[i] != GL_NONE) {
                            set = true;
                            break;
                        }
                    }
                }
                if (set) {
                    output->_activeColourBuffers.fill(GL_NONE);
                    output->_activeColourBuffers[0] = colourAttOut;
                    glNamedFramebufferDrawBuffer(output->_framebufferHandle, colourAttOut);
                }

                prevWriteAtt = crtWriteAtt;
            }

            const U8 loopCount = IsCubeTexture(inAtt->texture()->descriptor().texType()) ? 6u : 1u;
            for (U8 i = 0u ; i < loopCount; ++i) {
                if (i > 0u) {
                    BlitIndex crtInput = entry.input();
                    BlitIndex crtOutput = entry.output();
                    ++crtInput._layer;
                    ++crtOutput._layer;
                    entry.set(crtInput, crtOutput);
                }
                prepareAttachments(input, inAtt, entry.input()._layer);
                prepareAttachments(output, outAtt, entry.output()._layer);

                // If we change layers, then the depth buffer should match that ... I guess ... this sucks!
                if (!depthMisMatch && loopCount == 1u) {
                    if (input->hasDepth()) {
                        prepareAttachments(input, input->_attachments[RT_DEPTH_ATTACHMENT_IDX], entry.input()._layer);
                    }

                    if (output->hasDepth()) {
                        prepareAttachments(output, output->_attachments[RT_DEPTH_ATTACHMENT_IDX], entry.output()._layer);
                    }
                }

                blittedDepth = loopCount == 1 &&
                               !blittedDepth &&
                               !depthMisMatch &&
                               params._blitDepth._inputLayer == inputLayer &&
                               params._blitDepth._outputLayer == outputLayer;
                checkStatus();

                glBlitNamedFramebuffer(input->_framebufferHandle,
                                       output->_framebufferHandle,
                                       0, 0,
                                       inputDim.width, inputDim.height,
                                       0, 0,
                                       outputDim.width, outputDim.height,
                                       blittedDepth ? GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT 
                                                    : GL_COLOR_BUFFER_BIT,
                                       GL_NEAREST);
                _context.registerDrawCall();
            }
            QueueMipMapsRecomputation(outAtt);
        }

        if (currentOutputBuffers != output->_activeColourBuffers) {
            output->_activeColourBuffers = currentOutputBuffers;
            glNamedFramebufferDrawBuffers(_framebufferHandle,
                                          MAX_RT_COLOUR_ATTACHMENTS,
                                          _activeColourBuffers.data());
        }
    }

    if (!depthMisMatch && !blittedDepth && IsValid(params._blitDepth)) {
        OPTICK_EVENT("Blit Depth");

        prepareAttachments(input, input->_attachments[RT_DEPTH_ATTACHMENT_IDX], params._blitDepth._inputLayer);
        prepareAttachments(output,  output->_attachments[RT_DEPTH_ATTACHMENT_IDX], params._blitDepth._outputLayer);

        checkStatus();

        glBlitNamedFramebuffer(input->_framebufferHandle,
                               output->_framebufferHandle,
                               0, 0,
                               inputDim.width, inputDim.height,
                               0, 0,
                               outputDim.width, outputDim.height,
                               GL_DEPTH_BUFFER_BIT,
                               GL_NEAREST);
        _context.registerDrawCall();
        QueueMipMapsRecomputation(output->_attachments[RT_DEPTH_ATTACHMENT_IDX]);
    }
}

void glFramebuffer::prepareBuffers(const RTDrawDescriptor& drawPolicy) {
    OPTICK_EVENT();

    if (_previousPolicy._drawMask != drawPolicy._drawMask) {
        bool set = false;
        // handle colour buffers first
        const U8 count = std::min(MAX_RT_COLOUR_ATTACHMENTS, getAttachmentCount(RTAttachmentType::Colour));
        for (U8 j = 0; j < MAX_RT_COLOUR_ATTACHMENTS; ++j) {
            GLenum temp = GL_NONE;
            if (j < count) {
                temp = GL_NONE;
                if (IsEnabled(drawPolicy._drawMask, RTAttachmentType::Colour, j) && usesAttachment(RTAttachmentType::Colour, j)) {
                    temp = static_cast<GLenum>(getAttachment(RTAttachmentType::Colour, j)->binding());
                };
            }
            if (_activeColourBuffers[j] != temp) {
                _activeColourBuffers[j] = temp;
                set = true;
            }
        }

        if (set) {
            glNamedFramebufferDrawBuffers(_framebufferHandle, MAX_RT_COLOUR_ATTACHMENTS, _activeColourBuffers.data());
        }

        queueCheckStatus();

        _activeDepthBuffer = usesAttachment(RTAttachmentType::Depth_Stencil , 0);
     }

    GL_API::GetStateTracker()->setDepthWrite(IsEnabled(drawPolicy._drawMask, RTAttachmentType::Depth_Stencil));
}

void glFramebuffer::toggleAttachmentInternal(const RTAttachment_uptr& attachment) {
    if (attachment != nullptr) {
        /// We also draw to mip and layer 0 unless specified otherwise in the drawPolicy
        attachment->writeLayer(0);
        attachment->mipWriteLevel(0);

        /// All active attachments are enabled by default
        toggleAttachment(attachment, AttachmentState::STATE_ENABLED, false);
    }
}

void glFramebuffer::setAttachmentUsage(const RTAttachmentType type, const ImageUsage usage) {
    if (type == RTAttachmentType::Depth_Stencil && _attachments[RT_DEPTH_ATTACHMENT_IDX]) {
        _attachments[RT_DEPTH_ATTACHMENT_IDX]->setImageUsage(usage);
    }
    if (type == RTAttachmentType::Colour) {
        for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS; ++i) {
            if (_attachments[i]) {
                _attachments[i]->setImageUsage(usage);
            }
        }
    }
}

void glFramebuffer::toggleAttachments() {
    OPTICK_EVENT();

    for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS + 1; ++i) {
        toggleAttachmentInternal(_attachments[i]);
    }
}

void glFramebuffer::setDefaultState(const RTDrawDescriptor& drawPolicy) {
    OPTICK_EVENT();

    toggleAttachments();

    /// Setup draw buffers
    prepareBuffers(drawPolicy);

    /// Set the depth range
    GL_API::GetStateTracker()->setDepthRange(_descriptor._depthRange.min, _descriptor._depthRange.max);

    /// Check that everything is valid
    checkStatus();
}

void glFramebuffer::begin(const RTDrawDescriptor& drawPolicy) {
    OPTICK_EVENT();

    // Push debug state
    GL_API::PushDebugMessage(_debugMessage.c_str());

    // Activate FBO
    if (GL_API::GetStateTracker()->setActiveFB(RenderTargetUsage::RT_WRITE_ONLY, _framebufferHandle) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }

    // Set the viewport
    if (drawPolicy._setViewport) {
        _prevViewport.set(_context.activeViewport());
        _context.setViewport(0, 0, to_I32(getWidth()), to_I32(getHeight()));
    }

    if (drawPolicy._setDefaultState) {
        const bool validationEnabled = enableAttachmentChangeValidation();
        enableAttachmentChangeValidation(false);
        setDefaultState(drawPolicy);
        enableAttachmentChangeValidation(validationEnabled);
    }

    if (_descriptor._msaaSamples > 0u && drawPolicy._alphaToCoverage) {
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    }

    setAttachmentUsage(RTAttachmentType::Colour, ImageUsage::RT_COLOUR_ATTACHMENT);
    setAttachmentUsage(RTAttachmentType::Depth_Stencil, ImageUsage::RT_DEPTH_ATTACHMENT);

    // Memorize the current draw policy to speed up later calls
    _previousPolicy = drawPolicy;
}

void glFramebuffer::end(const bool needsUnbind) const {
    OPTICK_EVENT();

    if (needsUnbind) {
        if (GL_API::GetStateTracker()->setActiveFB(RenderTargetUsage::RT_WRITE_ONLY, 0) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    if (_previousPolicy._setViewport) {
        _context.setViewport(_prevViewport);
    }

    if (_descriptor._msaaSamples > 0u && _previousPolicy._alphaToCoverage) {
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }

    queueMipMapRecomputation();

    GL_API::PopDebugMessage();
}

void glFramebuffer::queueMipMapRecomputation() const {
    for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS + 1; ++i) {
        QueueMipMapsRecomputation(_attachments[i]);
    }
}

void glFramebuffer::QueueMipMapsRecomputation(const RTAttachment_uptr& attachment) {
    if (attachment == nullptr) {
        return;
    }

    const Texture_ptr& texture = attachment->texture();
    if (texture != nullptr && texture->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO) {
        glGenerateTextureMipmap(static_cast<glTexture*>(texture.get())->textureHandle());
    }
}

void glFramebuffer::clear(const RTClearDescriptor& descriptor)  {
    OPTICK_EVENT();

    const bool validationEnabled = enableAttachmentChangeValidation();
    if (descriptor._resetToDefault) {
        enableAttachmentChangeValidation(false);
        toggleAttachments();
        prepareBuffers({});
        enableAttachmentChangeValidation(validationEnabled);
    }

    if (descriptor._clearColours && hasColour()) {
        OPTICK_EVENT("Clear Colour Attachments");

        for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS; ++i) {
            RTAttachment* att = getAttachment(RTAttachmentType::Colour, i);
            if (att == nullptr) {
                continue;
            }

            const U32 binding = att->binding();
            if (static_cast<GLenum>(binding) != GL_NONE) {

                if (!descriptor._clearExternalColour && att->isExternal()) {
                    continue;
                }

                const GLint buffer = static_cast<GLint>(binding - static_cast<GLint>(GL_COLOR_ATTACHMENT0));

                if (descriptor._clearColourAttachment[to_U8(buffer)]) {
                    const RTClearColourDescriptor* clearColour = descriptor._customClearColour;

                    const FColour4& colour = clearColour != nullptr ? clearColour->_customClearColour[buffer] : att->clearColour();
                    if (att->texture()->descriptor().normalized()) {
                        glClearNamedFramebufferfv(_framebufferHandle, GL_COLOR, buffer, colour._v);
                    } else {
                        switch (att->texture()->descriptor().dataType()) {
                            case GFXDataFormat::FLOAT_16:
                            case GFXDataFormat::FLOAT_32 : {
                                glClearNamedFramebufferfv(_framebufferHandle, GL_COLOR, buffer, colour._v);
                            } break;
                        
                            case GFXDataFormat::SIGNED_BYTE:
                            case GFXDataFormat::SIGNED_SHORT:
                            case GFXDataFormat::SIGNED_INT: {
                                glClearNamedFramebufferiv(_framebufferHandle, GL_COLOR, buffer, Util::ToIntColour(colour)._v);
                            } break;

                            default: {
                                glClearNamedFramebufferuiv(_framebufferHandle, GL_COLOR, buffer, Util::ToUIntColour(colour)._v);
                            } break;
                        }
                    }
                    _context.registerDrawCall();
                }
            }
        }
    }

    if (descriptor._clearDepth && hasDepth()) {
        OPTICK_EVENT("Clear Depth");

        if (descriptor._clearExternalDepth && _attachments[RT_DEPTH_ATTACHMENT_IDX]->isExternal()) {
            return;
        }
        const RTClearColourDescriptor* clearColour = descriptor._customClearColour;
        const F32 depthValue = clearColour != nullptr ? clearColour->_customClearDepth : _descriptor._depthValue;
        glClearNamedFramebufferfv(_framebufferHandle, GL_DEPTH, 0, &depthValue);
        _context.registerDrawCall();
    }
}

void glFramebuffer::drawToLayer(const DrawLayerParams& params) {
    OPTICK_EVENT();

    if (params._type == RTAttachmentType::COUNT) {
        return;
    }

    const RTAttachment_uptr& att = params._type == RTAttachmentType::Colour ? _attachments[params._index] : _attachments[RT_DEPTH_ATTACHMENT_IDX];

    const GLenum textureType = GLUtil::internalTextureType(att->texture()->descriptor().texType(), att->texture()->descriptor().msaaSamples());
    // only for array textures (it's better to simply ignore the command if the format isn't supported (debugging reasons)
    if (textureType != GL_TEXTURE_2D_ARRAY &&
        textureType != GL_TEXTURE_CUBE_MAP_ARRAY &&
        textureType != GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        DIVIDE_UNEXPECTED_CALL();
        return;
    }

    const bool useDepthLayer =  hasDepth()  && params._includeDepth ||
                                hasDepth()  && params._type == RTAttachmentType::Depth_Stencil;
    const bool useColourLayer = hasColour() && params._type == RTAttachmentType::Colour;

    if (useColourLayer) {
        const BindingState& state = getAttachmentState(static_cast<GLenum>(att->binding()));

        if (att->writeLayer(params._layer)) {
            toggleAttachment(att, state._attState, true);
        }
    }

    if (useDepthLayer && _isLayeredDepth) {
        const BindingState& state = getAttachmentState(static_cast<GLenum>(_attachments[RT_DEPTH_ATTACHMENT_IDX]->binding()));
        if (_attachments[RT_DEPTH_ATTACHMENT_IDX]->writeLayer(params._layer)) {
            toggleAttachment(_attachments[RT_DEPTH_ATTACHMENT_IDX], state._attState, true);
        }
    }

    if_constexpr (Config::Build::IS_DEBUG_BUILD) {
        checkStatus();
    } else {
        _statusCheckQueued = false;
    }
}

bool glFramebuffer::setMipLevelInternal(const RTAttachment_uptr& attachment, U16 writeLevel) {
    if (attachment == nullptr) {
        return false;
    }

    if (attachment->mipWriteLevel(writeLevel)) {
        const BindingState& state = getAttachmentState(static_cast<GLenum>(attachment->binding()));
        toggleAttachment(attachment, state._attState, state._layeredRendering);
        return true;
    }

    return attachment->mipWriteLevel() == writeLevel;
}

void glFramebuffer::setMipLevel(const U16 writeLevel) {
    if (writeLevel == U16_MAX) {
        return;
    }

    OPTICK_EVENT();

    bool changedMip = false;
    bool needsAttachmentDisabled = false;

    changedMip = setMipLevelInternal(_attachments[RT_DEPTH_ATTACHMENT_IDX], writeLevel) || changedMip;
    for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS; ++i) {
        changedMip = getAttachment(RTAttachmentType::Colour, i) || changedMip;
    }

    if (changedMip && needsAttachmentDisabled) {
        for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS + 1; ++i) {
            if (_attachments[i]->mipWriteLevel() != writeLevel) {
                toggleAttachment(_attachments[i], AttachmentState::STATE_DISABLED, false);
            }
        }
    }

    if_constexpr(Config::Build::IS_DEBUG_BUILD) {
        checkStatus();
    } else {
        _statusCheckQueued = false;
    }
}

void glFramebuffer::readData(const vec4<U16>& rect,
                             const GFXImageFormat imageFormat,
                             const GFXDataFormat dataType,
                             const std::pair<bufferPtr, size_t> outData) const {
    OPTICK_EVENT();

    GL_API::GetStateTracker()->setPixelPackUnpackAlignment();
    if (GL_API::GetStateTracker()->setActiveFB(RenderTargetUsage::RT_READ_ONLY, _framebufferHandle) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    glReadnPixels(
        rect.x, rect.y, rect.z, rect.w,
        GLUtil::glImageFormatTable[to_U32(imageFormat)],
        GLUtil::glDataFormat[to_U32(dataType)],
        (GLsizei)outData.second,
        outData.first);
}

bool glFramebuffer::hasDepth() const noexcept {
    return _activeDepthBuffer;
}

bool glFramebuffer::hasColour() const noexcept {
    for (const auto& rt : _attachments) {
        if (rt != nullptr) {
            return true;
        }
    }

    return false;
}

void glFramebuffer::setAttachmentState(const GLenum binding, const BindingState state) {
    _attachmentState[ColorAttachmentToIndex(binding)] = state;
}

glFramebuffer::BindingState glFramebuffer::getAttachmentState(const GLenum binding) const {
    return _attachmentState[ColorAttachmentToIndex(binding)];
}

void glFramebuffer::queueCheckStatus() noexcept {
    _statusCheckQueued = enableAttachmentChangeValidation();
}

bool glFramebuffer::checkStatus() {
    if (!_statusCheckQueued) {
        return true;
    }

    OPTICK_EVENT();

    _statusCheckQueued = false;
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        // check FB status
        const GLenum status = glCheckNamedFramebufferStatus(_framebufferHandle, GL_FRAMEBUFFER);
        if (status == GL_FRAMEBUFFER_COMPLETE) {
            return true;
        }

        switch (status)
        {
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_ATTACHMENT_INCOMPLETE")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_NO_IMAGE")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_INCOMPLETE_DRAW_BUFFER")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_INCOMPLETE_READ_BUFFER")));
                return false;
            }
            case GL_FRAMEBUFFER_UNSUPPORTED: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_UNSUPPORTED")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_INCOMPLETE_MULTISAMPLE")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_INCOMPLETE_LAYER_TARGETS")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_DIMENSIONS")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT: {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_FORMAT")));
                return false;
            }
            default: {
                Console::errorfn(Locale::Get(_ID("ERROR_UNKNOWN")));
            } break;
        };
    }

    return false;
}

};  // namespace Divide
