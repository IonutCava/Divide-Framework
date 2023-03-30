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
        [[nodiscard]] FORCE_INLINE U32 ColorAttachmentToIndex( const GLenum colorAttachmentEnum ) noexcept
        {
            switch ( colorAttachmentEnum )
            {
                case GL_DEPTH_ATTACHMENT:
                case GL_DEPTH_STENCIL_ATTACHMENT:return GFXDevice::GetDeviceInformation()._maxRTColourAttachments;
                case GL_STENCIL_ATTACHMENT:      return GFXDevice::GetDeviceInformation()._maxRTColourAttachments + 1u;
                default:
                { //GL_COLOR_ATTACHMENTn
                    constexpr U32 offset = to_U32( GL_COLOR_ATTACHMENT0 );
                    const U32 enumValue = to_U32( colorAttachmentEnum );
                    if ( enumValue >= offset )
                    {
                        const U32 diff = enumValue - offset;
                        assert( diff < GFXDevice::GetDeviceInformation()._maxRTColourAttachments );
                        return diff;
                    }
                } break;
            };

            DIVIDE_UNEXPECTED_CALL();
            return U32_MAX;
        }
    };

    bool operator==( const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs ) noexcept
    {
        return lhs._attState == rhs._attState &&
               lhs._layer._layer == rhs._layer._layer &&
               lhs._layer._cubeFace == rhs._layer._cubeFace &&
               lhs._levelOffset == rhs._levelOffset;
    }

    bool operator!=( const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs ) noexcept
    {
        return lhs._attState != rhs._attState ||
               lhs._layer._layer != rhs._layer._layer ||
               lhs._layer._cubeFace != rhs._layer._cubeFace ||
               lhs._levelOffset != rhs._levelOffset;
    }

    glFramebuffer::glFramebuffer( GFXDevice& context, const RenderTargetDescriptor& descriptor )
        : RenderTarget( context, descriptor ),
        _prevViewport( -1 ),
        _debugMessage( "Render Target: [ " + name() + " ]" )
    {
        glCreateFramebuffers( 1, &_framebufferHandle );
        assert( (_framebufferHandle != 0 && _framebufferHandle != GLUtil::k_invalidObjectID) &&
                "glFramebuffer error: Tried to bind an invalid framebuffer!" );

        _isLayeredDepth = false;

        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            // label this FB to be able to tell that it's internally created and nor from a 3rd party lib
            glObjectLabel( GL_FRAMEBUFFER,
                           _framebufferHandle,
                           -1,
                           name().empty() ? Util::StringFormat( "DVD_FB_%d", _framebufferHandle ).c_str() : name().c_str() );
        }

        // Everything disabled so that the initial "begin" will override this
        _previousPolicy._drawMask.fill(false);
        _attachmentState.resize( GFXDevice::GetDeviceInformation()._maxRTColourAttachments + 1u + 1u ); //colours + depth-stencil
    }

    glFramebuffer::~glFramebuffer()
    {
        GL_API::DeleteFramebuffers( 1, &_framebufferHandle );
    }

    bool glFramebuffer::initAttachment( RTAttachment* att, const RTAttachmentType type, const RTColourAttachmentSlot slot, const bool isExternal )
    {
        if ( RenderTarget::initAttachment( att, type, slot, isExternal ) )
        {
            if ( !isExternal && att->texture()->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO )
            {
                // We do this here to avoid any undefined data if we use this attachment as a texture before we actually draw to it
                glGenerateTextureMipmap( static_cast<glTexture*>(att->texture().get())->textureHandle() );
            }

            // Find the appropriate binding point
            U32 binding = to_U32( GL_COLOR_ATTACHMENT0 ) + to_base( slot );
            if ( type == RTAttachmentType::DEPTH || type == RTAttachmentType::DEPTH_STENCIL )
            {
                binding = type == RTAttachmentType::DEPTH ? to_U32( GL_DEPTH_ATTACHMENT ) : to_U32( GL_DEPTH_STENCIL_ATTACHMENT );

                const TextureType texType = att->texture()->descriptor().texType();
                // Most of these aren't even valid, but hey, doesn't hurt to check
                _isLayeredDepth = texType == TextureType::TEXTURE_1D_ARRAY ||
                    texType == TextureType::TEXTURE_2D_ARRAY ||
                    texType == TextureType::TEXTURE_3D ||
                    IsCubeTexture( texType );
            }

            att->binding( binding );
            setAttachmentState( static_cast<GLenum>(binding), {} );
            return true;
        }

        return false;
    }

    bool glFramebuffer::toggleAttachment( const RTAttachment_uptr& attachment, const AttachmentState state, const U16 levelOffset, const DrawLayerEntry layerOffset )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const Texture_ptr& tex = attachment->texture();
        if ( tex == nullptr )
        {
            return false;
        }

        const GLenum binding = static_cast<GLenum>(attachment->binding());
        const BindingState bState
        {
            ._layer = layerOffset,
            ._levelOffset = levelOffset,
            ._attState = state
        };

        // Compare with old state
        if ( bState != getAttachmentState( binding ) )
        {
            if ( state == AttachmentState::STATE_DISABLED )
            {
                glNamedFramebufferTexture( _framebufferHandle, binding, 0u, 0u );
            }
            else
            {
                const GLuint handle = static_cast<glTexture*>(tex.get())->textureHandle();
                if ( bState._layer._layer == 0u && bState._layer._cubeFace == 0u)
                {
                    glNamedFramebufferTexture( _framebufferHandle, binding, handle, bState._levelOffset);
                }
                else
                {
                    if ( IsCubeTexture( tex->descriptor().texType() ) )
                    {
                        glNamedFramebufferTextureLayer( _framebufferHandle, binding, handle, bState._levelOffset, bState._layer._cubeFace + (bState._layer._layer * 6u) );
                    }
                    else
                    {
                        assert(bState._layer._cubeFace == 0u);
                        glNamedFramebufferTextureLayer( _framebufferHandle, binding, handle, bState._levelOffset, bState._layer._layer );
                    }
                }
            }

            queueCheckStatus();
            setAttachmentState( binding, bState );

            return true;
        }

        return false;
    }

    bool glFramebuffer::create()
    {
        if ( !RenderTarget::create() )
        {
            return false;
        }

        for ( size_t i = 0u; i < _attachments.size(); ++i )
        {
            setDefaultAttachmentBinding( _attachments[i] );
        }

        /// Setup draw buffers
        prepareBuffers( {} );

        return checkStatus();
    }

    void glFramebuffer::blitFrom( RenderTarget* source, const RTBlitParams& params )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( source == nullptr || !IsValid(params) )
        {
            return;
        }

        glFramebuffer* input = static_cast<glFramebuffer*>(source);
        glFramebuffer* output = this;
        const vec2<U16> inputDim = input->_descriptor._resolution;
        const vec2<U16> outputDim = output->_descriptor._resolution;

        // When reading/writing from/to an FBO, we need to make sure it is complete and doesn't throw a GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS.
        // An easy way to achieve this is to disable attachments we don't need to write to
        const auto preparetAttachments = []( glFramebuffer* fbo, const U16 attIndex, const U16 layer, const U16 mip )
        {
            bool ret = false;
            for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ) + 1u; ++i )
            {
                if ( !fbo->_attachmentsUsed[i] )
                {
                    continue;
                }

                if (fbo->toggleAttachment( fbo->_attachments[i], i == attIndex ? AttachmentState::STATE_ENABLED : AttachmentState::STATE_DISABLED, mip, {layer, 0u} ) )
                {
                    ret = true;
                }
            }

            if ( ret )
            {
                return fbo->checkStatus();
            }

            return false;
        };

        bool readBufferDirty = false;

        for ( const RTBlitEntry entry : params )
        {
            if ( entry._input._index == INVALID_INDEX ||
                 entry._output._index == INVALID_INDEX ||
                 !input->_attachmentsUsed[entry._input._index] ||
                 !output->_attachmentsUsed[entry._output._index] )
            {
                continue;
            }

            const RTAttachment_uptr& inAtt = input->_attachments[entry._input._index];
            const RTAttachment_uptr& outAtt = output->_attachments[entry._output._index];
            const GLenum readBuffer = static_cast<GLenum>(inAtt->binding());
            const GLenum writeBuffer = static_cast<GLenum>(outAtt->binding());

            const bool isColourBlit = entry._input._index != RT_DEPTH_ATTACHMENT_IDX;
            if ( isColourBlit )
            {
                DIVIDE_ASSERT( entry._output._index != RT_DEPTH_ATTACHMENT_IDX );
                if ( readBuffer != input->_activeReadBuffer )
                {
                    input->_activeReadBuffer = readBuffer;
                    glNamedFramebufferReadBuffer( input->_framebufferHandle, readBuffer );
                    readBufferDirty = true;
                }

                if (output->_colourBuffers._glSlot[0] != writeBuffer)
                {
                    output->_colourBuffers._glSlot[0] = writeBuffer;
                    output->_colourBuffers._dirty = true;
                    glNamedFramebufferDrawBuffers( output->_framebufferHandle,
                                                   (GLsizei)output->_colourBuffers._glSlot.size(),
                                                   output->_colourBuffers._glSlot.data() );
                }
            }

            bool blitted = false, inputDirty = false, outputDirty = false;;
            U16 layerCount = entry._layerCount;
            if ( IsCubeTexture( inAtt->texture()->descriptor().texType() ) )
            {
                layerCount *= 6u;
            }

            for ( U8 layer = 0u; layer < layerCount; ++layer )
            {
                for ( U8 mip = 0u; mip < entry._mipCount; ++mip )
                {
                    if ( preparetAttachments( input, entry._input._index, entry._input._layerOffset + layer, entry._input._mipOffset + mip ) )
                    {
                        inputDirty = true;
                    }

                    if ( preparetAttachments( output, entry._output._index, entry._output._layerOffset + layer, entry._output._mipOffset + mip ) )
                    {
                        outputDirty = true;
                    }
                    

                    glBlitNamedFramebuffer( input->_framebufferHandle,
                                            output->_framebufferHandle,
                                            0, 0,
                                            inputDim.width, inputDim.height,
                                            0, 0,
                                            outputDim.width, outputDim.height,
                                            isColourBlit ? GL_COLOR_BUFFER_BIT : GL_DEPTH_BUFFER_BIT,
                                            GL_NEAREST );

                    _context.registerDrawCall();
                    blitted = true;
                }
            }

            if ( inputDirty )
            {
                input->setDefaultAttachmentBinding( inAtt );
            }
            if ( outputDirty )
            {
                output->setDefaultAttachmentBinding( outAtt );
            }
            if ( blitted )
            {
                QueueMipMapsRecomputation( outAtt );
            }
        }

        if ( readBufferDirty )
        {
            glNamedFramebufferReadBuffer( input->_framebufferHandle, GL_NONE );
            input->_activeReadBuffer = GL_NONE;
        }
    }

    void glFramebuffer::prepareBuffers( const RTDrawDescriptor& drawPolicy )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _previousPolicy._drawMask != drawPolicy._drawMask || _colourBuffers._dirty )
        {
            bool set = false;
            // handle colour buffers first
            const U8 count = std::min( to_base( RTColourAttachmentSlot::COUNT ), getAttachmentCount( RTAttachmentType::COLOUR ) );
            for ( U8 j = 0; j < to_base( RTColourAttachmentSlot::COUNT ); ++j )
            {
                GLenum temp = GL_NONE;
                if ( j < count )
                {
                    const RTColourAttachmentSlot slot = static_cast<RTColourAttachmentSlot>(j);
                    temp = GL_NONE;
                    if ( drawPolicy._drawMask[j] && usesAttachment(RTAttachmentType::COLOUR, slot) )
                    {
                        temp = static_cast<GLenum>(getAttachment( RTAttachmentType::COLOUR, slot )->binding());
                    }
                }

                if ( _colourBuffers._glSlot[j] != temp )
                {
                    _colourBuffers._glSlot[j] = temp;
                    set = true;
                }
            }

            if ( set )
            {
                glNamedFramebufferDrawBuffers( _framebufferHandle, to_base( RTColourAttachmentSlot::COUNT ), _colourBuffers._glSlot.data() );
            }

            _colourBuffers._dirty = false;
            queueCheckStatus();
        }
    }

    bool glFramebuffer::setDefaultAttachmentBinding( const RTAttachment_uptr& attachment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( attachment == nullptr ) [[unlikely]]
        {
            return false;
        }

        // All active attachments are enabled by default
        // We also draw to mip and layer 0 unless specified otherwise in the drawPolicy
        return toggleAttachment( attachment, AttachmentState::STATE_ENABLED, 0u, {._layer = 0u, ._cubeFace = 0u} );
    }

    void glFramebuffer::begin( const RTDrawDescriptor& drawPolicy, const RTClearDescriptor& clearPolicy )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const DrawLayerEntry srcDepthLayer = drawPolicy._writeLayers[RT_DEPTH_ATTACHMENT_IDX];
        DrawLayerEntry targetDepthLayer{};

        bool needLayeredDepth = (srcDepthLayer._layer != INVALID_INDEX && (srcDepthLayer._layer > 0u || srcDepthLayer._cubeFace > 0u));

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] )
            {
                continue;
            }

            if ( drawPolicy._writeLayers[i]._layer != INVALID_INDEX && (drawPolicy._writeLayers[i]._layer > 0u || drawPolicy._writeLayers[i]._cubeFace > 0u))
            {
                targetDepthLayer = drawPolicy._writeLayers[i];
                needLayeredDepth = true;
                break;
            }
        }

        DIVIDE_ASSERT(drawPolicy._mipWriteLevel != INVALID_INDEX);

        if ( _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] )
        {
            if ( needLayeredDepth )
            {
                drawToLayer(
                {
                    ._layer = srcDepthLayer._layer == INVALID_INDEX ? targetDepthLayer : srcDepthLayer,
                    ._mipLevel = drawPolicy._mipWriteLevel,
                    ._index = RT_DEPTH_ATTACHMENT_IDX
                });
            }
            else if ( drawPolicy._mipWriteLevel > 0u )
            {
                setMipLevelInternal( _attachments[RT_DEPTH_ATTACHMENT_IDX], drawPolicy._mipWriteLevel );
            }
            else
            {
                setDefaultAttachmentBinding( _attachments[RT_DEPTH_ATTACHMENT_IDX] );
            }
        }

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] )
            {
                continue;
            }

            if ( needLayeredDepth || (drawPolicy._writeLayers[i]._layer != INVALID_INDEX && (drawPolicy._writeLayers[i]._layer > 0u || drawPolicy._writeLayers[i]._cubeFace > 0u)) )
            {
                drawToLayer(
                {
                    ._layer = drawPolicy._writeLayers[i]._layer == INVALID_INDEX ? targetDepthLayer : drawPolicy._writeLayers[i],
                    ._mipLevel = drawPolicy._mipWriteLevel,
                    ._index = i
                });
            }
            else if ( drawPolicy._mipWriteLevel > 0u )
            {
                setMipLevelInternal( _attachments[i], drawPolicy._mipWriteLevel );
            }
            else
            {
                setDefaultAttachmentBinding( _attachments[i] );
            }
        }

        prepareBuffers( drawPolicy );

        if constexpr ( Config::Build::IS_DEBUG_BUILD )
        {
            checkStatus();
        }
        else
        {
            _statusCheckQueued = false;
        }

        // Set the viewport
        _prevViewport.set( _context.activeViewport() );
        _context.setViewport( 0, 0, to_I32( getWidth() ), to_I32( getHeight() ) );

        clear( clearPolicy );

        /// Set the depth range
        GL_API::GetStateTracker().setDepthRange( _descriptor._depthRange.min, _descriptor._depthRange.max );
        _context.setDepthRange( _descriptor._depthRange );

        // Memorize the current draw policy to speed up later calls
        _previousPolicy = drawPolicy;
    }

    void glFramebuffer::end()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ) + 1u; ++i )
        {
            QueueMipMapsRecomputation( _attachments[i] );
        }

    }

    void glFramebuffer::QueueMipMapsRecomputation( const RTAttachment_uptr& attachment )
    {
        if ( attachment == nullptr )
        {
            return;
        }

        const Texture_ptr& texture = attachment->texture();
        if ( texture != nullptr && texture->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO )
        {
            glGenerateTextureMipmap( static_cast<glTexture*>(texture.get())->textureHandle() );
        }
    }

    void glFramebuffer::clear( const RTClearDescriptor& descriptor )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        {
            PROFILE_SCOPE( "Clear Colour Attachments", Profiler::Category::Graphics );
            for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
            {
                if ( !_attachmentsUsed[i] || !descriptor[i]._enabled )
                {
                    continue;
                }

                RTAttachment* att = getAttachment( RTAttachmentType::COLOUR, static_cast<RTColourAttachmentSlot>(i) );
                DIVIDE_ASSERT( att != nullptr, "glFramebuffer::error: Invalid clear target specified!" );

                const U32 binding = att->binding();
                if ( static_cast<GLenum>(binding) != GL_NONE )
                {
                    const GLint buffer = static_cast<GLint>(binding - static_cast<GLint>(GL_COLOR_ATTACHMENT0));
                    const FColour4& colour = descriptor[i]._colour;
                    if ( att->texture()->descriptor().normalized() )
                    {
                        glClearNamedFramebufferfv( _framebufferHandle, GL_COLOR, buffer, colour._v );
                    }
                    else
                    {
                        switch ( att->texture()->descriptor().dataType() )
                        {
                            case GFXDataFormat::FLOAT_16:
                            case GFXDataFormat::FLOAT_32:
                            {
                                glClearNamedFramebufferfv( _framebufferHandle, GL_COLOR, buffer, colour._v );
                            } break;

                            case GFXDataFormat::SIGNED_BYTE:
                            case GFXDataFormat::SIGNED_SHORT:
                            case GFXDataFormat::SIGNED_INT:
                            {
                                static vec4<I32> clearColour;
                                clearColour.set( FLOAT_TO_CHAR_SNORM( colour.r ),
                                                 FLOAT_TO_CHAR_SNORM( colour.g ),
                                                 FLOAT_TO_CHAR_SNORM( colour.b ),
                                                 FLOAT_TO_CHAR_SNORM( colour.a ) );
                                glClearNamedFramebufferiv( _framebufferHandle, GL_COLOR, buffer, clearColour._v );
                            } break;

                            default:
                            {
                                static vec4<U32> clearColour;
                                clearColour.set( FLOAT_TO_CHAR_UNORM( colour.r ),
                                                 FLOAT_TO_CHAR_UNORM( colour.g ),
                                                 FLOAT_TO_CHAR_UNORM( colour.b ),
                                                 FLOAT_TO_CHAR_UNORM( colour.a ) );
                                glClearNamedFramebufferuiv( _framebufferHandle, GL_COLOR, buffer, clearColour._v );
                            } break;
                        }
                    }
                    _context.registerDrawCall();
                }
            }
        }

        if ( _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] && descriptor[RT_DEPTH_ATTACHMENT_IDX]._enabled )
        {
            PROFILE_SCOPE( "Clear Depth", Profiler::Category::Graphics );
            glClearNamedFramebufferfv( _framebufferHandle, GL_DEPTH, 0, &descriptor[RT_DEPTH_ATTACHMENT_IDX]._colour.r);
            _context.registerDrawCall();
        }
    }

    void glFramebuffer::drawToLayer( const RTDrawLayerParams& params )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( params._index == INVALID_INDEX )
        {
            return;
        }

        const RTAttachment_uptr& att = _attachments[params._index];

        const GLenum textureType = GLUtil::internalTextureType( att->texture()->descriptor().texType(), att->texture()->descriptor().msaaSamples() );
        // only for array textures (it's better to simply ignore the command if the format isn't supported (debugging reasons)
        if ( textureType != GL_TEXTURE_2D_ARRAY &&
             textureType != GL_TEXTURE_CUBE_MAP_ARRAY &&
             textureType != GL_TEXTURE_2D_MULTISAMPLE_ARRAY )
        {
            DIVIDE_UNEXPECTED_CALL();
            return;
        }

        if (_attachmentsUsed[params._index])
        {
            if ( params._index != RT_DEPTH_ATTACHMENT_IDX )
            {
                const BindingState& state = getAttachmentState( static_cast<GLenum>(att->binding()) );
                toggleAttachment( att, state._attState, params._mipLevel, params._layer);

            }
            else if ( _isLayeredDepth )
            {
                const BindingState& state = getAttachmentState( static_cast<GLenum>(_attachments[RT_DEPTH_ATTACHMENT_IDX]->binding()) );
                toggleAttachment( _attachments[RT_DEPTH_ATTACHMENT_IDX], state._attState, params._mipLevel, params._layer );
            }
        }
    }

    bool glFramebuffer::setMipLevelInternal( const RTAttachment_uptr& attachment, U16 writeLevel )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( attachment != nullptr )
        {
            const BindingState& state = getAttachmentState( static_cast<GLenum>(attachment->binding()) );
            return toggleAttachment( attachment, state._attState, writeLevel, state._layer );
        }

        return false;
    }

    void glFramebuffer::setMipLevel( const U16 writeLevel )
    {
        if ( writeLevel == U16_MAX )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        bool changedMip = false;
        bool needsAttachmentDisabled = false;

        changedMip = setMipLevelInternal( _attachments[RT_DEPTH_ATTACHMENT_IDX], writeLevel ) || changedMip;
        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            changedMip = setMipLevelInternal( _attachments[i], writeLevel ) || changedMip;
        }

        if ( changedMip && needsAttachmentDisabled )
        {
            for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ) + 1; ++i )
            {
                const BindingState& state = getAttachmentState( static_cast<GLenum>(_attachments[i]->binding()) );

                if ( state._levelOffset != writeLevel )
                {
                    toggleAttachment( _attachments[i], AttachmentState::STATE_DISABLED, state._levelOffset, state._layer );
                }
            }
        }
    }

    void glFramebuffer::setAttachmentState( const GLenum binding, const BindingState state )
    {
        _attachmentState[ColorAttachmentToIndex( binding )] = state;
    }

    glFramebuffer::BindingState glFramebuffer::getAttachmentState( const GLenum binding ) const
    {
        return _attachmentState[ColorAttachmentToIndex( binding )];
    }

    void glFramebuffer::queueCheckStatus() noexcept
    {
        _statusCheckQueued = enableAttachmentChangeValidation();
    }

    bool glFramebuffer::checkStatus()
    {
        if ( !_statusCheckQueued )
        {
            return true;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _statusCheckQueued = false;
        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            // check FB status
            const GLenum status = glCheckNamedFramebufferStatus( _framebufferHandle, GL_FRAMEBUFFER );
            if ( status == GL_FRAMEBUFFER_COMPLETE )
            {
                return true;
            }

            switch ( status )
            {
                case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_ATTACHMENT_INCOMPLETE" ) ) );
                    return false;
                }
                case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_NO_IMAGE" ) ) );
                    return false;
                }
                case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_INCOMPLETE_DRAW_BUFFER" ) ) );
                    return false;
                }
                case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_INCOMPLETE_READ_BUFFER" ) ) );
                    return false;
                }
                case GL_FRAMEBUFFER_UNSUPPORTED:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_UNSUPPORTED" ) ) );
                    return false;
                }
                case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_INCOMPLETE_MULTISAMPLE" ) ) );
                    return false;
                }
                case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_INCOMPLETE_LAYER_TARGETS" ) ) );
                    return false;
                }
                case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_DIMENSIONS" ) ) );
                    return false;
                }
                case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_RT_FORMAT" ) ) );
                    return false;
                }
                default:
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_UNKNOWN" ) ) );
                } break;
            };
        }

        DIVIDE_ASSERT( !GL_API::GetStateTracker().assertOnAPIError());

        return false;
    }

};  // namespace Divide
