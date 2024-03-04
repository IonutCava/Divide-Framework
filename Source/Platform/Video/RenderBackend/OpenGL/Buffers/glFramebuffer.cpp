

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
    bool operator==( const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs ) noexcept
    {
        return lhs._attState == rhs._attState &&
               lhs._layer._layer == rhs._layer._layer &&
               lhs._layer._cubeFace == rhs._layer._cubeFace &&
               lhs._levelOffset == rhs._levelOffset &&
               lhs._layeredRendering == rhs._layeredRendering;
    }

    bool operator!=( const glFramebuffer::BindingState& lhs, const glFramebuffer::BindingState& rhs ) noexcept
    {
        return lhs._attState != rhs._attState ||
               lhs._layer._layer != rhs._layer._layer ||
               lhs._layer._cubeFace != rhs._layer._cubeFace ||
               lhs._levelOffset != rhs._levelOffset ||
               lhs._layeredRendering != rhs._layeredRendering;
    }

    glFramebuffer::glFramebuffer( GFXDevice& context, const RenderTargetDescriptor& descriptor )
        : RenderTarget( context, descriptor )
        , _debugMessage(("Render Target: [ " + name() + " ]").c_str())
    {
        glCreateFramebuffers( 1, &_framebufferHandle );

        DIVIDE_ASSERT( (_framebufferHandle != 0 && _framebufferHandle != GL_NULL_HANDLE), "glFramebuffer error: Tried to bind an invalid framebuffer!" );

        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            // label this FB to be able to tell that it's internally created and nor from a 3rd party lib
            glObjectLabel( GL_FRAMEBUFFER,
                           _framebufferHandle,
                           -1,
                           (name() + "_RENDER").c_str());
        }

        // Everything disabled so that the initial "begin" will override this
        _previousPolicy._drawMask.fill(false);
        _attachmentState.resize( GFXDevice::GetDeviceInformation()._maxRTColourAttachments + 1u + 1u ); //colours + depth-stencil
    }

    glFramebuffer::~glFramebuffer()
    {
        GL_API::DeleteFramebuffers( 1, &_framebufferHandle );

        if ( _framebufferResolveHandle != GL_NULL_HANDLE )
        {
            GL_API::DeleteFramebuffers( 1, &_framebufferResolveHandle );
        }
    }

    bool glFramebuffer::initAttachment( RTAttachment* att, const RTAttachmentType type, const RTColourAttachmentSlot slot )
    {
        if ( !RenderTarget::initAttachment( att, type, slot ) )
        {
            return false;
        }
         
        if ( att->_descriptor._externalAttachment == nullptr && att->resolvedTexture()->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::AUTO )
        {
            // We do this here to avoid any undefined data if we use this attachment as a texture before we actually draw to it
            glGenerateTextureMipmap( static_cast<glTexture*>(att->resolvedTexture().get())->textureHandle() );
        }

        // Find the appropriate binding point
        U32 binding = to_U32( GL_COLOR_ATTACHMENT0 ) + to_base( slot );
        if ( type == RTAttachmentType::DEPTH || type == RTAttachmentType::DEPTH_STENCIL )
        {
            binding = type == RTAttachmentType::DEPTH ? to_U32( GL_DEPTH_ATTACHMENT ) : to_U32( GL_DEPTH_STENCIL_ATTACHMENT );
            // Most of these aren't even valid, but hey, doesn't hurt to check
            _isLayeredDepth = SupportsZOffsetTexture( att->resolvedTexture()->descriptor().texType() );
        }

        att->binding( binding );
        _attachmentState[to_base(slot)] = {};

        return true;
    }

    bool glFramebuffer::toggleAttachment( const U8 attachmentIdx, const AttachmentState state, const U16 levelOffset, const DrawLayerEntry layerOffset, bool layeredRendering )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const RTAttachment_uptr& attachment = _attachments[ attachmentIdx ];
        const Texture_ptr& tex = attachment->renderTexture();
        if ( tex == nullptr )
        {
            return false;
        }

        if ( tex->depth() == 1u )
        {
            DIVIDE_ASSERT( layerOffset._layer == 0u );
            if ( !IsCubeTexture( tex->descriptor().texType() ) || layerOffset._cubeFace == 0u )
            {
                layeredRendering = true;
            }
        }

        const BindingState bState
        {
            ._layer = layerOffset,
            ._levelOffset = levelOffset,
            ._layeredRendering = layeredRendering,
            ._attState = state
        };

        // Compare with old state
        if ( bState != _attachmentState[attachmentIdx] )
        {
            const GLenum binding = static_cast<GLenum>(attachment->binding());

            if ( state == AttachmentState::STATE_DISABLED )
            {
                glNamedFramebufferTexture( _framebufferHandle, binding, 0u, 0u );
                if ( _attachmentsAutoResolve[attachmentIdx] )
                {
                    glNamedFramebufferTexture( _framebufferResolveHandle, binding, 0u, 0u );
                }
            }
            else
            {
                DIVIDE_ASSERT( bState._layer._layer < tex->depth() && bState._levelOffset < tex->mipCount());

                const GLuint handle = static_cast<glTexture*>(tex.get())->textureHandle();
                if ( bState._layer._layer == 0u && bState._layer._cubeFace == 0u && layeredRendering )
                {
                    glNamedFramebufferTexture( _framebufferHandle, binding, handle, bState._levelOffset);
                    if ( _attachmentsAutoResolve[attachmentIdx] )
                    {
                        glNamedFramebufferTexture( _framebufferResolveHandle, binding, static_cast<glTexture*>(attachment->resolvedTexture().get())->textureHandle(), bState._levelOffset );
                    }
                }
                else if ( IsCubeTexture( tex->descriptor().texType() ) )
                {
                    glNamedFramebufferTextureLayer( _framebufferHandle, binding, handle, bState._levelOffset, bState._layer._cubeFace + (bState._layer._layer * 6u) );
                    if ( _attachmentsAutoResolve[attachmentIdx] )
                    {
                        glNamedFramebufferTextureLayer( _framebufferResolveHandle, binding, static_cast<glTexture*>(attachment->resolvedTexture().get())->textureHandle(), bState._levelOffset, bState._layer._cubeFace + (bState._layer._layer * 6u) );
                    }
                }
                else
                {
                    assert(bState._layer._cubeFace == 0u);
                    glNamedFramebufferTextureLayer( _framebufferHandle, binding, handle, bState._levelOffset, bState._layer._layer );
                    if ( _attachmentsAutoResolve[attachmentIdx] )
                    {
                        glNamedFramebufferTextureLayer( _framebufferResolveHandle, binding, static_cast<glTexture*>(attachment->resolvedTexture().get())->textureHandle(), bState._levelOffset, bState._layer._layer );
                    }
                }
            }

            _statusCheckQueued = true;
            _attachmentState[attachmentIdx] = bState;
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

        bool needsAutoResolve = false;
        for ( U8 i = 0u; i < RT_MAX_ATTACHMENT_COUNT; ++i )
        {
            if ( _attachmentsAutoResolve[i] )
            {
                needsAutoResolve = true;
                break;
            }
        }

        if ( needsAutoResolve && _framebufferResolveHandle  == GL_NULL_HANDLE )
        {

            glCreateFramebuffers( 1, &_framebufferResolveHandle );
            if constexpr ( Config::ENABLE_GPU_VALIDATION )
            {
                glObjectLabel( GL_FRAMEBUFFER,
                                _framebufferResolveHandle,
                                -1,
                                (name() + "_RESOLVE").c_str() );
            }
        }
        else if ( !needsAutoResolve && _framebufferResolveHandle != GL_NULL_HANDLE )
        {
            GL_API::DeleteFramebuffers( 1, &_framebufferResolveHandle );
            _framebufferResolveHandle = GL_NULL_HANDLE;
        }

        for ( U8 i = 0u; i < RT_MAX_ATTACHMENT_COUNT; ++i )
        {
            if ( !_attachmentsUsed[i] )
            {
                continue;
            }

            toggleAttachment( i, AttachmentState::STATE_ENABLED, 0u, { ._layer = 0u, ._cubeFace = 0u }, true );
        }

        // Setup draw buffers
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

        GLuint inputHandle = input->_framebufferHandle;
        if ( input->_framebufferResolveHandle != GL_NULL_HANDLE )
        {
            inputHandle = input->_framebufferResolveHandle;
        }

        PROFILE_TAG( "Input_Width", inputDim.width );
        PROFILE_TAG( "Input_Height", inputDim.height );
        PROFILE_TAG( "Output_Width", outputDim.width );
        PROFILE_TAG( "Output_Height",outputDim.height );

        // When reading/writing from/to an FBO, we need to make sure it is complete and doesn't throw a GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS.
        // An easy way to achieve this is to disable attachments we don't need to write to
        const auto prepareAttachments = []( glFramebuffer* fbo, const U16 attIndex, const U16 layer, const U16 mip )
        {
            bool ret = false;
            for ( U8 i = 0u; i < RT_MAX_ATTACHMENT_COUNT; ++i )
            {
                if ( !fbo->_attachmentsUsed[i] )
                {
                    continue;
                }

                if (fbo->toggleAttachment( i, i == attIndex ? AttachmentState::STATE_ENABLED : AttachmentState::STATE_DISABLED, mip, {layer, 0u}, false ) )
                {
                    ret = true;
                }
            }

            return ret;
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
                PROFILE_SCOPE( "Prepare RW Buffers", Profiler::Category::Graphics );

                DIVIDE_ASSERT( entry._output._index != RT_DEPTH_ATTACHMENT_IDX );
                if ( readBuffer != input->_activeReadBuffer )
                {
                    input->_activeReadBuffer = readBuffer;
                    glNamedFramebufferReadBuffer( inputHandle, readBuffer );
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
            DIVIDE_ASSERT( layerCount != U16_MAX && entry._mipCount != U16_MAX);
            if ( IsCubeTexture( inAtt->resolvedTexture()->descriptor().texType() ) )
            {
                layerCount *= 6u;
            }
            
            {
                PROFILE_SCOPE( "Blit layers", Profiler::Category::Graphics );
                for ( U8 layer = 0u; layer < layerCount; ++layer )
                {
                    PROFILE_TAG("Layer", layer);

                    for ( U8 mip = 0u; mip < entry._mipCount; ++mip )
                    {
                        PROFILE_SCOPE( "Blit Mip", Profiler::Category::Graphics );
                        PROFILE_TAG("Mip", mip);

                        {
                            PROFILE_SCOPE( "Prepare Attachments Input", Profiler::Category::Graphics );
                            if ( prepareAttachments( input, entry._input._index, entry._input._layerOffset + layer, entry._input._mipOffset + mip ) )
                            {
                                inputDirty = true;
                            }
                        }
                        {
                            PROFILE_SCOPE( "Prepare Attachments Output", Profiler::Category::Graphics );
                            if ( prepareAttachments( output, entry._output._index, entry._output._layerOffset + layer, entry._output._mipOffset + mip ) )
                            {
                                outputDirty = true;
                            }
                        }

                        glBlitNamedFramebuffer( inputHandle,
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
            }

            if ( inputDirty )
            {
                PROFILE_SCOPE( "Reset Input Attachments", Profiler::Category::Graphics );
                DIVIDE_ASSERT( input->_attachmentsUsed[entry._input._index] );
                input->toggleAttachment( entry._input._index, AttachmentState::STATE_ENABLED, 0u, { ._layer = 0u, ._cubeFace = 0u }, true );
            }
            if ( outputDirty )
            {
                PROFILE_SCOPE( "Reset Output Attachments", Profiler::Category::Graphics );
                DIVIDE_ASSERT( output->_attachmentsUsed[entry._output._index] );
                output->toggleAttachment( entry._output._index, AttachmentState::STATE_ENABLED, 0u, { ._layer = 0u, ._cubeFace = 0u }, true );
            }
            if ( blitted )
            {
                QueueMipMapsRecomputation( outAtt );
            }
        }

        if ( readBufferDirty )
        {
            glNamedFramebufferReadBuffer( inputHandle, GL_NONE );
            input->_activeReadBuffer = GL_NONE;
        }
    }

    void glFramebuffer::prepareBuffers( const RTDrawDescriptor& drawPolicy )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _colourBuffers._dirty || _previousPolicy._drawMask != drawPolicy._drawMask )
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
            _statusCheckQueued = true;
        }
    }

    void glFramebuffer::begin( const RTDrawDescriptor& drawPolicy, const RTClearDescriptor& clearPolicy )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );


        DrawLayerEntry targetDepthLayer{};

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( _attachmentsUsed[i] )
            {
                if ( drawPolicy._writeLayers[i]._layer != INVALID_INDEX && (drawPolicy._writeLayers[i]._layer > 0u || drawPolicy._writeLayers[i]._cubeFace > 0u))
                {
                    targetDepthLayer = drawPolicy._writeLayers[i];
                    break;
                }
            }
        }
        for ( U8 i = 0u; i < RT_MAX_ATTACHMENT_COUNT; ++i )
        {
            if ( _attachmentsUsed[i] )
            {
                _previousDrawLayers[i] = drawPolicy._writeLayers[i]._layer == INVALID_INDEX ? targetDepthLayer : drawPolicy._writeLayers[i];
                toggleAttachment( i, AttachmentState::STATE_ENABLED, drawPolicy._mipWriteLevel, _previousDrawLayers[i], drawPolicy._layeredRendering);
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
        _context.setViewport( 0, 0, to_I32( getWidth() ), to_I32( getHeight() ) );

        clear( clearPolicy );
        _previousPolicy = drawPolicy;
    }

    void glFramebuffer::end( const RTTransitionMask& mask )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        resolve( mask );

        for ( U8 i = 0u; i < RT_MAX_ATTACHMENT_COUNT; ++i )
        {
            QueueMipMapsRecomputation( _attachments[i] );
        }

    }

    void glFramebuffer::resolve(const RTTransitionMask& mask)
    {
        if ( _descriptor._msaaSamples == 0u || !_previousPolicy._autoResolveMSAA )
        {
            return;
        }

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] || !_attachmentsAutoResolve[i] )
            {
                continue;
            }

            if ( _previousPolicy._drawMask[i] && mask[i] )
            {
                toggleAttachment( i, AttachmentState::STATE_ENABLED, _previousPolicy._mipWriteLevel, _previousDrawLayers[i], _previousPolicy._layeredRendering);

                const GLenum rwBuffer = static_cast<GLenum>(_attachments[i]->binding());
                glNamedFramebufferReadBuffer( _framebufferHandle, rwBuffer );
                glNamedFramebufferDrawBuffers( _framebufferResolveHandle, 1u, &rwBuffer);

                glBlitNamedFramebuffer( _framebufferHandle,
                                        _framebufferResolveHandle,
                                        0, 0,
                                        _descriptor._resolution.width, _descriptor._resolution.height,
                                        0, 0,
                                        _descriptor._resolution.width, _descriptor._resolution.height,
                                        GL_COLOR_BUFFER_BIT,
                                        GL_NEAREST );
            }
        }

        if ( _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] && _attachmentsAutoResolve[RT_DEPTH_ATTACHMENT_IDX] && mask[RT_DEPTH_ATTACHMENT_IDX] )
        {
            toggleAttachment( RT_DEPTH_ATTACHMENT_IDX, AttachmentState::STATE_ENABLED, _previousPolicy._mipWriteLevel, _previousDrawLayers[RT_DEPTH_ATTACHMENT_IDX], _previousPolicy._layeredRendering );

            glBlitNamedFramebuffer( _framebufferHandle,
                                    _framebufferResolveHandle,
                                    0, 0,
                                    _descriptor._resolution.width, _descriptor._resolution.height,
                                    0, 0,
                                    _descriptor._resolution.width, _descriptor._resolution.height,
                                    GL_DEPTH_BUFFER_BIT,
                                    GL_NEAREST );
        }


        glNamedFramebufferReadBuffer( _framebufferHandle, GL_NONE );
        _activeReadBuffer = GL_NONE;
    }

    void glFramebuffer::QueueMipMapsRecomputation( const RTAttachment_uptr& attachment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( attachment == nullptr )
        {
            return;
        }

        const Texture_ptr& texture = attachment->resolvedTexture();
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
                    if ( IsNormalizedTexture(att->renderTexture()->descriptor().packing()) )
                    {
                        glClearNamedFramebufferfv( _framebufferHandle, GL_COLOR, buffer, colour._v );
                    }
                    else
                    {
                        switch ( att->renderTexture()->descriptor().dataType() )
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
                                thread_local vec4<I32> clearColour {
                                    FLOAT_TO_CHAR_SNORM( colour.r ),
                                    FLOAT_TO_CHAR_SNORM( colour.g ),
                                    FLOAT_TO_CHAR_SNORM( colour.b ),
                                    FLOAT_TO_CHAR_SNORM( colour.a ) 
                                };

                                glClearNamedFramebufferiv( _framebufferHandle, GL_COLOR, buffer, clearColour._v );
                            } break;

                            default:
                            {
                                thread_local vec4<U32> clearColour {
                                     FLOAT_TO_CHAR_UNORM( colour.r ),
                                     FLOAT_TO_CHAR_UNORM( colour.g ),
                                     FLOAT_TO_CHAR_UNORM( colour.b ),
                                     FLOAT_TO_CHAR_UNORM( colour.a ) 
                                };
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

            GL_API::GetStateTracker().setDepthWrite(true);
            const FColour4& clearColour = descriptor[RT_DEPTH_ATTACHMENT_IDX]._colour;
            if ( _attachments[RT_DEPTH_ATTACHMENT_IDX]->_descriptor._type == RTAttachmentType::DEPTH_STENCIL )
            {
                glClearNamedFramebufferfi( _framebufferHandle, GL_DEPTH_STENCIL, 0, clearColour.r, to_I32( clearColour.g) );
            }
            else
            {
                glClearNamedFramebufferfv( _framebufferHandle, GL_DEPTH, 0, &clearColour.r );
            }
            _context.registerDrawCall();
        }
    }

    bool glFramebuffer::setMipLevelInternal( const U8 attachmentIdx, U16 writeLevel )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _attachmentsUsed[attachmentIdx] )
        {
            const BindingState& state = _attachmentState[attachmentIdx];
            return toggleAttachment( attachmentIdx, state._attState, writeLevel, state._layer, state._layeredRendering );
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

        changedMip = setMipLevelInternal( RT_DEPTH_ATTACHMENT_IDX, writeLevel ) || changedMip;
        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            changedMip = setMipLevelInternal( i, writeLevel ) || changedMip;
        }

        if ( changedMip && needsAttachmentDisabled )
        {
            for ( U8 i = 0u; i < RT_MAX_ATTACHMENT_COUNT; ++i )
            {
                const BindingState& state = _attachmentState[i];

                if ( state._levelOffset != writeLevel )
                {
                    toggleAttachment( i, AttachmentState::STATE_DISABLED, state._levelOffset, state._layer, state._layeredRendering );
                }
            }
        }
    }

    bool glFramebuffer::checkStatus()
    {
        if ( !_statusCheckQueued )
        {
            return true;
        }
        _statusCheckQueued = false;
        if ( _framebufferResolveHandle != GL_NULL_HANDLE && !checkStatusInternal( _framebufferResolveHandle ) )
        {
            return false;
        }

        return checkStatusInternal( _framebufferHandle );
    }

    bool glFramebuffer::checkStatusInternal( const GLuint handle )
    {
        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

            // check FB status
            const GLenum status = glCheckNamedFramebufferStatus( handle, GL_FRAMEBUFFER );
            if ( status == GL_FRAMEBUFFER_COMPLETE )
            {
                return true;
            }

            switch ( status )
            {
                case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_ATTACHMENT_INCOMPLETE" ) );
                } break;
                case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_NO_IMAGE" ) );
                } break;
                case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_INCOMPLETE_DRAW_BUFFER" ) );
                } break;
                case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_INCOMPLETE_READ_BUFFER" ) );
                } break;
                case GL_FRAMEBUFFER_UNSUPPORTED:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_UNSUPPORTED" ) );
                } break;
                case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_INCOMPLETE_MULTISAMPLE" ) );
                } break;
                case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_INCOMPLETE_LAYER_TARGETS" ) );
                } break;
                case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_DIMENSIONS" ) );
                } break;
                case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RT_FORMAT" ) );
                } break;
                default:
                {
                    Console::errorfn( LOCALE_STR( "ERROR_UNKNOWN" ) );
                } break;
            }

            DIVIDE_ASSERT( !(*GL_API::GetStateTracker()._assertOnAPIError));

            return false;
        }
        else
        {
            return true;
        }
    }

};  // namespace Divide
