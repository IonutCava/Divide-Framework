

#include "Headers/glStateTracker.h"
#include "Headers/GLWrapper.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

#include "Utility/Headers/Localization.h"

namespace Divide
{

    namespace
    {
        // GL_NONE returns the count
        FORCE_INLINE gl46core::GLint GetBufferTargetIndex( const gl46core::GLenum target ) noexcept
        {
            // Select the appropriate index in the array based on the buffer target
            switch ( target )
            {
                case gl46core::GL_TEXTURE_BUFFER: return 0;
                case gl46core::GL_UNIFORM_BUFFER: return 1;
                case gl46core::GL_SHADER_STORAGE_BUFFER: return 2;
                case gl46core::GL_PIXEL_UNPACK_BUFFER: return 3;
                case gl46core::GL_DRAW_INDIRECT_BUFFER: return 4;
                case gl46core::GL_ARRAY_BUFFER: return 5;
                case gl46core::GL_PARAMETER_BUFFER: return 6;
                case gl46core::GL_ELEMENT_ARRAY_BUFFER: return 7;
                case gl46core::GL_PIXEL_PACK_BUFFER: return 8;
                case gl46core::GL_TRANSFORM_FEEDBACK_BUFFER: return 9;
                case gl46core::GL_COPY_READ_BUFFER: return 10;
                case gl46core::GL_COPY_WRITE_BUFFER: return 11;
                case gl46core::GL_QUERY_BUFFER: return 12;
                case gl46core::GL_ATOMIC_COUNTER_BUFFER: return 13;
                case gl46core::GL_NONE: return 14;
                default: break;
            };

            DIVIDE_UNEXPECTED_CALL();
            return -1;
        }

    }; //namespace 

    void GLStateTracker::setDefaultState()
    {
        _activeState = {};
        for (auto& scope : _debugScope)
        {
            scope = {};
        }
        _debugScopeDepth = 0u;
        _attributeHash = 0u;
        _activePipeline = nullptr;
        _activeShaderProgram = nullptr;
        _activeTopology = PrimitiveTopology::COUNT;
        _activeRenderTarget = nullptr;
        _activeRenderTargetID = INVALID_RENDER_TARGET_ID;
        _activeRenderTargetDimensions = {1u, 1u};
        _activeFBID[0] = _activeFBID[1] = _activeFBID[2] = GL_NULL_HANDLE;
        _activeVAOIB = GL_NULL_HANDLE;
        _drawIndirectBufferOffset = 0u;
        _packAlignment = {};
        _unpackAlignment = {};
        _activeShaderProgramHandle = 0u;
        _activeShaderPipelineHandle = 0u;
        _alphaToCoverageEnabled = false;
        _blendPropertiesGlobal = {};
        _blendEnabledGlobal = gl46core::GL_FALSE;
        _currentBindConfig = {};
        _blendProperties.clear();
        _blendEnabled.clear();
        _blendColour = { 0, 0, 0, 0 };
        _activeViewport = { -1, -1, -1, -1 };
        _activeScissor = { -1, -1, -1, -1 };
        _activeClearColour = DefaultColours::BLACK_U8;
        _clearDepthValue = 1.f;
        _imageBoundMap.clear();

        _vaoBufferData = {};
        _endFrameFences = {};
        _lastSyncedFrameNumber = 0u;

        _blendPropertiesGlobal.blendSrc( BlendProperty::ONE );
        _blendPropertiesGlobal.blendDest( BlendProperty::ZERO );
        _blendPropertiesGlobal.blendOp( BlendOperation::ADD );
        _blendPropertiesGlobal.enabled( false );

        _vaoBufferData.init( GFXDevice::GetDeviceInformation()._maxVertAttributeBindings );

        _imageBoundMap.resize( GFXDevice::GetDeviceInformation()._maxTextureUnits );

        _blendProperties.resize( GFXDevice::GetDeviceInformation()._maxRTColourAttachments, BlendingSettings() );
        _blendEnabled.resize( GFXDevice::GetDeviceInformation()._maxRTColourAttachments, gl46core::GL_FALSE );

        _activeBufferID = create_array<13, gl46core::GLuint>( GL_NULL_HANDLE );
        _textureBoundMap.fill( GL_NULL_HANDLE );
        _samplerBoundMap.fill( GL_NULL_HANDLE );
    }


    void GLStateTracker::setPrimitiveTopology( const PrimitiveTopology topology )
    {
        _activeTopology = topology;
    }

