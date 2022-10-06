#include "stdafx.h"

#include "config.h"

#include "Headers/glFramebuffer.h"

#include "Core/Headers/StringHelper.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"

#include "Utility/Headers/Localization.h"

namespace Divide
{

namespace
{
    [[nodiscard]] FORCE_INLINE bool IsValid(const BlitIndex& entry) noexcept
    {
        return entry._index != INVALID_LAYER_INDEX && entry._layer != INVALID_LAYER_INDEX;
    }

    [[nodiscard]] FORCE_INLINE bool IsValid(const DepthBlitEntry& entry) noexcept
    {
        return entry._inputLayer != INVALID_LAYER_INDEX && entry._outputLayer != INVALID_LAYER_INDEX;
    } 
    
    [[nodiscard]] FORCE_INLINE bool IsValid(const ColourBlitEntry& entry) noexcept
    {
        return IsValid(entry._input) && IsValid(entry._output);
    }

    [[nodiscard]] FORCE_INLINE bool IsValid(const RTBlitParams::ColourArray& colours) noexcept
    {
        for (const auto& it : colours)
        {
            if (IsValid(it))
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] FORCE_INLINE bool IsValid(const RTBlitParams& params) noexcept
    {
        if (IsValid(params._blitDepth))
        {
            return true;
        }

        return IsValid(params._blitColours);
    }

    [[nodiscard]] FORCE_INLINE U32 ColorAttachmentToIndex(const GLenum colorAttachmentEnum) noexcept
    {
        switch (colorAttachmentEnum)
        {
            case GL_DEPTH_ATTACHMENT:
            case GL_DEPTH_STENCIL_ATTACHMENT:return GFXDevice::GetDeviceInformation()._maxRTColourAttachments;
            case GL_STENCIL_ATTACHMENT:      return GFXDevice::GetDeviceInformation()._maxRTColourAttachments + 1u;
            default:
            { //GL_COLOR_ATTACHMENTn
                constexpr U32 offset = to_U32(GL_COLOR_ATTACHMENT0);
                const U32 enumValue = to_U32(colorAttachmentEnum);
                if (enumValue >= offset)
                {
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

bool operator==(const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs) noexcept
{
    return lhs._attState == rhs._attState &&
           lhs._writeLevel == rhs._writeLevel &&
           lhs._writeLayer == rhs._writeLayer &&
           lhs._layeredRendering == rhs._layeredRendering;
}

bool operator!=(const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs) noexcept
{
    return lhs._attState != rhs._attState ||
           lhs._writeLevel != rhs._writeLevel ||
           lhs._writeLayer != rhs._writeLayer ||
           lhs._layeredRendering != rhs._layeredRendering;
}

bool glFramebuffer::s_alphaToCoverageEnabled = false;

glFramebuffer::glFramebuffer(GFXDevice& context, const RenderTargetDescriptor& descriptor)
    : RenderTarget(context, descriptor),
      _prevViewport(-1),
      _debugMessage("Render Target: [ " + name() + " ]")
{
    glCreateFramebuffers(1, &_framebufferHandle);
    assert((_framebufferHandle != 0 && _framebufferHandle != GLUtil::k_invalidObjectID) &&
           "glFramebuffer error: Tried to bind an invalid framebuffer!");

    _isLayeredDepth = false;

    if_constexpr(Config::ENABLE_GPU_VALIDATION)
    {
        // label this FB to be able to tell that it's internally created and nor from a 3rd party lib
        glObjectLabel(GL_FRAMEBUFFER,
                      _framebufferHandle,
                      -1,
                      name().empty() ? Util::StringFormat("DVD_FB_%d", _framebufferHandle).c_str() : name().c_str());
    }

    // Everything disabled so that the initial "begin" will override this
    DisableAll(_previousPolicy._drawMask);
    _attachmentState.resize(GFXDevice::GetDeviceInformation()._maxRTColourAttachments + 1u + 1u); //colours + depth-stencil
}

glFramebuffer::~glFramebuffer()
{
    GL_API::DeleteFramebuffers(1, &_framebufferHandle);
}

bool glFramebuffer::initAttachment(RTAttachment* att, const RTAttachmentType type, const RTColourAttachmentSlot slot, const bool isExternal)
{
    if (RenderTarget::initAttachment(att, type, slot, isExternal))
    {
        if (!isExternal && att->texture()->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO)
        {
            // We do this here to avoid any undefined data if we use this attachment as a texture before we actually draw to it
            glGenerateTextureMipmap(static_cast<glTexture*>(att->texture().get())->textureHandle());
        }

        // Find the appropriate binding point
        U32 binding = to_U32(GL_COLOR_ATTACHMENT0) + to_base(slot);
        if (type == RTAttachmentType::DEPTH || type == RTAttachmentType::DEPTH_STENCIL)
        {
            binding = type == RTAttachmentType::DEPTH ? to_U32(GL_DEPTH_ATTACHMENT) : to_U32(GL_DEPTH_STENCIL_ATTACHMENT);

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

void glFramebuffer::toggleAttachment(const RTAttachment_uptr& attachment, const AttachmentState state, bool layeredRendering)
{
    PROFILE_SCOPE();

    const Texture_ptr& tex = attachment->texture();
    if (tex == nullptr)
    {
        return;
    }

    if (layeredRendering && tex->numLayers() == 1 && !IsCubeTexture(tex->descriptor().texType()))
    {
        layeredRendering = false;
    }

    const GLenum binding = static_cast<GLenum>(attachment->binding());
    const BindingState bState{
        state,
        attachment->mipWriteLevel(),
        attachment->writeLayer(),
        layeredRendering
    };

    // Compare with old state
    if (bState != getAttachmentState(binding))
    {
        if (state == AttachmentState::STATE_DISABLED)
        {
            glNamedFramebufferTexture(_framebufferHandle, binding, 0u, 0);
        }
        else
        {
            const GLuint handle = static_cast<glTexture*>(tex.get())->textureHandle();
            if (layeredRendering)
            {
                glNamedFramebufferTextureLayer(_framebufferHandle, binding, handle, bState._writeLevel, bState._writeLayer);
            }
            else
            {
                glNamedFramebufferTexture(_framebufferHandle, binding, handle, bState._writeLevel);
            }
        }
        queueCheckStatus();
        setAttachmentState(binding, bState);
    }
}

bool glFramebuffer::create()
{
    if (!RenderTarget::create())
    {
        return false;
    }

    for (size_t i = 0u; i < _attachments.size(); ++i )
    {
        setDefaultAttachmentBinding(_attachments[i]);
    }

    /// Setup draw buffers
    prepareBuffers({});

    /// Check that everything is valid
    checkStatus();

    return checkStatus();
}

void glFramebuffer::blitFrom(RenderTarget* source, const RTBlitParams& params)
{
    PROFILE_SCOPE();

    if (source == nullptr || !IsValid(params))
    {
        return;
    }

    const auto prepareAttachments = [](glFramebuffer* fbo, const RTAttachment_uptr& att, const U16 layer)
    {
        const bool layerChanged = att->writeLayer(layer);
        if (layerChanged || att->numLayers() > 0)
        {
            const glFramebuffer::BindingState& state = fbo->getAttachmentState(static_cast<GLenum>(att->binding()));
            fbo->toggleAttachment(att, state._attState, att->numLayers() > 0u || layer > 0u);
        }
    };

    glFramebuffer* input = static_cast<glFramebuffer*>(source);
    glFramebuffer* output = this;
    const vec2<U16> inputDim = input->_descriptor._resolution;
    const vec2<U16> outputDim = output->_descriptor._resolution;

    bool blittedDepth = false;
    bool readBufferDirty = false;
    const bool depthMismatch = input->_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] != output->_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX];

    // Multiple attachments, multiple layers, multiple everything ... what a mess ... -Ionut
    if (IsValid(params._blitColours))
    {
        PROFILE_SCOPE("Blit Colours");

        GLuint prevReadAtt = 0;
        GLuint prevWriteAtt = 0;

        const auto currentOutputBuffers = output->_colourBuffers._glSlot;
        for (ColourBlitEntry entry : params._blitColours)
        {
            if (entry._input._index == INVALID_LAYER_INDEX ||
                entry._output._index == INVALID_LAYER_INDEX ||
                !input->_attachmentsUsed[entry._input._index] ||
                !output->_attachmentsUsed[entry._output._index])
            {
                continue;
            }

            const RTAttachment_uptr& inAtt = input->_attachments[entry._input._index];
            const GLuint crtReadAtt = inAtt->binding();
            const GLenum readBuffer = static_cast<GLenum>(crtReadAtt);
            if (prevReadAtt != readBuffer)
            {
                if (readBuffer != input->_activeReadBuffer)
                {
                    input->_activeReadBuffer = readBuffer;
                    glNamedFramebufferReadBuffer(input->_framebufferHandle, readBuffer);
                    readBufferDirty = true;
                }

                prevReadAtt = crtReadAtt;
            }

            const RTAttachment_uptr& outAtt = output->_attachments[entry._output._index];
            const GLuint crtWriteAtt = outAtt->binding();
            if (prevWriteAtt != crtWriteAtt)
            {
                const GLenum colourAttOut = static_cast<GLenum>(crtWriteAtt);
                bool set = output->_colourBuffers._glSlot[0] != colourAttOut;
                output->_colourBuffers._glSlot[0] = colourAttOut;
                for (size_t i = 1; i < output->_colourBuffers._glSlot.size(); ++i )
                {
                    if (output->_colourBuffers._glSlot[i] != GL_NONE)
                    {
                        output->_colourBuffers._glSlot[i] = GL_NONE;
                        set = true;
                    }
                }

                if (set)
                {
                    output->_colourBuffers._dirty = true;
                    glNamedFramebufferDrawBuffers(output->_framebufferHandle,
                                                  (GLsizei)output->_colourBuffers._glSlot.size(),
                                                  output->_colourBuffers._glSlot.data());
                }

                prevWriteAtt = crtWriteAtt;
            }

            const U8 loopCount = IsCubeTexture(inAtt->texture()->descriptor().texType()) ? 6u : 1u;

            for (U8 i = 0u ; i < loopCount; ++i)
            {
                if (i > 0u)
                {
                    ++entry._input._layer;
                    ++entry._output._layer;
                }

                prepareAttachments(input, inAtt, entry._input._layer);
                prepareAttachments(output, outAtt, entry._output._layer);

                // If we change layers, then the depth buffer should match that ... I guess ... this sucks!
                if (loopCount == 1u && !depthMismatch && input->_attachments[RT_DEPTH_ATTACHMENT_IDX])
                {
                    prepareAttachments(input, input->_attachments[RT_DEPTH_ATTACHMENT_IDX], entry._input._layer);
                    prepareAttachments(output, output->_attachments[RT_DEPTH_ATTACHMENT_IDX], entry._output._layer);
                    if (params._blitDepth._inputLayer == entry._input._layer && params._blitDepth._outputLayer == entry._output._layer)
                    {
                        blittedDepth = true;
                    }
                }

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
    }

    if (!blittedDepth &&
        !depthMismatch &&
        input->_attachments[RT_DEPTH_ATTACHMENT_IDX] &&
        IsValid(params._blitDepth))
    {
        PROFILE_SCOPE("Blit Depth");

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

    if (readBufferDirty )
    {
        glNamedFramebufferReadBuffer( input->_framebufferHandle, GL_NONE );
        input->_activeReadBuffer = GL_NONE;
    }
}

void glFramebuffer::prepareBuffers(const RTDrawDescriptor& drawPolicy)
{
    PROFILE_SCOPE();

    if (_previousPolicy._drawMask != drawPolicy._drawMask || _colourBuffers._dirty )
    {
        bool set = false;
        // handle colour buffers first
        const U8 count = std::min(to_base(RTColourAttachmentSlot::COUNT), getAttachmentCount(RTAttachmentType::COLOUR));
        for (U8 j = 0; j < to_base( RTColourAttachmentSlot::COUNT ); ++j)
        {
            GLenum temp = GL_NONE;
            if (j < count)
            {
                const RTColourAttachmentSlot slot = static_cast<RTColourAttachmentSlot>(j);
                temp = GL_NONE;
                if (IsEnabled(drawPolicy._drawMask, RTAttachmentType::COLOUR, slot) && usesAttachment(RTAttachmentType::COLOUR, slot))
                {
                    temp = static_cast<GLenum>(getAttachment(RTAttachmentType::COLOUR, slot)->binding());
                }
            }

            if (_colourBuffers._glSlot[j] != temp)
            {
                _colourBuffers._glSlot[j] = temp;
                set = true;
            }
        }

        if (set)
        {
            glNamedFramebufferDrawBuffers(_framebufferHandle, to_base( RTColourAttachmentSlot::COUNT ), _colourBuffers._glSlot.data());
        }

        _colourBuffers._dirty = false;
        queueCheckStatus();
     }

    GL_API::GetStateTracker().setDepthWrite(IsEnabled(drawPolicy._drawMask, RTAttachmentType::DEPTH));
}

void glFramebuffer::setDefaultAttachmentBinding(const RTAttachment_uptr& attachment)
{
    PROFILE_SCOPE();

    if (attachment != nullptr)
    {
        /// We also draw to mip and layer 0 unless specified otherwise in the drawPolicy
        attachment->writeLayer(0);
        attachment->mipWriteLevel(0);

        /// All active attachments are enabled by default
        toggleAttachment(attachment, AttachmentState::STATE_ENABLED, false);
    }
}

void glFramebuffer::transitionAttachments( const RTDrawDescriptor& drawPolicy, const bool toWrite)
{
    PROFILE_SCOPE();

    for (U8 i = 0u; i < to_base(RTColourAttachmentSlot::COUNT); ++i )
    {
        if (_attachmentsUsed[i])
        {
            const bool prepareForWrite = IsEnabled( drawPolicy._drawMask, RTAttachmentType::COLOUR, static_cast<RTColourAttachmentSlot>(i) ) && toWrite;
            if (!_attachments[i]->setImageUsage( prepareForWrite ? ImageUsage::RT_COLOUR_ATTACHMENT : ImageUsage::SHADER_SAMPLE ))
            {
                NOP();
            }
        }
    }

    if (_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX])
    {
        const bool prepareForWrite = IsEnabled( drawPolicy._drawMask, RTAttachmentType::DEPTH ) && toWrite;
        if (!_attachments[RT_DEPTH_ATTACHMENT_IDX]->setImageUsage( prepareForWrite ? ImageUsage::RT_DEPTH_ATTACHMENT : ImageUsage::SHADER_SAMPLE))
        {
            NOP();
        }
    }
}

void glFramebuffer::begin(const RTDrawDescriptor& drawPolicy, const RTClearDescriptor& clearPolicy)
{
    PROFILE_SCOPE();

    transitionAttachments(drawPolicy, true);

    const bool needLayeredColour = drawPolicy._writeLayers._depthLayer != INVALID_LAYER_INDEX;
    U16 targetColourLayer = needLayeredColour ? drawPolicy._writeLayers._depthLayer : 0u;

    bool needLayeredDepth = false;
    U16 targetDepthLayer = 0u;
    for (U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i)
    {
        if (!_attachmentsUsed[i])
        {
            continue;
        }

        if (drawPolicy._writeLayers._colourLayers[i] != INVALID_LAYER_INDEX)
        {
            needLayeredDepth = true;
            targetDepthLayer = drawPolicy._writeLayers._colourLayers[i];
            break;
        }
    }

    if (_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX])
    {
        if (drawPolicy._writeLayers._depthLayer != INVALID_LAYER_INDEX || needLayeredDepth)
        {
            targetDepthLayer = drawPolicy._writeLayers._depthLayer == INVALID_LAYER_INDEX ? targetDepthLayer : drawPolicy._writeLayers._depthLayer;
            drawToLayer({ RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0, targetDepthLayer, drawPolicy._mipWriteLevel });
        }
        else if (drawPolicy._mipWriteLevel != U16_MAX)
        {
            setMipLevelInternal(_attachments[RT_DEPTH_ATTACHMENT_IDX], drawPolicy._mipWriteLevel);
        }
        else
        {
            setDefaultAttachmentBinding(_attachments[RT_DEPTH_ATTACHMENT_IDX]);
        }
    }

    for (U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i)
    {
        if (!_attachmentsUsed[i])
        {
            continue;
        }

        if (drawPolicy._writeLayers._colourLayers[i] != INVALID_LAYER_INDEX || needLayeredColour)
        {
            targetColourLayer = drawPolicy._writeLayers._colourLayers[i] == INVALID_LAYER_INDEX ? targetColourLayer : drawPolicy._writeLayers._colourLayers[i];
            drawToLayer({ RTAttachmentType::COLOUR, static_cast<RTColourAttachmentSlot>(i), targetColourLayer, drawPolicy._mipWriteLevel});
        }
        else if (drawPolicy._mipWriteLevel != U16_MAX)
        {
            setMipLevelInternal(_attachments[i], drawPolicy._mipWriteLevel);
        }
        else
        {
            setDefaultAttachmentBinding(_attachments[i]);
        }
    }

    prepareBuffers( drawPolicy );

    if_constexpr(Config::Build::IS_DEBUG_BUILD)
    {
        checkStatus();
    }
    else
    {
        _statusCheckQueued = false;
    }

    // Set the viewport
    if (drawPolicy._setViewport)
    {
        _prevViewport.set(_context.activeViewport());
        _context.setViewport(0, 0, to_I32(getWidth()), to_I32(getHeight()));
    }

    clear(clearPolicy);

    if (_descriptor._msaaSamples > 0u && drawPolicy._alphaToCoverage && !s_alphaToCoverageEnabled)
    {
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        s_alphaToCoverageEnabled = true;
    }
    else if (s_alphaToCoverageEnabled)
    {
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        s_alphaToCoverageEnabled = false;
    }

    /// Set the depth range
    GL_API::GetStateTracker().setDepthRange(_descriptor._depthRange.min, _descriptor._depthRange.max);
    _context.setDepthRange(_descriptor._depthRange);

    // Memorize the current draw policy to speed up later calls
    _previousPolicy = drawPolicy;
}

void glFramebuffer::end()
{
    PROFILE_SCOPE();

    transitionAttachments( _previousPolicy, false);

    for (U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ) + 1u; ++i)
    {
        QueueMipMapsRecomputation(_attachments[i]);
    }
}

void glFramebuffer::QueueMipMapsRecomputation(const RTAttachment_uptr& attachment)
{
    if (attachment == nullptr)
    {
        return;
    }

    const Texture_ptr& texture = attachment->texture();
    if (texture != nullptr && texture->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO)
    {
        glGenerateTextureMipmap(static_cast<glTexture*>(texture.get())->textureHandle());
    }
}

void glFramebuffer::clear(const RTClearDescriptor& descriptor)
{
    PROFILE_SCOPE();
    {
        PROFILE_SCOPE("Clear Colour Attachments");
        for (U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i)
        {
            if (!_attachmentsUsed[i] || descriptor._clearColourDescriptors[i]._index == RTColourAttachmentSlot::COUNT)
            {
                continue;
            }

            RTAttachment* att = getAttachment(RTAttachmentType::COLOUR, descriptor._clearColourDescriptors[i]._index);
            DIVIDE_ASSERT(att != nullptr, "glFramebuffer::error: Invalid clear target specified!");

            const U32 binding = att->binding();
            if (static_cast<GLenum>(binding) != GL_NONE)
            {
                const GLint buffer = static_cast<GLint>(binding - static_cast<GLint>(GL_COLOR_ATTACHMENT0));
                const FColour4& colour = descriptor._clearColourDescriptors[i]._colour;
                if (att->texture()->descriptor().normalized())
                {
                    glClearNamedFramebufferfv(_framebufferHandle, GL_COLOR, buffer, colour._v);
                }
                else
                {
                    switch (att->texture()->descriptor().dataType())
                    {
                        case GFXDataFormat::FLOAT_16:
                        case GFXDataFormat::FLOAT_32 :
                        {
                            glClearNamedFramebufferfv(_framebufferHandle, GL_COLOR, buffer, colour._v);
                        } break;
                        
                        case GFXDataFormat::SIGNED_BYTE:
                        case GFXDataFormat::SIGNED_SHORT:
                        case GFXDataFormat::SIGNED_INT:
                        {
                            static vec4<I32> clearColour;
                            clearColour.set( FLOAT_TO_CHAR_SNORM(colour.r),
                                             FLOAT_TO_CHAR_SNORM(colour.g),
                                             FLOAT_TO_CHAR_SNORM(colour.b),
                                             FLOAT_TO_CHAR_SNORM(colour.a));
                            glClearNamedFramebufferiv(_framebufferHandle, GL_COLOR, buffer, clearColour._v);
                        } break;

                        default:
                        {
                            static vec4<U32> clearColour;
                            clearColour.set( FLOAT_TO_CHAR_UNORM( colour.r ),
                                             FLOAT_TO_CHAR_UNORM( colour.g ),
                                             FLOAT_TO_CHAR_UNORM( colour.b ),
                                             FLOAT_TO_CHAR_UNORM( colour.a ) );
                            glClearNamedFramebufferuiv(_framebufferHandle, GL_COLOR, buffer, clearColour._v);
                        } break;
                    }
                }
                _context.registerDrawCall();
            }
        }
    }

    if (_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] && descriptor._clearDepth)
    {
        PROFILE_SCOPE("Clear Depth");
        glClearNamedFramebufferfv(_framebufferHandle, GL_DEPTH, 0, &descriptor._clearDepthValue);
        _context.registerDrawCall();
    }
}

void glFramebuffer::drawToLayer(const RTDrawLayerParams& params)
{
    PROFILE_SCOPE();

    if (params._type == RTAttachmentType::COUNT)
    {
        return;
    }

    const RTAttachment_uptr& att = params._type == RTAttachmentType::COLOUR ? _attachments[to_base(params._index)] : _attachments[RT_DEPTH_ATTACHMENT_IDX];

    const GLenum textureType = GLUtil::internalTextureType(att->texture()->descriptor().texType(), att->texture()->descriptor().msaaSamples());
    // only for array textures (it's better to simply ignore the command if the format isn't supported (debugging reasons)
    if (textureType != GL_TEXTURE_2D_ARRAY &&
        textureType != GL_TEXTURE_CUBE_MAP_ARRAY &&
        textureType != GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
    {
        DIVIDE_UNEXPECTED_CALL();
        return;
    }

    const U16 targetMip = params._mipLevel == U16_MAX ? 0u : params._mipLevel;

    if (params._type == RTAttachmentType::COLOUR && _attachmentsUsed[to_base(params._index)])
    {
        const BindingState& state = getAttachmentState(static_cast<GLenum>(att->binding()));

        if (att->writeLayer(params._layer) || att->mipWriteLevel(targetMip))
        {
            toggleAttachment(att, state._attState, true);
        }
    }

    if ((params._type == RTAttachmentType::DEPTH || params._type == RTAttachmentType::DEPTH_STENCIL) && _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] && _isLayeredDepth)
    {
        const BindingState& state = getAttachmentState(static_cast<GLenum>(_attachments[RT_DEPTH_ATTACHMENT_IDX]->binding()));
        if (_attachments[RT_DEPTH_ATTACHMENT_IDX]->writeLayer(params._layer) || _attachments[RT_DEPTH_ATTACHMENT_IDX]->mipWriteLevel(targetMip))
        {
            toggleAttachment(_attachments[RT_DEPTH_ATTACHMENT_IDX], state._attState, true);
        }
    }
}

bool glFramebuffer::setMipLevelInternal(const RTAttachment_uptr& attachment, U16 writeLevel)
{
    PROFILE_SCOPE();

    if (attachment == nullptr)
    {
        return false;
    }

    if (attachment->mipWriteLevel(writeLevel))
    {
        const BindingState& state = getAttachmentState(static_cast<GLenum>(attachment->binding()));
        toggleAttachment(attachment, state._attState, state._layeredRendering);
        return true;
    }

    return attachment->mipWriteLevel() == writeLevel;
}

void glFramebuffer::setMipLevel(const U16 writeLevel)
{
    if (writeLevel == U16_MAX)
    {
        return;
    }

    PROFILE_SCOPE();

    bool changedMip = false;
    bool needsAttachmentDisabled = false;

    changedMip = setMipLevelInternal(_attachments[RT_DEPTH_ATTACHMENT_IDX], writeLevel) || changedMip;
    for (U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i)
    {
        changedMip = setMipLevelInternal(_attachments[i], writeLevel) || changedMip;
    }

    if (changedMip && needsAttachmentDisabled)
    {
        for (U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ) + 1; ++i)
        {
            if (_attachments[i]->mipWriteLevel() != writeLevel)
            {
                toggleAttachment(_attachments[i], AttachmentState::STATE_DISABLED, false);
            }
        }
    }
}

void glFramebuffer::readData(const vec4<U16> rect,
                             const GFXImageFormat imageFormat,
                             const GFXDataFormat dataType,
                             const std::pair<bufferPtr, size_t> outData) const
{
    PROFILE_SCOPE();

    GL_API::GetStateTracker().setPixelPackUnpackAlignment();
    if (GL_API::GetStateTracker().setActiveFB(Usage::RT_READ_ONLY, _framebufferHandle) == GLStateTracker::BindResult::FAILED)
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    glReadnPixels(
        rect.x, rect.y, rect.z, rect.w,
        GLUtil::glImageFormatTable[to_U32(imageFormat)],
        GLUtil::glDataFormat[to_U32(dataType)],
        (GLsizei)outData.second,
        outData.first);
}

void glFramebuffer::setAttachmentState(const GLenum binding, const BindingState state)
{
    _attachmentState[ColorAttachmentToIndex(binding)] = state;
}

glFramebuffer::BindingState glFramebuffer::getAttachmentState(const GLenum binding) const
{
    return _attachmentState[ColorAttachmentToIndex(binding)];
}

void glFramebuffer::queueCheckStatus() noexcept
{
    _statusCheckQueued = enableAttachmentChangeValidation();
}

bool glFramebuffer::checkStatus()
{
    if (!_statusCheckQueued)
    {
        return true;
    }

    PROFILE_SCOPE();

    _statusCheckQueued = false;
    if_constexpr(Config::ENABLE_GPU_VALIDATION)
    {
        // check FB status
        const GLenum status = glCheckNamedFramebufferStatus(_framebufferHandle, GL_FRAMEBUFFER);
        if (status == GL_FRAMEBUFFER_COMPLETE)
        {
            return true;
        }

        switch (status)
        {
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_ATTACHMENT_INCOMPLETE")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_NO_IMAGE")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_INCOMPLETE_DRAW_BUFFER")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_INCOMPLETE_READ_BUFFER")));
                return false;
            }
            case GL_FRAMEBUFFER_UNSUPPORTED:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_UNSUPPORTED")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_INCOMPLETE_MULTISAMPLE")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_INCOMPLETE_LAYER_TARGETS")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_DIMENSIONS")));
                return false;
            }
            case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_RT_FORMAT")));
                return false;
            }
            default:
            {
                Console::errorfn(Locale::Get(_ID("ERROR_UNKNOWN")));
            } break;
        };
    }

    return false;
}

};  // namespace Divide
