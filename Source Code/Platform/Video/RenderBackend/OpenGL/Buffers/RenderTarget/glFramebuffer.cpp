#include "stdafx.h"

#include "config.h"

#include "Headers/glFramebuffer.h"

#include "Core/Headers/StringHelper.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

namespace {
    FORCE_INLINE bool IsValid(const DepthBlitEntry& entry) noexcept {
        return entry._inputLayer != INVALID_DEPTH_LAYER &&
               entry._outputLayer != INVALID_DEPTH_LAYER;
    }
    FORCE_INLINE U32 ColorAttachmentToIndex(const GLenum colorAttachmentEnum) noexcept {
        switch (colorAttachmentEnum) {
            case GL_DEPTH_ATTACHMENT  : return GL_API::s_maxFBOAttachments + 1u;
            case GL_STENCIL_ATTACHMENT: return GL_API::s_maxFBOAttachments + 2u;
            default: { //GL_COLOR_ATTACHMENTn
                constexpr U32 offset = to_U32(GL_COLOR_ATTACHMENT0);
                const U32 enumValue = to_U32(colorAttachmentEnum);
                if (enumValue >= offset) {
                    const U32 diff = enumValue - offset;
                    assert(diff < GL_API::s_maxFBOAttachments);
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

bool glFramebuffer::_zWriteEnabled = true;

glFramebuffer::glFramebuffer(GFXDevice& context, const RenderTargetDescriptor& descriptor)
    : RenderTarget(context, descriptor),
      glObject(glObjectType::TYPE_FRAMEBUFFER, context),
      _activeColourBuffers{},
      _prevViewport(-1),
      _debugMessage("Render Target: [ " + name() + " ]")
{
    glCreateFramebuffers(1, &_framebufferHandle);
    assert(_framebufferHandle != 0 && "glFramebuffer error: Tried to bind an invalid framebuffer!");

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
    _attachmentState.resize(GL_API::s_maxFBOAttachments + 2u); //colours + depth + stencil
}

glFramebuffer::~glFramebuffer()
{
    GL_API::DeleteFramebuffers(1, &_framebufferHandle);
    destroy();
}

void glFramebuffer::initAttachment(const RTAttachmentType type, const U8 index) {
    // Avoid invalid dimensions
    assert(getWidth() != 0 && getHeight() != 0 && "glFramebuffer error: Invalid frame buffer dimensions!");

    // Process only valid attachments
    RTAttachment* attachment = _attachmentPool->get(type, index).get();
    if (!attachment->used()) {
        return;
    }   

    Texture* tex = nullptr;
    if (!attachment->isExternal()) {
        tex = attachment->texture().get();
        // Do we need to resize the attachment?
        const bool shouldResize = tex->width() != getWidth() || tex->height() != getHeight();
        if (shouldResize) {
            tex->loadData({nullptr, 0 }, vec2<U16>(getWidth(), getHeight()));
        }
        const bool updateSampleCount = tex->descriptor().msaaSamples() != _descriptor._msaaSamples;
        if (updateSampleCount) {
            tex->setSampleCount(_descriptor._msaaSamples);
        }
        if (tex->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO) {
            // We do this here to avoid any undefined data if we use this attachment as a texture before we actually draw to it
            GL_API::ComputeMipMaps(tex->data()._textureHandle);
        }
    } else {
        RTAttachment* attachmentTemp = _attachmentPool->get(type, index).get();
        if (attachmentTemp->isExternal()) {
            RenderTarget& parent = attachmentTemp->parent().parent();
            attachmentTemp = &parent.getAttachment(attachmentTemp->getExternal()->descriptor()._type, attachmentTemp->getExternal()->descriptor()._index);
        }

        attachment->setTexture(attachmentTemp->texture(true));
        attachment->clearChanged();
        tex = attachment->texture(false).get();
    }
    assert(IsValid(tex->data()));

    // Find the appropriate binding point
    if (type == RTAttachmentType::Depth) {
        attachment->binding(to_U32(GL_DEPTH_ATTACHMENT));

        const TextureType texType = tex->data()._textureType;
        _isLayeredDepth = texType == TextureType::TEXTURE_2D_ARRAY ||
            texType == TextureType::TEXTURE_2D_ARRAY_MS ||
            texType == TextureType::TEXTURE_CUBE_MAP ||
            texType == TextureType::TEXTURE_CUBE_ARRAY ||
            texType == TextureType::TEXTURE_3D;
    } else {
        attachment->binding(to_U32(GL_COLOR_ATTACHMENT0) + index);
    }

    attachment->clearChanged();
}

void glFramebuffer::toggleAttachment(const RTAttachment& attachment, const AttachmentState state, bool layeredRendering) {
    if (!attachment.used()) {
        return;
    }

    OPTICK_EVENT();

    const Texture_ptr& tex = attachment.texture(false);
    if (layeredRendering && tex->numLayers() == 1 && !IsCubeTexture(tex->descriptor().texType())) {
        layeredRendering = false;
    }

    const GLenum binding = static_cast<GLenum>(attachment.binding());
    const BindingState bState{ state,
                               attachment.mipWriteLevel(),
                               attachment.writeLayer(),
                               layeredRendering };
    // Compare with old state
    if (bState != getAttachmentState(binding)) {
        if (state == AttachmentState::STATE_DISABLED) {
            glNamedFramebufferTexture(_framebufferHandle, binding, 0u, 0);
        } else {
            const GLuint handle = tex->data()._textureHandle;
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
    if (!RenderTarget::create() && _attachmentPool == nullptr) {
        return false;
    }

    // For every attachment, be it a colour or depth attachment ...
    [[maybe_unused]] GLuint attachmentCountTotal = 0u;
    for (U8 i = 0; i < to_base(RTAttachmentType::COUNT); ++i) {
        for (U8 j = 0; j < _attachmentPool->attachmentCount(static_cast<RTAttachmentType>(i)); ++j) {
            initAttachment(static_cast<RTAttachmentType>(i), j);
            assert(GL_API::s_maxFBOAttachments > ++attachmentCountTotal);
        }
    }

    setDefaultState({});

    return checkStatus();
}

namespace BlitHelpers {
    inline RTAttachment* prepareAttachments(glFramebuffer* fbo, RTAttachment* att, const U16 layer) {
        const bool layerChanged = att->writeLayer(layer);
        if (layerChanged || att->numLayers() > 0) {
            const glFramebuffer::BindingState& state = fbo->getAttachmentState(static_cast<GLenum>(att->binding()));
            fbo->toggleAttachment(*att, state._attState, att->numLayers() > 0u || layer > 0u);
        }
        return att;
    };

    inline RTAttachment* prepareAttachments(glFramebuffer* fbo, RTAttachment* att, const ColourBlitEntry& entry, const bool isInput) {
        return prepareAttachments(fbo, att, isInput ? entry.input()._layer : entry.output()._layer);
    };

    inline RTAttachment* prepareAttachments(glFramebuffer* fbo, RTAttachment* att, const DepthBlitEntry& entry, const bool isInput) {
        return prepareAttachments(fbo, att, isInput ? entry._inputLayer : entry._outputLayer);
    };

    inline RTAttachment* prepareAttachments(glFramebuffer* fbo, const ColourBlitEntry& entry, const bool isInput) {
        RTAttachment* att = fbo->getAttachmentPtr(RTAttachmentType::Colour, to_U8(entry.input()._index)).get();
        return prepareAttachments(fbo, att, entry, isInput);
    };

    inline RTAttachment* prepareAttachments(glFramebuffer* fbo, const DepthBlitEntry& entry, const bool isInput) {
        RTAttachment* att = fbo->getAttachmentPtr(RTAttachmentType::Depth, 0u).get();
        return prepareAttachments(fbo, att, entry, isInput);
    };
};

void glFramebuffer::blitFrom(const RTBlitParams& params) {
    OPTICK_EVENT();

    if (!params._inputFB || (!params.hasBlitColours() && !IsValid(params._blitDepth))) {
        return;
    }

    glFramebuffer* input = static_cast<glFramebuffer*>(params._inputFB);
    glFramebuffer* output = this;
    const vec2<U16>& inputDim = input->_descriptor._resolution;
    const vec2<U16>& outputDim = output->_descriptor._resolution;

    bool blittedDepth = false;

    const bool depthMisMatch = input->hasDepth() != output->hasDepth();
    if (depthMisMatch) {
        if (input->hasDepth()) {
            const RTAttachment* att = input->getAttachmentPtr(RTAttachmentType::Depth, 0u).get();
            input->toggleAttachment(*att, AttachmentState::STATE_DISABLED, false);
        }

        if (output->hasDepth()) {
            const RTAttachment* att = output->getAttachmentPtr(RTAttachmentType::Depth, 0u).get();
            output->toggleAttachment(*att, AttachmentState::STATE_DISABLED, false);
        }
    }

    // Multiple attachments, multiple layers, multiple everything ... what a mess ... -Ionut
    if (params.hasBlitColours() && hasColour()) {

        const RTAttachmentPool::PoolEntry& inputAttachments = input->_attachmentPool->get(RTAttachmentType::Colour);
        const RTAttachmentPool::PoolEntry& outputAttachments = output->_attachmentPool->get(RTAttachmentType::Colour);

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

            const RTAttachment_ptr& inAtt = inputAttachments[inputIndex];
            const RTAttachment_ptr& outAtt = outputAttachments[outputIndex];

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

            
            RTAttachment* inAttachment = inAtt.get();
            const U8 loopCount = IsCubeTexture(inAttachment->texture(false)->data()._textureType) ? 6u : 1u;
            RTAttachment* outAttachment = outAtt.get();
            assert(loopCount == 1u || IsCubeTexture(outAttachment->texture(false)->data()._textureType));
            for (U8 i = 0u ; i < loopCount; ++i) {
                if (i > 0u) {
                    BlitIndex crtInput = entry.input();
                    BlitIndex crtOutput = entry.output();
                    ++crtInput._layer;
                    ++crtOutput._layer;
                    entry.set(crtInput, crtOutput);
                }
                BlitHelpers::prepareAttachments(input, inAttachment, entry, true);
                BlitHelpers::prepareAttachments(output, outAttachment, entry, false);

                // If we change layers, then the depth buffer should match that ... I guess ... this sucks!
                if (!depthMisMatch && loopCount == 1u) {
                    if (input->hasDepth()) {
                        BlitHelpers::prepareAttachments(input, entry, true);
                    }

                    if (output->hasDepth()) {
                        BlitHelpers::prepareAttachments(output, entry, false);
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
            QueueMipMapsRecomputation(*outAtt);
        }

        if (currentOutputBuffers != output->_activeColourBuffers) {
            output->_activeColourBuffers = currentOutputBuffers;
            glNamedFramebufferDrawBuffers(_framebufferHandle,
                                          MAX_RT_COLOUR_ATTACHMENTS,
                                          _activeColourBuffers.data());
        }
    }

    if (!depthMisMatch && !blittedDepth && IsValid(params._blitDepth)) {
        BlitHelpers::prepareAttachments(input, params._blitDepth, true);
        const RTAttachment* outAtt = BlitHelpers::prepareAttachments(output,  params._blitDepth, false);
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
        QueueMipMapsRecomputation(*outAtt);
    }
}

void glFramebuffer::setBlendState(const RTBlendStates& blendStates) const {
    const RTAttachmentPool::PoolEntry& colourAttachments = _attachmentPool->get(RTAttachmentType::Colour);
    setBlendState(blendStates, colourAttachments);
}

void glFramebuffer::setBlendState(const RTBlendStates& blendStates, const RTAttachmentPool::PoolEntry& activeAttachments) const {
    OPTICK_EVENT();

    for (size_t i = 0u; i < activeAttachments.size(); ++i) {
        const RTAttachment_ptr& colourAtt = activeAttachments[i];
        const RTBlendState& blend = blendStates[i];

        // Set blending per attachment if specified. Overrides general blend state
        GL_API::getStateTracker().setBlending(static_cast<GLuint>(colourAtt->binding() - to_U32(GL_COLOR_ATTACHMENT0)), blend._blendProperties);
        GL_API::getStateTracker().setBlendColour(blend._blendColour);
    }
}

void glFramebuffer::prepareBuffers(const RTDrawDescriptor& drawPolicy, const RTAttachmentPool::PoolEntry& activeAttachments) {
    OPTICK_EVENT();

    if (_previousPolicy._drawMask != drawPolicy._drawMask) {
        bool set = false;
        // handle colour buffers first
        const U8 count = to_U8(std::min(to_size(MAX_RT_COLOUR_ATTACHMENTS), activeAttachments.size()));
        for (U8 j = 0; j < MAX_RT_COLOUR_ATTACHMENTS; ++j) {
            GLenum temp = GL_NONE;
            if (j < count) {
                temp = IsEnabled(drawPolicy._drawMask, RTAttachmentType::Colour, j) ? static_cast<GLenum>(activeAttachments[j]->binding()) : GL_NONE;
            }
            if (_activeColourBuffers[j] != temp) {
                _activeColourBuffers[j] = temp;
                set = true;
            }
        }

        if (set) {
            glNamedFramebufferDrawBuffers(_framebufferHandle,
                                          MAX_RT_COLOUR_ATTACHMENTS,
                                          _activeColourBuffers.data());
        }

        queueCheckStatus();

        const RTAttachment_ptr& depthAtt = _attachmentPool->get(RTAttachmentType::Depth, 0);
        _activeDepthBuffer = depthAtt && depthAtt->used();
     }
    
    if (IsEnabled(drawPolicy._drawMask, RTAttachmentType::Depth) != _zWriteEnabled) {
        _zWriteEnabled = !_zWriteEnabled;
        glDepthMask(_zWriteEnabled ? GL_TRUE : GL_FALSE);
    }
}

void glFramebuffer::toggleAttachments() {
    OPTICK_EVENT();

    for (U8 i = 0; i < to_base(RTAttachmentType::COUNT); ++i) {
        /// Get the attachments in use for each type
        const RTAttachmentPool::PoolEntry& attachments = _attachmentPool->get(static_cast<RTAttachmentType>(i));

        /// Reset attachments if they changed (e.g. after layered rendering);
        for (const RTAttachment_ptr& attachment : attachments) {
            /// We also draw to mip and layer 0 unless specified otherwise in the drawPolicy
            attachment->writeLayer(0);
            attachment->mipWriteLevel(0);

            /// All active attachments are enabled by default
            toggleAttachment(*attachment, AttachmentState::STATE_ENABLED, false);
        }
    }
}

void glFramebuffer::clear(const RTClearDescriptor& descriptor) {
    OPTICK_EVENT();

    const bool validationEnabled = enableAttachmentChangeValidation();
    const RTAttachmentPool::PoolEntry& colourAttachments = _attachmentPool->get(RTAttachmentType::Colour);

    if (descriptor._resetToDefault) {
        enableAttachmentChangeValidation(false);
        toggleAttachments();
        prepareBuffers({}, colourAttachments);
        enableAttachmentChangeValidation(validationEnabled);
    }

    /// Clear the draw buffers
    clear(descriptor, colourAttachments);
}

void glFramebuffer::setDefaultState(const RTDrawDescriptor& drawPolicy) {
    toggleAttachments();

    const RTAttachmentPool::PoolEntry& colourAttachments = _attachmentPool->get(RTAttachmentType::Colour);

    /// Setup draw buffers
    prepareBuffers(drawPolicy, colourAttachments);

    /// Set the depth range
    GL_API::getStateTracker().setDepthRange(_descriptor._depthRange.min, _descriptor._depthRange.max);

    /// Check that everything is valid
    checkStatus();
}

void glFramebuffer::begin(const RTDrawDescriptor& drawPolicy) {
    OPTICK_EVENT();

    // Push debug state
    GL_API::PushDebugMessage(_debugMessage.c_str());

    // Activate FBO
    GL_API::getStateTracker().setActiveFB(RenderTargetUsage::RT_WRITE_ONLY, _framebufferHandle);

    // Set the viewport
    if (drawPolicy._setViewport) {
        _prevViewport.set(_context.getViewport());
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

    // Memorize the current draw policy to speed up later calls
    _previousPolicy = drawPolicy;
}

void glFramebuffer::end(const bool needsUnbind) const {
    OPTICK_EVENT();

    if (needsUnbind) {
        GL_API::getStateTracker().setActiveFB(RenderTargetUsage::RT_WRITE_ONLY, 0);
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
    if (hasColour()) {
        const RTAttachmentPool::PoolEntry& colourAttachments = _attachmentPool->get(RTAttachmentType::Colour);
        for (const RTAttachment_ptr& att : colourAttachments) {
            QueueMipMapsRecomputation(*att);
        }
    }

    if (hasDepth()) {
        const RTAttachment_ptr& attDepth = _attachmentPool->get(RTAttachmentType::Depth, 0);
        QueueMipMapsRecomputation(*attDepth);
    }
}

void glFramebuffer::QueueMipMapsRecomputation(const RTAttachment& attachment) {
    const Texture_ptr& texture = attachment.texture(false);
    if (attachment.used() && texture->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO) {
        GL_API::QueueComputeMipMaps(texture->data()._textureHandle);
    }
}

void glFramebuffer::clear(const RTClearDescriptor& drawPolicy, const RTAttachmentPool::PoolEntry& activeAttachments) const {
    OPTICK_EVENT();

    if (drawPolicy._clearColours && hasColour()) {
        for (const RTAttachment_ptr& att : activeAttachments) {
            const U32 binding = att->binding();
            if (static_cast<GLenum>(binding) != GL_NONE) {

                if (!drawPolicy._clearExternalColour && att->isExternal()) {
                    continue;
                }

                const GLint buffer = static_cast<GLint>(binding - static_cast<GLint>(GL_COLOR_ATTACHMENT0));

                if (drawPolicy._clearColourAttachment[to_U8(buffer)]) {
                    const RTClearColourDescriptor* clearColour = drawPolicy._customClearColour;

                    const FColour4& colour = clearColour != nullptr ? clearColour->_customClearColour[buffer] : att->clearColour();
                    if (att->texture(false)->descriptor().normalized()) {
                        glClearNamedFramebufferfv(_framebufferHandle, GL_COLOR, buffer, colour._v);
                    } else {
                        switch (att->texture(false)->descriptor().dataType()) {
                            case GFXDataFormat::FLOAT_16:
                            case GFXDataFormat::FLOAT_32 :
                                glClearNamedFramebufferfv(_framebufferHandle, GL_COLOR, buffer, colour._v);
                                break;
                        
                            case GFXDataFormat::SIGNED_BYTE:
                            case GFXDataFormat::SIGNED_SHORT:
                            case GFXDataFormat::SIGNED_INT :
                                glClearNamedFramebufferiv(_framebufferHandle, GL_COLOR, buffer, Util::ToIntColour(colour)._v);
                                break;

                            default:
                                glClearNamedFramebufferuiv(_framebufferHandle, GL_COLOR, buffer, Util::ToUIntColour(colour)._v);
                                break;
                        }
                    }
                    _context.registerDrawCall();
                }
            }
        }
    }

    if (drawPolicy._clearDepth && hasDepth()) {
        if (drawPolicy._clearExternalDepth && _attachmentPool->get(RTAttachmentType::Depth, 0)->isExternal()) {
            return;
        }
        const RTClearColourDescriptor* clearColour = drawPolicy._customClearColour;
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

    const RTAttachment_ptr& att = _attachmentPool->get(params._type, params._index);

    const GLenum textureType = GLUtil::glTextureTypeTable[to_U32(att->texture(false)->data()._textureType)];
    // only for array textures (it's better to simply ignore the command if the format isn't supported (debugging reasons)
    if (textureType != GL_TEXTURE_2D_ARRAY &&
        textureType != GL_TEXTURE_CUBE_MAP_ARRAY &&
        textureType != GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        DIVIDE_UNEXPECTED_CALL();
        return;
    }

    const bool useDepthLayer =  hasDepth()  && params._includeDepth ||
                                hasDepth()  && params._type == RTAttachmentType::Depth;
    const bool useColourLayer = hasColour() && params._type == RTAttachmentType::Colour;

    if (useDepthLayer && _isLayeredDepth) {
        const RTAttachment_ptr& attDepth = _attachmentPool->get(RTAttachmentType::Depth, 0);
        const BindingState& state = getAttachmentState(static_cast<GLenum>(attDepth->binding()));
        if (attDepth->writeLayer(params._layer)) {
            toggleAttachment(*attDepth, state._attState, true);
        }
    }

    if (useColourLayer) {
        const BindingState& state = getAttachmentState(static_cast<GLenum>(att->binding()));
        if (att->writeLayer(params._layer)) {
            toggleAttachment(*att, state._attState, true);
        }
    }

    if_constexpr (Config::Build::IS_DEBUG_BUILD) {
        checkStatus();
    } else {
        _statusCheckQueued = false;
    }
}

void glFramebuffer::setMipLevel(const U16 writeLevel) {
    if (writeLevel == U16_MAX) {
        return;
    }

    OPTICK_EVENT();

    bool changedMip = false;
    bool needsAttachmentDisabled = false;
    for (U8 i = 0; i < to_base(RTAttachmentType::COUNT); ++i) {
        const RTAttachmentPool::PoolEntry& attachments = _attachmentPool->get(static_cast<RTAttachmentType>(i));

        for (const RTAttachment_ptr& attachment : attachments) {
            if (attachment->mipWriteLevel(writeLevel)) {
                const BindingState& state = getAttachmentState(static_cast<GLenum>(attachment->binding()));
                changedMip = true;
                toggleAttachment(*attachment, state._attState, state._layeredRendering);
            } else if (attachment->mipWriteLevel() != writeLevel) {
                needsAttachmentDisabled = true;
            }
        }
    }

    if (changedMip && needsAttachmentDisabled) {
        for (U8 i = 0; i < to_base(RTAttachmentType::COUNT); ++i) {
            const RTAttachmentPool::PoolEntry& attachments = _attachmentPool->get(static_cast<RTAttachmentType>(i));
            for (const RTAttachment_ptr& attachment : attachments) {
                if (attachment->mipWriteLevel() != writeLevel) {
                    toggleAttachment(*attachment, AttachmentState::STATE_DISABLED, false);
                }
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

    GL_API::getStateTracker().setPixelPackUnpackAlignment();
    GL_API::getStateTracker().setActiveFB(RenderTargetUsage::RT_READ_ONLY, _framebufferHandle);
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
    return _attachmentPool->attachmentCount(RTAttachmentType::Colour) > 0;
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