    void GLStateTracker::setVertexFormat( const AttributeMap& attributes, const size_t attributeHash )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _attributeHash == attributeHash)
        {
            NOP();
            //DebugBreak();
            return;
        }

        _attributeHash = attributeHash;

        // Update vertex attributes if needed (e.g. if offsets changed)
        for ( U8 idx = 0u; idx < to_base( AttribLocation::COUNT ); ++idx )
        {
            const AttributeDescriptor& descriptor = attributes._attributes[idx];

            if (descriptor != _currentAttributes[idx] )
            {
                if ( descriptor._dataType == GFXDataFormat::COUNT )
                {
                    gl46core::glDisableVertexAttribArray( idx );
                }
                else
                {
                    gl46core::glEnableVertexAttribArray( idx );
                    gl46core::glVertexAttribBinding( idx, descriptor._vertexBindingIndex );
                    const bool isIntegerType = descriptor._dataType != GFXDataFormat::FLOAT_16 && descriptor._dataType != GFXDataFormat::FLOAT_32;

                    if ( !isIntegerType || descriptor._normalized )
                    {
                        gl46core::glVertexAttribFormat( idx,
                                                        descriptor._componentsPerElement,
                                                        GLUtil::glDataFormatTable[to_U32( descriptor._dataType )],
                                                        descriptor._normalized ? gl46core::GL_TRUE : gl46core::GL_FALSE,
                                                        static_cast<gl46core::GLuint>(descriptor._strideInBytes) );
                    }
                    else
                    {
                        gl46core::glVertexAttribIFormat( idx,
                                                         descriptor._componentsPerElement,
                                                         GLUtil::glDataFormatTable[to_U32( descriptor._dataType )],
                                                         static_cast<gl46core::GLuint>(descriptor._strideInBytes) );
                    }
                }
                _currentAttributes[idx] = descriptor;
            }
        }

        for ( const VertexBinding& vertBinding : attributes._vertexBindings )
        {
            const bool perInstanceDivisor = !vertBinding._perVertexInputRate;
            if ( _vaoBufferData.instanceDivisorFlag( vertBinding._bufferBindIndex ) != perInstanceDivisor )
            {
                gl46core::glVertexBindingDivisor( vertBinding._bufferBindIndex, perInstanceDivisor ? 1u : 0u );
                _vaoBufferData.instanceDivisorFlag( vertBinding._bufferBindIndex, perInstanceDivisor );
            }
        }
    }

    GLStateTracker::BindResult GLStateTracker::setStateBlock( const RenderStateBlock& stateBlock )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Activate the new render state block in an rendering API dependent way
        if (activateStateBlock( stateBlock ))
        {
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    /// Pixel pack alignment is usually changed by textures, PBOs, etc
    bool GLStateTracker::setPixelPackAlignment( const PixelAlignment& pixelPackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Keep track if we actually affect any OpenGL state
        bool changed = false;
        if ( _packAlignment._alignment != pixelPackAlignment._alignment )
        {
            gl46core::glPixelStorei( gl46core::GL_PACK_ALIGNMENT, (gl46core::GLint)pixelPackAlignment._alignment );
            changed = true;
        }

        if ( _packAlignment._rowLength != pixelPackAlignment._rowLength )
        {
            gl46core::glPixelStorei( gl46core::GL_PACK_ROW_LENGTH, (gl46core::GLint)pixelPackAlignment._rowLength );
            changed = true;
        }

        if ( _packAlignment._skipRows != pixelPackAlignment._skipRows )
        {
            gl46core::glPixelStorei( gl46core::GL_PACK_SKIP_ROWS, (gl46core::GLint)pixelPackAlignment._skipRows );
            changed = true;
        }

        if ( _packAlignment._skipPixels != pixelPackAlignment._skipPixels )
        {
            gl46core::glPixelStorei( gl46core::GL_PACK_SKIP_PIXELS, (gl46core::GLint)pixelPackAlignment._skipPixels );
            changed = true;
        }

        if ( changed )
        {
            _packAlignment = pixelPackAlignment;
        }

        // We managed to change at least one entry
        return changed;
    }

    /// Pixel unpack alignment is usually changed by textures, PBOs, etc
    bool GLStateTracker::setPixelUnpackAlignment( const PixelAlignment& pixelUnpackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Keep track if we actually affect any OpenGL state
        bool changed = false;
        if ( _unpackAlignment._alignment != SIZE_MAX && _unpackAlignment._alignment != pixelUnpackAlignment._alignment )
        {
            gl46core::glPixelStorei( gl46core::GL_UNPACK_ALIGNMENT, (gl46core::GLint)pixelUnpackAlignment._alignment );
            changed = true;
        }

        if ( pixelUnpackAlignment._rowLength != SIZE_MAX && _unpackAlignment._rowLength != pixelUnpackAlignment._rowLength )
        {
            gl46core::glPixelStorei( gl46core::GL_UNPACK_ROW_LENGTH, (gl46core::GLint)pixelUnpackAlignment._rowLength );
            changed = true;
        }

        if ( pixelUnpackAlignment._skipRows != SIZE_MAX && _unpackAlignment._skipRows != pixelUnpackAlignment._skipRows )
        {
            gl46core::glPixelStorei( gl46core::GL_UNPACK_SKIP_ROWS, (gl46core::GLint)pixelUnpackAlignment._skipRows );
            changed = true;
        }

        if ( pixelUnpackAlignment._skipPixels != SIZE_MAX && _unpackAlignment._skipPixels != pixelUnpackAlignment._skipPixels )
        {
            gl46core::glPixelStorei( gl46core::GL_UNPACK_SKIP_PIXELS, (gl46core::GLint)pixelUnpackAlignment._skipPixels );
            changed = true;
        }

        if ( changed )
        {
            _unpackAlignment = pixelUnpackAlignment;
        }
        // We managed to change at least one entry
        return changed;
    }

    GLStateTracker::BindResult GLStateTracker::bindSamplers( const gl46core::GLubyte unitOffset,
                                                             const gl46core::GLuint samplerCount,
                                                             const gl46core::GLuint* const samplerHandles )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        BindResult result = BindResult::FAILED;

        if ( samplerCount > 0 && unitOffset + samplerCount < GFXDevice::GetDeviceInformation()._maxTextureUnits )
        {
            if ( samplerCount == 1 )
            {
                gl46core::GLuint& handle = _samplerBoundMap[unitOffset];
                const gl46core::GLuint targetHandle = samplerHandles ? samplerHandles[0] : 0u;
                if ( handle != targetHandle )
                {
                    gl46core::glBindSampler( unitOffset, targetHandle );
                    handle = targetHandle;
                    result = BindResult::JUST_BOUND;
                }
                result = BindResult::ALREADY_BOUND;
            }
            else
            {
                // Update bound map
                bool newBinding = false;
                for ( gl46core::GLubyte idx = 0u; idx < samplerCount; ++idx )
                {
                    const U8 slot = unitOffset + idx;
                    const gl46core::GLuint targetHandle = samplerHandles ? samplerHandles[idx] : 0u;
                    gl46core::GLuint& crtHandle = _samplerBoundMap[slot];
                    if ( crtHandle != targetHandle )
                    {
                        crtHandle = targetHandle;
                        newBinding = true;
                    }
                }

                if ( !newBinding )
                {
                    result = BindResult::ALREADY_BOUND;
                }
                else
                {
                    gl46core::glBindSamplers( unitOffset, samplerCount, samplerHandles );
                    result = BindResult::JUST_BOUND;
                }
            }
        }

        return result;
    }

    bool GLStateTracker::unbindTexture( [[maybe_unused]] const TextureType type, const gl46core::GLuint handle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        for ( U8 idx = 0u; idx < MAX_BOUND_TEXTURE_UNITS; ++idx )
        {
            if ( _textureBoundMap[idx] == handle )
            {
                _textureBoundMap[idx] = GL_NULL_HANDLE;
                gl46core::glBindTextureUnit( idx, 0u );
                DIVIDE_EXPECTED_CALL( bindSamplers( idx, 1, nullptr ) != BindResult::FAILED );

                return true;
            }
        }

        return false;
    }

    bool GLStateTracker::unbindTextures()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _textureBoundMap.fill( GL_NULL_HANDLE );
        _samplerBoundMap.fill( GL_NULL_HANDLE );

        gl46core::glBindTextures( 0u, GFXDevice::GetDeviceInformation()._maxTextureUnits - 1, nullptr );
        gl46core::glBindSamplers( 0u, GFXDevice::GetDeviceInformation()._maxTextureUnits - 1, nullptr );

        return true;
    }

    /// Bind a texture specified by a GL handle and GL type to the specified unit using the sampler object defined by hash value
    GLStateTracker::BindResult GLStateTracker::bindTexture( const gl46core::GLubyte unit, const gl46core::GLuint handle, const gl46core::GLuint samplerHandle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Fail if we specified an invalid unit. Assert instead of returning false because this might be related to a bad algorithm
        DIVIDE_ASSERT( unit < GFXDevice::GetDeviceInformation()._maxTextureUnits, "GLStates error: invalid texture unit specified as a texture binding slot!" );

        BindResult result = BindResult::ALREADY_BOUND;

        gl46core::GLuint& crtEntry = _textureBoundMap[unit];
        if ( crtEntry != handle )
        {
            crtEntry = handle;
            gl46core::glBindTextureUnit( unit, handle );
            result = BindResult::JUST_BOUND;
        }

        gl46core::GLuint& crtSampler = _samplerBoundMap[unit];
        if ( crtSampler != samplerHandle )
        {
            crtSampler = samplerHandle;
            gl46core::glBindSampler( unit, samplerHandle );
            result = BindResult::JUST_BOUND;
        }

        return result;
    }

    GLStateTracker::BindResult GLStateTracker::bindTextures( const gl46core::GLubyte unitOffset,
                                                             const gl46core::GLuint textureCount,
                                                             const gl46core::GLuint* const textureHandles,
                                                             const gl46core::GLuint* const samplerHandles )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( textureCount == 1 )
        {
            return bindTexture( unitOffset,
                                textureHandles == nullptr ? 0u : textureHandles[0],
                                samplerHandles == nullptr ? 0u : samplerHandles[0] );
        }
        BindResult result = BindResult::FAILED;

        if ( textureCount > 0u && unitOffset + textureCount < GFXDevice::GetDeviceInformation()._maxTextureUnits )
        {
            // Update bound map
            bool newBinding = false;
            for ( gl46core::GLubyte idx = 0u; idx < textureCount; ++idx )
            {
                const U8 slot = unitOffset + idx;
                const gl46core::GLuint targetHandle = textureHandles ? textureHandles[idx] : 0u;
                gl46core::GLuint& crtHandle = _textureBoundMap[slot];
                if ( crtHandle != targetHandle )
                {
                    crtHandle = targetHandle;
                    newBinding = true;
                }
            }

            if ( !newBinding )
            {
                result = BindResult::ALREADY_BOUND;
            }
            else
            {
                gl46core::glBindTextures( unitOffset, textureCount, textureHandles );
                result = BindResult::JUST_BOUND;
            }

            const BindResult samplerResult = bindSamplers( unitOffset, textureCount, samplerHandles );
            // Even if the textures are already bound, the samplers might differ (and that affects the result)
            if ( samplerResult == BindResult::FAILED || samplerResult == BindResult::JUST_BOUND )
            {
                result = samplerResult;
            }
        }

        return result;
    }

    GLStateTracker::BindResult GLStateTracker::bindTextureImage( const gl46core::GLubyte unit, const gl46core::GLuint handle, const gl46core::GLint level,
                                                                 const bool layered, const gl46core::GLint layer, const gl46core::GLenum access,
                                                                 const gl46core::GLenum format )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( handle != GL_NULL_HANDLE );

        const ImageBindSettings tempSettings = 
        {
            ._texture = handle,
            ._level = level,
            ._layer = layer,
            ._access = access,
            ._format = format,
            ._layered = layered ? gl46core::GL_TRUE : gl46core::GL_FALSE
        };

        ImageBindSettings& settings = _imageBoundMap[unit];
        if ( settings != tempSettings )
        {
            glBindImageTexture( unit, handle, level, layered ? gl46core::GL_TRUE : gl46core::GL_FALSE, layer, access, format );
            settings = tempSettings;
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    /// Single place to change buffer objects for every target available
    GLStateTracker::BindResult GLStateTracker::bindActiveBuffer( const gl46core::GLuint location, gl46core::GLuint bufferID, size_t offset, size_t stride )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const VAOBindings::BufferBindingParams& bindings = _vaoBufferData.bindingParams( location );
        const VAOBindings::BufferBindingParams currentParams
        {
            ._id = bufferID,
            ._offset = offset,
            ._stride = stride
        };

        if ( bindings != currentParams )
        {
            // Bind the specified buffer handle to the desired buffer target
            gl46core::glBindVertexBuffer( location, bufferID, static_cast<gl46core::GLintptr>(offset), static_cast<gl46core::GLsizei>(stride) );
            // Remember the new binding for future reference
            _vaoBufferData.bindingParams( location, currentParams );
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    GLStateTracker::BindResult GLStateTracker::bindActiveBuffers( const gl46core::GLuint location, const gl46core::GLsizei count, gl46core::GLuint* bufferIDs, gl46core::GLintptr* offsets, gl46core::GLsizei* strides )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        bool needsBind = false;
        for ( gl46core::GLsizei i = 0u; i < count; ++i )
        {
            const VAOBindings::BufferBindingParams& bindings = _vaoBufferData.bindingParams( i );
            const VAOBindings::BufferBindingParams currentParams
            {
                ._id = bufferIDs[i],
                ._offset = to_size(offsets[i]),
                ._stride = to_size(strides[i])
            };
            if ( bindings != currentParams )
            {
                _vaoBufferData.bindingParams( location + i, currentParams );
                needsBind = true;
            }
        }
        if ( needsBind )
        {
            gl46core::glBindVertexBuffers( location, count, bufferIDs, offsets, strides );
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    GLStateTracker::BindResult GLStateTracker::setActiveFB( const RenderTarget::Usage usage, const gl46core::GLuint ID )
    {
        gl46core::GLuint temp = 0;
        return setActiveFB( usage, ID, temp );
    }

    /// Switch the current framebuffer by binding it as either a R/W buffer, read
    /// buffer or write buffer
    GLStateTracker::BindResult GLStateTracker::setActiveFB( const RenderTarget::Usage usage, gl46core::GLuint ID, gl46core::GLuint& previousID )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // We may query the active framebuffer handle and get an invalid handle in
        // return and then try to bind the queried handle
        // This is, for example, in save/restore FB scenarios. An invalid handle
        // will just reset the buffer binding
        if ( ID == GL_NULL_HANDLE )
        {
            ID = 0;
        }
        previousID = _activeFBID[to_U32( usage )];

        // Prevent double bind
        if ( _activeFBID[to_U32( usage )] == ID )
        {
            if ( usage == RenderTarget::Usage::RT_READ_WRITE )
            {
                if ( _activeFBID[to_base( RenderTarget::Usage::RT_READ_ONLY )] == ID &&
                     _activeFBID[to_base( RenderTarget::Usage::RT_WRITE_ONLY )] == ID )
                {
                    return BindResult::ALREADY_BOUND;
                }
            }
            else
            {
                return BindResult::ALREADY_BOUND;
            }
        }

        // Bind the requested buffer to the appropriate target
        switch ( usage )
        {
            case RenderTarget::Usage::RT_READ_WRITE:
            {
                // According to documentation this is equivalent to independent calls to
                // bindFramebuffer(read, ID) and bindFramebuffer(write, ID)
                gl46core::glBindFramebuffer( gl46core::GL_FRAMEBUFFER, ID );
                // This also overrides the read and write bindings
                _activeFBID[to_base( RenderTarget::Usage::RT_READ_ONLY )] = ID;
                _activeFBID[to_base( RenderTarget::Usage::RT_WRITE_ONLY )] = ID;
            } break;
            case RenderTarget::Usage::RT_READ_ONLY:
            {
                gl46core::glBindFramebuffer( gl46core::GL_READ_FRAMEBUFFER, ID );
            } break;
            case RenderTarget::Usage::RT_WRITE_ONLY:
            {
                gl46core::glBindFramebuffer( gl46core::GL_DRAW_FRAMEBUFFER, ID );
            } break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        };

        // Remember the new binding state for future reference
        _activeFBID[to_U32( usage )] = ID;

        return BindResult::JUST_BOUND;
    }

    /// Single place to change buffer objects for every target available
    GLStateTracker::BindResult GLStateTracker::setActiveBuffer( const gl46core::GLenum target, const gl46core::GLuint bufferHandle, gl46core::GLuint& previousID )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        gl46core::GLuint& crtBinding = target != gl46core::GL_ELEMENT_ARRAY_BUFFER
                                               ? _activeBufferID[GetBufferTargetIndex( target )]
                                               : _activeVAOIB;
        previousID = crtBinding;

        // Prevent double bind (hope that this is the most common case. Should be.)
        if ( previousID == bufferHandle )
        {
            return BindResult::ALREADY_BOUND;
        }

        // Remember the new binding for future reference
        crtBinding = bufferHandle;
        // Bind the specified buffer handle to the desired buffer target
        glBindBuffer( target, bufferHandle );
        return BindResult::JUST_BOUND;
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBuffer( const gl46core::GLenum target, const gl46core::GLuint bufferHandle )
    {
        gl46core::GLuint temp = 0u;
        return setActiveBuffer( target, bufferHandle, temp );
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBufferIndexRange( const gl46core::GLenum target, const gl46core::GLuint bufferHandle, const gl46core::GLuint bindIndex, const size_t offsetInBytes, const size_t rangeInBytes, gl46core::GLuint& previousID )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        BindConfig& crtConfig = _currentBindConfig[GetBufferTargetIndex( target )];
        DIVIDE_ASSERT( bindIndex < crtConfig.size() );

        BindConfigEntry& entry = crtConfig[bindIndex];

        if ( entry._handle != bufferHandle ||
             entry._offset != offsetInBytes ||
             entry._range != rangeInBytes )
        {
            previousID = entry._handle;
            entry = { bufferHandle, offsetInBytes, rangeInBytes };
            if ( offsetInBytes == 0u && rangeInBytes == 0u )
            {
                gl46core::glBindBufferBase( target, bindIndex, bufferHandle );
            }
            else
            {
                gl46core::glBindBufferRange( target, bindIndex, bufferHandle, offsetInBytes, rangeInBytes );
            }
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBufferIndex( const gl46core::GLenum target, const gl46core::GLuint bufferHandle, const gl46core::GLuint bindIndex )
    {
        gl46core::GLuint temp = 0u;
        return setActiveBufferIndex( target, bufferHandle, bindIndex, temp );
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBufferIndex( const gl46core::GLenum target, const gl46core::GLuint bufferHandle, const gl46core::GLuint bindIndex, gl46core::GLuint& previousID )
    {
        return setActiveBufferIndexRange( target, bufferHandle, bindIndex, 0u, 0u, previousID );
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBufferIndexRange( const gl46core::GLenum target, const gl46core::GLuint bufferHandle, const gl46core::GLuint bindIndex, const size_t offsetInBytes, const size_t rangeInBytes )
    {
        gl46core::GLuint temp = 0u;
        return setActiveBufferIndexRange( target, bufferHandle, bindIndex, offsetInBytes, rangeInBytes, temp );
    }

    /// Change the currently active shader program. Passing null will unbind shaders (will use program 0)
    GLStateTracker::BindResult GLStateTracker::setActiveProgram( const gl46core::GLuint programHandle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Check if we are binding a new program or unbinding all shaders
        // Prevent double bind
        if ( _activeShaderProgramHandle != programHandle )
        {
            DIVIDE_EXPECTED_CALL( setActiveShaderPipeline( 0u ) != GLStateTracker::BindResult::FAILED );

            // Remember the new binding for future reference
            _activeShaderProgramHandle = programHandle;
            // Bind the new program
            gl46core::glUseProgram( programHandle );
            if ( programHandle == 0u )
            {
                _activeShaderProgram = nullptr;
            }
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    /// Change the currently active shader pipeline. Passing null will unbind shaders (will use pipeline 0)
    GLStateTracker::BindResult GLStateTracker::setActiveShaderPipeline( const gl46core::GLuint pipelineHandle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Check if we are binding a new program or unbinding all shaders
        // Prevent double bind
        if ( _activeShaderPipelineHandle != pipelineHandle )
        {
            DIVIDE_EXPECTED_CALL( setActiveProgram( 0u ) != GLStateTracker::BindResult::FAILED );

            // Remember the new binding for future reference
            _activeShaderPipelineHandle = pipelineHandle;
            // Bind the new pipeline
            gl46core::glBindProgramPipeline( pipelineHandle );
            if ( pipelineHandle == 0u )
            {
                _activeShaderProgram = nullptr;
            }
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    void GLStateTracker::setBlendColour( const UColour4& blendColour )
    {
        if ( _blendColour != blendColour )
        {
            _blendColour.set( blendColour );

            const FColour4 floatColour = Util::ToFloatColour( blendColour );
            gl46core::glBlendColor( floatColour.r, floatColour.g, floatColour.b, floatColour.a );
        }
    }

    void GLStateTracker::setBlending( const BlendingSettings& blendingProperties )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const bool enable = blendingProperties.enabled();

        if ( _blendEnabledGlobal == gl46core::GL_TRUE != enable )
        {
            enable ? gl46core::glEnable( gl46core::GL_BLEND ) : gl46core::glDisable( gl46core::GL_BLEND );
            _blendEnabledGlobal = enable ? gl46core::GL_TRUE : gl46core::GL_FALSE;
            std::fill( std::begin( _blendEnabled ), std::end( _blendEnabled ), _blendEnabledGlobal );
        }

        if ( enable && _blendPropertiesGlobal != blendingProperties )
        {
            if ( blendingProperties.blendOpAlpha() != BlendOperation::COUNT )
            {
                if ( blendingProperties.blendSrc() != _blendPropertiesGlobal.blendSrc() ||
                     blendingProperties.blendDest() != _blendPropertiesGlobal.blendDest() ||
                     blendingProperties.blendSrcAlpha() != _blendPropertiesGlobal.blendSrcAlpha() ||
                     blendingProperties.blendDestAlpha() != _blendPropertiesGlobal.blendDestAlpha() )
                {
                    gl46core::glBlendFuncSeparate( GLUtil::glBlendTable[to_base( blendingProperties.blendSrc() )],
                                                   GLUtil::glBlendTable[to_base( blendingProperties.blendDest() )],
                                                   GLUtil::glBlendTable[to_base( blendingProperties.blendSrcAlpha() )],
                                                   GLUtil::glBlendTable[to_base( blendingProperties.blendDestAlpha() )] );
                }

                if ( blendingProperties.blendOp() != _blendPropertiesGlobal.blendOp() ||
                     blendingProperties.blendOpAlpha() != _blendPropertiesGlobal.blendOpAlpha() )
                {
                    gl46core::glBlendEquationSeparate( GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                                                                                            ? to_base( blendingProperties.blendOp() )
                                                                                                            : to_base( BlendOperation::ADD )],
                                                       GLUtil::glBlendOpTable[to_base( blendingProperties.blendOpAlpha() )] );
                }
            }
            else
            {
                if ( blendingProperties.blendSrc() != _blendPropertiesGlobal.blendSrc() ||
                     blendingProperties.blendDest() != _blendPropertiesGlobal.blendDest() )
                {
                    gl46core::glBlendFunc( GLUtil::glBlendTable[to_base( blendingProperties.blendSrc() )],
                                           GLUtil::glBlendTable[to_base( blendingProperties.blendDest() )] );
                }
                if ( blendingProperties.blendOp() != _blendPropertiesGlobal.blendOp() )
                {
                    gl46core::glBlendEquation( GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                                                                                    ? to_base( blendingProperties.blendOp() )
                                                                                                    : to_base( BlendOperation::ADD )] );
                }

            }

            _blendPropertiesGlobal = blendingProperties;

            std::fill( std::begin( _blendProperties ), std::end( _blendProperties ), _blendPropertiesGlobal );
        }
    }

    void GLStateTracker::setBlending( const gl46core::GLuint drawBufferIdx, const BlendingSettings& blendingProperties )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const bool enable = blendingProperties.enabled();

        assert( drawBufferIdx < GFXDevice::GetDeviceInformation()._maxRTColourAttachments );

        if ( _blendEnabled[drawBufferIdx] == gl46core::GL_TRUE != enable )
        {
            enable ? gl46core::glEnablei( gl46core::GL_BLEND, drawBufferIdx ) : gl46core::glDisablei( gl46core::GL_BLEND, drawBufferIdx );
            _blendEnabled[drawBufferIdx] = enable ? gl46core::GL_TRUE : gl46core::GL_FALSE;
            if ( !enable )
            {
                _blendEnabledGlobal = gl46core::GL_FALSE;
            }
        }

        BlendingSettings& crtProperties = _blendProperties[drawBufferIdx];

        if ( enable && crtProperties != blendingProperties )
        {
            if ( blendingProperties.blendOpAlpha() != BlendOperation::COUNT )
            {
                if ( blendingProperties.blendSrc() != crtProperties.blendSrc() ||
                     blendingProperties.blendDest() != crtProperties.blendDest() ||
                     blendingProperties.blendSrcAlpha() != crtProperties.blendSrcAlpha() ||
                     blendingProperties.blendDestAlpha() != crtProperties.blendDestAlpha() )
                {
                    gl46core::glBlendFuncSeparatei( drawBufferIdx,
                                                    GLUtil::glBlendTable[to_base( blendingProperties.blendSrc() )],
                                                    GLUtil::glBlendTable[to_base( blendingProperties.blendDest() )],
                                                    GLUtil::glBlendTable[to_base( blendingProperties.blendSrcAlpha() )],
                                                    GLUtil::glBlendTable[to_base( blendingProperties.blendDestAlpha() )] );

                    _blendPropertiesGlobal.blendSrc() = blendingProperties.blendSrc();
                    _blendPropertiesGlobal.blendDest() = blendingProperties.blendDest();
                    _blendPropertiesGlobal.blendSrcAlpha() = blendingProperties.blendSrcAlpha();
                    _blendPropertiesGlobal.blendDestAlpha() = blendingProperties.blendDestAlpha();
                }

                if ( blendingProperties.blendOp() != crtProperties.blendOp() ||
                     blendingProperties.blendOpAlpha() != crtProperties.blendOpAlpha() )
                {
                    gl46core::glBlendEquationSeparatei( drawBufferIdx,
                                                        GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                                                                                             ? to_base( blendingProperties.blendOp() )
                                                                                                             : to_base( BlendOperation::ADD )],
                                                         GLUtil::glBlendOpTable[to_base( blendingProperties.blendOpAlpha() )] );

                    _blendPropertiesGlobal.blendOp() = blendingProperties.blendOp();
                    _blendPropertiesGlobal.blendOpAlpha() = blendingProperties.blendOpAlpha();
                }
            }
            else
            {
                if ( blendingProperties.blendSrc() != crtProperties.blendSrc() ||
                     blendingProperties.blendDest() != crtProperties.blendDest() )
                {
                    gl46core::glBlendFunci( drawBufferIdx,
                                            GLUtil::glBlendTable[to_base( blendingProperties.blendSrc() )],
                                            GLUtil::glBlendTable[to_base( blendingProperties.blendDest() )] );
                                        
                    _blendPropertiesGlobal.blendSrc() = blendingProperties.blendSrc();
                    _blendPropertiesGlobal.blendDest() = blendingProperties.blendDest();
                }

                if ( blendingProperties.blendOp() != crtProperties.blendOp() )
                {
                    gl46core::glBlendEquationi( drawBufferIdx,
                                                GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                                                                                     ? to_base( blendingProperties.blendOp() )
                                                                                                     : to_base( BlendOperation::ADD )] );
                    _blendPropertiesGlobal.blendOp() = blendingProperties.blendOp();
                }
            }

            crtProperties = blendingProperties;
        }
    }

    /// Change the current viewport area. Redundancy check is performed in GFXDevice class
    bool GLStateTracker::setViewport( const Rect<I32>& viewport )
    {
        if ( viewport.z > 0 && viewport.w > 0 && viewport != _activeViewport )
        {
            gl46core::glViewport( viewport.x, viewport.y, viewport.z, viewport.w );
            _activeViewport.set( viewport );
            return true;
        }

        return false;
    }

    bool GLStateTracker::setClearColour( const FColour4& colour )
    {
        if ( colour != _activeClearColour )
        {
            gl46core::glClearColor( colour.r, colour.g, colour.b, colour.a );
            _activeClearColour.set( colour );
            return true;
        }

        return false;
    }

    bool GLStateTracker::setClearDepth( const F32 value )
    {
        if ( !COMPARE( _clearDepthValue, value ) )
        {
            gl46core::glClearDepth( value );
            _clearDepthValue = value;
            return true;
        }

        return false;
    }

    bool GLStateTracker::setScissor( const Rect<I32>& rect )
    {
        if ( rect != _activeScissor )
        {
            gl46core::glScissor( rect.x, rect.y, rect.z, rect.w );
            _activeScissor.set( rect );
            return true;
        }

        return false;
    }

    bool GLStateTracker::setAlphaToCoverage( const bool state )
    {
        if ( _alphaToCoverageEnabled != state )
        {
            _alphaToCoverageEnabled = state;
            if ( state )
            {
                gl46core::glEnable( gl46core::GL_SAMPLE_ALPHA_TO_COVERAGE );
            }
            else
            {
                gl46core::glDisable( gl46core::GL_SAMPLE_ALPHA_TO_COVERAGE );
            }

            return true;
        }

        return false;
    }

    gl46core::GLuint GLStateTracker::getBoundTextureHandle( const U8 slot )  const noexcept
    {
        return _textureBoundMap[slot];
    }

    gl46core::GLuint GLStateTracker::getBoundSamplerHandle( const U8 slot ) const noexcept
    {
        return _samplerBoundMap[slot];
    }

    gl46core::GLuint GLStateTracker::getBoundProgramHandle() const noexcept
    {
        return _activeShaderPipelineHandle == 0u ? _activeShaderProgramHandle : _activeShaderPipelineHandle;
    }

    gl46core::GLuint GLStateTracker::getBoundBuffer( const gl46core::GLenum target, const gl46core::GLuint bindIndex ) const noexcept
    {
        size_t offset, range;
        return getBoundBuffer( target, bindIndex, offset, range );
    }

    gl46core::GLuint GLStateTracker::getBoundBuffer( const gl46core::GLenum target, const gl46core::GLuint bindIndex, size_t& offsetOut, size_t& rangeOut ) const noexcept
    {
        const BindConfigEntry& entry = _currentBindConfig[GetBufferTargetIndex( target )][bindIndex];
        offsetOut = entry._offset;
        rangeOut = entry._range;
        return entry._handle;
    }

    void GLStateTracker::getActiveViewport( Rect<I32>& viewportOut ) const noexcept
    {
        viewportOut = _activeViewport;
    }

    bool GLStateTracker::setDepthWrite( const bool state )
    {
        if ( _activeState._depthWriteEnabled != state )
        {
            gl46core::glDepthMask( state ? gl46core::GL_TRUE : gl46core::GL_FALSE );
            return true;
        }

        return false;
    }

    /// A state block should contain all rendering state changes needed for the next draw call.
    /// Some may be redundant, so we check each one individually
    bool GLStateTracker::activateStateBlock( const RenderStateBlock& newBlock )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        bool ret = false;
        if ( _activeState._stencilEnabled != newBlock._stencilEnabled )
        {
            newBlock._stencilEnabled ? gl46core::glEnable( gl46core::GL_STENCIL_TEST ) : gl46core::glDisable( gl46core::GL_STENCIL_TEST );
            ret = true;
        }

        if ( _activeState._depthTestEnabled != newBlock._depthTestEnabled )
        {
            newBlock._depthTestEnabled ? gl46core::glEnable( gl46core::GL_DEPTH_TEST ) : gl46core::glDisable( gl46core::GL_DEPTH_TEST );
            ret = true;
        }

        if ( _activeState._scissorTestEnabled != newBlock._scissorTestEnabled )
        {
            newBlock._scissorTestEnabled ? gl46core::glEnable( gl46core::GL_SCISSOR_TEST ) : gl46core::glDisable( gl46core::GL_SCISSOR_TEST );
            ret = true;
        }

        if ( setDepthWrite( newBlock._depthWriteEnabled ) )
        {
            ret = true;
        }

        
        if ( _activeState._rasterizationEnabled != newBlock._rasterizationEnabled )
        {
            newBlock._rasterizationEnabled ? gl46core::glDisable( gl46core::GL_RASTERIZER_DISCARD )
                                           : gl46core::glEnable( gl46core::GL_RASTERIZER_DISCARD );
            ret = true;
        }

        // Toggle primitive restart on or off
        if ( _activeState._primitiveRestartEnabled != newBlock._primitiveRestartEnabled )
        {
            newBlock._primitiveRestartEnabled ? gl46core::glEnable( gl46core::GL_PRIMITIVE_RESTART_FIXED_INDEX )
                                              : gl46core::glDisable( gl46core::GL_PRIMITIVE_RESTART_FIXED_INDEX );
            ret = true;
        }
        // Check culling mode (back (CW) / front (CCW) by default)
        if ( _activeState._cullMode != newBlock._cullMode )
        {
            if ( newBlock._cullMode != CullMode::NONE )
            {
                if ( _activeState._cullMode == CullMode::NONE )
                {
                    gl46core::glEnable( gl46core::GL_CULL_FACE );
                }

                gl46core::glCullFace( GLUtil::glCullModeTable[to_U32( newBlock._cullMode )] );
            }
            else
            {
                gl46core::glDisable( gl46core::GL_CULL_FACE );
            }
            ret = true;
        }

        if ( _activeState._frontFaceCCW != newBlock._frontFaceCCW )
        {
            gl46core::glFrontFace( newBlock._frontFaceCCW ? gl46core::GL_CCW : gl46core::GL_CW );
            ret = true;
        }

        // Check rasterization mode
        if ( _activeState._fillMode != newBlock._fillMode )
        {
            gl46core::glPolygonMode( gl46core::GL_FRONT_AND_BACK, GLUtil::glFillModeTable[to_U32( newBlock._fillMode )] );
            ret = true;
        }

        if ( _activeState._tessControlPoints != newBlock._tessControlPoints )
        {
            gl46core::glPatchParameteri( gl46core::GL_PATCH_VERTICES, newBlock._tessControlPoints );
            ret = true;
        }

        // Check the depth function
        if ( _activeState._zFunc != newBlock._zFunc )
        {
            gl46core::glDepthFunc( GLUtil::glCompareFuncTable[to_U32( newBlock._zFunc )] );
            ret = true;
        }

        // Check if we need to change the stencil mask
        if ( _activeState._stencilWriteMask != newBlock._stencilWriteMask )
        {
            gl46core::glStencilMask( newBlock._stencilWriteMask );
            ret = true;
        }

        // Stencil function is dependent on 3 state parameters set together
        if ( _activeState._stencilFunc != newBlock._stencilFunc ||
             _activeState._stencilRef != newBlock._stencilRef ||
             _activeState._stencilMask != newBlock._stencilMask )
        {
            gl46core::glStencilFunc( GLUtil::glCompareFuncTable[to_U32( newBlock._stencilFunc )],
                                     newBlock._stencilRef,
                                     newBlock._stencilMask );
            ret = true;
        }
        // Stencil operation is also dependent  on 3 state parameters set together
        if ( _activeState._stencilFailOp != newBlock._stencilFailOp ||
             _activeState._stencilZFailOp != newBlock._stencilZFailOp ||
             _activeState._stencilPassOp != newBlock._stencilPassOp )
        {
            glStencilOp( GLUtil::glStencilOpTable[to_U32( newBlock._stencilFailOp )],
                         GLUtil::glStencilOpTable[to_U32( newBlock._stencilZFailOp )],
                         GLUtil::glStencilOpTable[to_U32( newBlock._stencilPassOp )] );
            ret = true;
        }

        // Check and set polygon offset
        if ( !COMPARE( _activeState._zBias, newBlock._zBias ) )
        {
            if ( IS_ZERO( newBlock._zBias ) )
            {
                gl46core::glDisable( gl46core::GL_POLYGON_OFFSET_FILL );
            }
            else
            {
                gl46core::glEnable( gl46core::GL_POLYGON_OFFSET_FILL );
                if ( !COMPARE( _activeState._zBias, newBlock._zBias ) || !COMPARE( _activeState._zUnits, newBlock._zUnits ) )
                {
                    gl46core::glPolygonOffset( newBlock._zBias, newBlock._zUnits );
                }
            }
            ret = true;
        }

        // Check and set colour mask
        if ( _activeState._colourWrite.i != newBlock._colourWrite.i )
        {
            const P32 cWrite = newBlock._colourWrite;
            gl46core::glColorMask( cWrite.b[0] == 1 ? gl46core::GL_TRUE : gl46core::GL_FALSE,   // R
                                   cWrite.b[1] == 1 ? gl46core::GL_TRUE : gl46core::GL_FALSE,   // G
                                   cWrite.b[2] == 1 ? gl46core::GL_TRUE : gl46core::GL_FALSE,   // B
                                   cWrite.b[3] == 1 ? gl46core::GL_TRUE : gl46core::GL_FALSE ); // A

            ret = true;
        }

        if ( ret )
        {
            _activeState = newBlock;
        }

        return ret;
    }

} //namespace Divide
