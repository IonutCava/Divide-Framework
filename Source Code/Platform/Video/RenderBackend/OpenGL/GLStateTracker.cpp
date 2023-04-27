#include "stdafx.h"

#include "Headers/GLStateTracker.h"
#include "Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

#include "Utility/Headers/Localization.h"

namespace Divide
{

    namespace
    {
        // GL_NONE returns the count
        FORCE_INLINE GLint GetBufferTargetIndex( const GLenum target ) noexcept
        {
            // Select the appropriate index in the array based on the buffer target
            switch ( target )
            {
                case GL_TEXTURE_BUFFER: return 0;
                case GL_UNIFORM_BUFFER: return 1;
                case GL_SHADER_STORAGE_BUFFER: return 2;
                case GL_PIXEL_UNPACK_BUFFER: return 3;
                case GL_DRAW_INDIRECT_BUFFER: return 4;
                case GL_ARRAY_BUFFER: return 5;
                case GL_PARAMETER_BUFFER: return 6;
                case GL_ELEMENT_ARRAY_BUFFER: return 7;
                case GL_PIXEL_PACK_BUFFER: return 8;
                case GL_TRANSFORM_FEEDBACK_BUFFER: return 9;
                case GL_COPY_READ_BUFFER: return 10;
                case GL_COPY_WRITE_BUFFER: return 11;
                case GL_QUERY_BUFFER: return 12;
                case GL_ATOMIC_COUNTER_BUFFER: return 13;
                case GL_NONE: return 14;
                default: break;
            };

            DIVIDE_UNEXPECTED_CALL();
            return -1;
        }

    }; //namespace 

    void GLStateTracker::setDefaultState()
    {
        _activeState = {};
        _debugScope.fill( {} );
        _debugScopeDepth = 0u;
        _activePipeline = nullptr;
        _activeShaderProgram = nullptr;
        _activeTopology = PrimitiveTopology::COUNT;
        _activeRenderTarget = nullptr;
        _activeRenderTargetID = INVALID_RENDER_TARGET_ID;
        _activeRenderTargetDimensions = {1u, 1u};
        _activeVAOID = GLUtil::k_invalidObjectID;
        _activeFBID[0] = _activeFBID[1] = _activeFBID[2] = GLUtil::k_invalidObjectID;
        _activeVAOIB.clear();
        _drawIndirectBufferOffset = 0u;
        _packAlignment = {};
        _unpackAlignment = {};
        _activeShaderProgramHandle = 0u;
        _activeShaderPipelineHandle = 0u;
        _alphaToCoverageEnabled = false;
        _blendPropertiesGlobal = {};
        _blendEnabledGlobal = GL_FALSE;
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
        _blendEnabled.resize( GFXDevice::GetDeviceInformation()._maxRTColourAttachments, GL_FALSE );

        _activeBufferID = create_array<13, GLuint>( GLUtil::k_invalidObjectID );
        _textureBoundMap.fill( GLUtil::k_invalidObjectID );
        _samplerBoundMap.fill( GLUtil::k_invalidObjectID );
    }

    void GLStateTracker::setAttributesInternal( const GLuint vaoID, const AttributeMap& attributes )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Update vertex attributes if needed (e.g. if offsets changed)
        for ( U8 idx = 0u; idx < to_base( AttribLocation::COUNT ); ++idx )
        {
            const AttributeDescriptor& descriptor = attributes._attributes[idx];

            if ( descriptor._dataType == GFXDataFormat::COUNT )
            {
                glDisableVertexArrayAttrib( vaoID, idx );
            }
            else
            {
                glEnableVertexArrayAttrib( vaoID, idx );
                glVertexArrayAttribBinding( vaoID, idx, descriptor._vertexBindingIndex );
                const bool isIntegerType = descriptor._dataType != GFXDataFormat::FLOAT_16 &&
                    descriptor._dataType != GFXDataFormat::FLOAT_32;

                if ( !isIntegerType || descriptor._normalized )
                {
                    glVertexArrayAttribFormat( vaoID,
                                               idx,
                                               descriptor._componentsPerElement,
                                               GLUtil::glDataFormatTable[to_U32( descriptor._dataType )],
                                               descriptor._normalized ? GL_TRUE : GL_FALSE,
                                               static_cast<GLuint>(descriptor._strideInBytes) );
                }
                else
                {
                    glVertexArrayAttribIFormat( vaoID,
                                                idx,
                                                descriptor._componentsPerElement,
                                                GLUtil::glDataFormatTable[to_U32( descriptor._dataType )],
                                                static_cast<GLuint>(descriptor._strideInBytes) );
                }
            }
        }

        for ( const VertexBinding& vertBinding : attributes._vertexBindings )
        {
            const bool perInstanceDivisor = !vertBinding._perVertexInputRate;
            if ( _vaoBufferData.instanceDivisorFlag( vaoID, vertBinding._bufferBindIndex ) != perInstanceDivisor )
            {
                glVertexArrayBindingDivisor( vaoID, vertBinding._bufferBindIndex, perInstanceDivisor ? 1u : 0u );
                _vaoBufferData.instanceDivisorFlag( vaoID, vertBinding._bufferBindIndex, perInstanceDivisor );
            }
        }
    }

    bool GLStateTracker::getOrCreateVAO( const size_t attributeHash, GLuint& vaoOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( Runtime::isMainThread() );

        vaoOut = GLUtil::k_invalidObjectID;

        // See if we already have a matching VAO
        const auto it = GL_API::s_vaoCache.find( attributeHash );
        if ( it != std::cend( GL_API::s_vaoCache ) )
        {
            // Remember it if we do
            vaoOut = it->second;
            // Return true on a cache hit;
            return true;
        }

        // Otherwise allocate a new VAO and save it in the cache
        glCreateVertexArrays( 1, &vaoOut );
        DIVIDE_ASSERT( vaoOut != GLUtil::k_invalidObjectID, Locale::Get( _ID( "ERROR_VAO_INIT" ) ) );

        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            glObjectLabel( GL_VERTEX_ARRAY, vaoOut, -1, Util::StringFormat( "GENERIC_VAO_%d", GL_API::s_vaoCache.size()).c_str());
        }

        insert( GL_API::s_vaoCache, attributeHash, vaoOut );
        return false;
    }

    void GLStateTracker::setPrimitiveTopology( const PrimitiveTopology topology )
    {
        _activeTopology = topology;
    }

    void GLStateTracker::setVertexFormat( const AttributeMap& attributes, const size_t attributeHash )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GLuint vao = GLUtil::k_invalidObjectID;
        if ( !getOrCreateVAO( attributeHash, vao ) )
        {
            // cache miss
            setAttributesInternal( vao, attributes );
        }

        if ( setActiveVAO( vao ) == BindResult::FAILED )
        {
            DIVIDE_UNEXPECTED_CALL();
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
            glPixelStorei( GL_PACK_ALIGNMENT, (GLint)pixelPackAlignment._alignment );
            changed = true;
        }

        if ( _packAlignment._rowLength != pixelPackAlignment._rowLength )
        {
            glPixelStorei( GL_PACK_ROW_LENGTH, (GLint)pixelPackAlignment._rowLength );
            changed = true;
        }

        if ( _packAlignment._skipRows != pixelPackAlignment._skipRows )
        {
            glPixelStorei( GL_PACK_SKIP_ROWS, (GLint)pixelPackAlignment._skipRows );
            changed = true;
        }

        if ( _packAlignment._skipPixels != pixelPackAlignment._skipPixels )
        {
            glPixelStorei( GL_PACK_SKIP_PIXELS, (GLint)pixelPackAlignment._skipPixels );
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
        if ( _unpackAlignment._alignment != pixelUnpackAlignment._alignment )
        {
            glPixelStorei( GL_UNPACK_ALIGNMENT, (GLint)pixelUnpackAlignment._alignment );
            changed = true;
        }

        if ( pixelUnpackAlignment._rowLength != -1 && _unpackAlignment._rowLength != pixelUnpackAlignment._rowLength )
        {
            glPixelStorei( GL_UNPACK_ROW_LENGTH, (GLint)pixelUnpackAlignment._rowLength );
            changed = true;
        }

        if ( pixelUnpackAlignment._skipRows != -1 && _unpackAlignment._skipRows != pixelUnpackAlignment._skipRows )
        {
            glPixelStorei( GL_UNPACK_SKIP_ROWS, (GLint)pixelUnpackAlignment._skipRows );
            changed = true;
        }

        if ( pixelUnpackAlignment._skipPixels != -1 && _unpackAlignment._skipPixels != pixelUnpackAlignment._skipPixels )
        {
            glPixelStorei( GL_UNPACK_SKIP_PIXELS, (GLint)pixelUnpackAlignment._skipPixels );
            changed = true;
        }

        if ( changed )
        {
            _unpackAlignment = pixelUnpackAlignment;
        }
        // We managed to change at least one entry
        return changed;
    }

    GLStateTracker::BindResult GLStateTracker::bindSamplers( const GLubyte unitOffset,
                                                             const GLuint samplerCount,
                                                             const GLuint* const samplerHandles )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        BindResult result = BindResult::FAILED;

        if ( samplerCount > 0 && unitOffset + samplerCount < GFXDevice::GetDeviceInformation()._maxTextureUnits )
        {
            if ( samplerCount == 1 )
            {
                GLuint& handle = _samplerBoundMap[unitOffset];
                const GLuint targetHandle = samplerHandles ? samplerHandles[0] : 0u;
                if ( handle != targetHandle )
                {
                    glBindSampler( unitOffset, targetHandle );
                    handle = targetHandle;
                    result = BindResult::JUST_BOUND;
                }
                result = BindResult::ALREADY_BOUND;
            }
            else
            {
                // Update bound map
                bool newBinding = false;
                for ( GLubyte idx = 0u; idx < samplerCount; ++idx )
                {
                    const U8 slot = unitOffset + idx;
                    const GLuint targetHandle = samplerHandles ? samplerHandles[idx] : 0u;
                    GLuint& crtHandle = _samplerBoundMap[slot];
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
                    glBindSamplers( unitOffset, samplerCount, samplerHandles );
                    result = BindResult::JUST_BOUND;
                }
            }
        }

        return result;
    }

    bool GLStateTracker::unbindTexture( [[maybe_unused]] const TextureType type, const GLuint handle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        for ( U8 idx = 0u; idx < MAX_BOUND_TEXTURE_UNITS; ++idx )
        {
            if ( _textureBoundMap[idx] == handle )
            {
                _textureBoundMap[idx] = GLUtil::k_invalidObjectID;
                glBindTextureUnit( idx, 0u );
                if ( bindSamplers( idx, 1, nullptr ) == BindResult::FAILED )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
                return true;
            }
        }

        return false;
    }

    bool GLStateTracker::unbindTextures()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _textureBoundMap.fill( GLUtil::k_invalidObjectID );
        _samplerBoundMap.fill( GLUtil::k_invalidObjectID );

        glBindTextures( 0u, GFXDevice::GetDeviceInformation()._maxTextureUnits - 1, nullptr );
        glBindSamplers( 0u, GFXDevice::GetDeviceInformation()._maxTextureUnits - 1, nullptr );

        return true;
    }

    /// Bind a texture specified by a GL handle and GL type to the specified unit using the sampler object defined by hash value
    GLStateTracker::BindResult GLStateTracker::bindTexture( const GLubyte unit, const GLuint handle, const GLuint samplerHandle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Fail if we specified an invalid unit. Assert instead of returning false because this might be related to a bad algorithm
        DIVIDE_ASSERT( unit < GFXDevice::GetDeviceInformation()._maxTextureUnits, "GLStates error: invalid texture unit specified as a texture binding slot!" );

        BindResult result = BindResult::ALREADY_BOUND;

        GLuint& crtEntry = _textureBoundMap[unit];
        if ( crtEntry != handle )
        {
            crtEntry = handle;
            glBindTextureUnit( unit, handle );
            result = BindResult::JUST_BOUND;
        }

        GLuint& crtSampler = _samplerBoundMap[unit];
        if ( crtSampler != samplerHandle )
        {
            crtSampler = samplerHandle;
            glBindSampler( unit, samplerHandle );
            result = BindResult::JUST_BOUND;
        }

        return result;
    }

    GLStateTracker::BindResult GLStateTracker::bindTextures( const GLubyte unitOffset,
                                                             const GLuint textureCount,
                                                             const GLuint* const textureHandles,
                                                             const GLuint* const samplerHandles )
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
            for ( GLubyte idx = 0u; idx < textureCount; ++idx )
            {
                const U8 slot = unitOffset + idx;
                const GLuint targetHandle = textureHandles ? textureHandles[idx] : 0u;
                GLuint& crtHandle = _textureBoundMap[slot];
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
                glBindTextures( unitOffset, textureCount, textureHandles );
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

    GLStateTracker::BindResult GLStateTracker::bindTextureImage( const GLubyte unit, const GLuint handle, const GLint level,
                                                                 const bool layered, const GLint layer, const GLenum access,
                                                                 const GLenum format )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( handle != GLUtil::k_invalidObjectID );

        ImageBindSettings tempSettings = { handle, level, layered ? GL_TRUE : GL_FALSE, layer, access, format };

        ImageBindSettings& settings = _imageBoundMap[unit];
        if ( settings != tempSettings )
        {
            glBindImageTexture( unit, handle, level, layered ? GL_TRUE : GL_FALSE, layer, access, format );
            settings = tempSettings;
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    /// Single place to change buffer objects for every target available
    GLStateTracker::BindResult GLStateTracker::bindActiveBuffer( const GLuint location, GLuint bufferID, size_t offset, size_t stride )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( _activeVAOID != GLUtil::k_invalidObjectID && _activeVAOID != 0u );

        const VAOBindings::BufferBindingParams& bindings = _vaoBufferData.bindingParams( _activeVAOID, location );
        const VAOBindings::BufferBindingParams currentParams( bufferID, offset, stride );

        if ( bindings != currentParams )
        {
            // Bind the specified buffer handle to the desired buffer target
            glVertexArrayVertexBuffer( _activeVAOID, location, bufferID, static_cast<GLintptr>(offset), static_cast<GLsizei>(stride) );
            // Remember the new binding for future reference
            _vaoBufferData.bindingParams( _activeVAOID, location, currentParams );
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    GLStateTracker::BindResult GLStateTracker::bindActiveBuffers( const GLuint location, const GLsizei count, GLuint* bufferIDs, GLintptr* offsets, GLsizei* strides )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( _activeVAOID != GLUtil::k_invalidObjectID && _activeVAOID != 0u );

        bool needsBind = false;
        for ( GLsizei i = 0u; i < count; ++i )
        {
            const VAOBindings::BufferBindingParams& bindings = _vaoBufferData.bindingParams( _activeVAOID, i );
            const VAOBindings::BufferBindingParams currentParams( bufferIDs[i], offsets[i], strides[i] );
            if ( bindings != currentParams )
            {
                _vaoBufferData.bindingParams( _activeVAOID, location + i, currentParams );
                needsBind = true;
            }
        }
        if ( needsBind )
        {
            glVertexArrayVertexBuffers( _activeVAOID, location, count, bufferIDs, offsets, strides );
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    GLStateTracker::BindResult GLStateTracker::setActiveFB( const RenderTarget::Usage usage, const GLuint ID )
    {
        GLuint temp = 0;
        return setActiveFB( usage, ID, temp );
    }

    /// Switch the current framebuffer by binding it as either a R/W buffer, read
    /// buffer or write buffer
    GLStateTracker::BindResult GLStateTracker::setActiveFB( const RenderTarget::Usage usage, GLuint ID, GLuint& previousID )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // We may query the active framebuffer handle and get an invalid handle in
        // return and then try to bind the queried handle
        // This is, for example, in save/restore FB scenarios. An invalid handle
        // will just reset the buffer binding
        if ( ID == GLUtil::k_invalidObjectID )
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
                glBindFramebuffer( GL_FRAMEBUFFER, ID );
                // This also overrides the read and write bindings
                _activeFBID[to_base( RenderTarget::Usage::RT_READ_ONLY )] = ID;
                _activeFBID[to_base( RenderTarget::Usage::RT_WRITE_ONLY )] = ID;
            } break;
            case RenderTarget::Usage::RT_READ_ONLY:
            {
                glBindFramebuffer( GL_READ_FRAMEBUFFER, ID );
            } break;
            case RenderTarget::Usage::RT_WRITE_ONLY:
            {
                glBindFramebuffer( GL_DRAW_FRAMEBUFFER, ID );
            } break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        };

        // Remember the new binding state for future reference
        _activeFBID[to_U32( usage )] = ID;

        return BindResult::JUST_BOUND;
    }

    GLStateTracker::BindResult GLStateTracker::setActiveVAO( const GLuint ID )
    {
        GLuint temp = 0;
        return setActiveVAO( ID, temp );
    }

    /// Switch the currently active vertex array object
    GLStateTracker::BindResult GLStateTracker::setActiveVAO( const GLuint ID, GLuint& previousID )
    {
        assert( ID != GLUtil::k_invalidObjectID );

        previousID = _activeVAOID;
        // Prevent double bind
        if ( _activeVAOID != ID )
        {
            // Remember the new binding for future reference
            _activeVAOID = ID;
            // Activate the specified VAO
            glBindVertexArray( ID );
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }


    /// Single place to change buffer objects for every target available
    GLStateTracker::BindResult GLStateTracker::setActiveBuffer( const GLenum target, const GLuint bufferHandle, GLuint& previousID )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GLuint& crtBinding = target != GL_ELEMENT_ARRAY_BUFFER
            ? _activeBufferID[GetBufferTargetIndex( target )]
            : _activeVAOIB[_activeVAOID];
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

    GLStateTracker::BindResult GLStateTracker::setActiveBuffer( const GLenum target, const GLuint bufferHandle )
    {
        GLuint temp = 0u;
        return setActiveBuffer( target, bufferHandle, temp );
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBufferIndexRange( const GLenum target, const GLuint bufferHandle, const GLuint bindIndex, const size_t offsetInBytes, const size_t rangeInBytes, GLuint& previousID )
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
                glBindBufferBase( target, bindIndex, bufferHandle );
            }
            else
            {
                glBindBufferRange( target, bindIndex, bufferHandle, offsetInBytes, rangeInBytes );
            }
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBufferIndex( const GLenum target, const GLuint bufferHandle, const GLuint bindIndex )
    {
        GLuint temp = 0u;
        return setActiveBufferIndex( target, bufferHandle, bindIndex, temp );
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBufferIndex( const GLenum target, const GLuint bufferHandle, const GLuint bindIndex, GLuint& previousID )
    {
        return setActiveBufferIndexRange( target, bufferHandle, bindIndex, 0u, 0u, previousID );
    }

    GLStateTracker::BindResult GLStateTracker::setActiveBufferIndexRange( const GLenum target, const GLuint bufferHandle, const GLuint bindIndex, const size_t offsetInBytes, const size_t rangeInBytes )
    {
        GLuint temp = 0u;
        return setActiveBufferIndexRange( target, bufferHandle, bindIndex, offsetInBytes, rangeInBytes, temp );
    }

    /// Change the currently active shader program. Passing null will unbind shaders (will use program 0)
    GLStateTracker::BindResult GLStateTracker::setActiveProgram( const GLuint programHandle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Check if we are binding a new program or unbinding all shaders
        // Prevent double bind
        if ( _activeShaderProgramHandle != programHandle )
        {
            if ( setActiveShaderPipeline( 0u ) == GLStateTracker::BindResult::FAILED )
            {
                DIVIDE_UNEXPECTED_CALL();
            }

            // Remember the new binding for future reference
            _activeShaderProgramHandle = programHandle;
            // Bind the new program
            glUseProgram( programHandle );
            if ( programHandle == 0u )
            {
                _activeShaderProgram = nullptr;
            }
            return BindResult::JUST_BOUND;
        }

        return BindResult::ALREADY_BOUND;
    }

    /// Change the currently active shader pipeline. Passing null will unbind shaders (will use pipeline 0)
    GLStateTracker::BindResult GLStateTracker::setActiveShaderPipeline( const GLuint pipelineHandle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Check if we are binding a new program or unbinding all shaders
        // Prevent double bind
        if ( _activeShaderPipelineHandle != pipelineHandle )
        {
            if ( setActiveProgram( 0u ) == GLStateTracker::BindResult::FAILED )
            {
                DIVIDE_UNEXPECTED_CALL();
            }

            // Remember the new binding for future reference
            _activeShaderPipelineHandle = pipelineHandle;
            // Bind the new pipeline
            glBindProgramPipeline( pipelineHandle );
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
            glBlendColor( floatColour.r, floatColour.g, floatColour.b, floatColour.a );
        }
    }

    void GLStateTracker::setBlending( const BlendingSettings& blendingProperties )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const bool enable = blendingProperties.enabled();

        if ( _blendEnabledGlobal == GL_TRUE != enable )
        {
            enable ? glEnable( GL_BLEND ) : glDisable( GL_BLEND );
            _blendEnabledGlobal = enable ? GL_TRUE : GL_FALSE;
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
                    glBlendFuncSeparate( GLUtil::glBlendTable[to_base( blendingProperties.blendSrc() )],
                                         GLUtil::glBlendTable[to_base( blendingProperties.blendDest() )],
                                         GLUtil::glBlendTable[to_base( blendingProperties.blendSrcAlpha() )],
                                         GLUtil::glBlendTable[to_base( blendingProperties.blendDestAlpha() )] );
                }

                if ( blendingProperties.blendOp() != _blendPropertiesGlobal.blendOp() ||
                     blendingProperties.blendOpAlpha() != _blendPropertiesGlobal.blendOpAlpha() )
                {
                    glBlendEquationSeparate( GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
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
                    glBlendFunc( GLUtil::glBlendTable[to_base( blendingProperties.blendSrc() )],
                                 GLUtil::glBlendTable[to_base( blendingProperties.blendDest() )] );
                }
                if ( blendingProperties.blendOp() != _blendPropertiesGlobal.blendOp() )
                {
                    glBlendEquation( GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                     ? to_base( blendingProperties.blendOp() )
                                     : to_base( BlendOperation::ADD )] );
                }

            }

            _blendPropertiesGlobal = blendingProperties;

            std::fill( std::begin( _blendProperties ), std::end( _blendProperties ), _blendPropertiesGlobal );
        }
    }

    void GLStateTracker::setBlending( const GLuint drawBufferIdx, const BlendingSettings& blendingProperties )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const bool enable = blendingProperties.enabled();

        assert( drawBufferIdx < GFXDevice::GetDeviceInformation()._maxRTColourAttachments );

        if ( _blendEnabled[drawBufferIdx] == GL_TRUE != enable )
        {
            enable ? glEnablei( GL_BLEND, drawBufferIdx ) : glDisablei( GL_BLEND, drawBufferIdx );
            _blendEnabled[drawBufferIdx] = enable ? GL_TRUE : GL_FALSE;
            if ( !enable )
            {
                _blendEnabledGlobal = GL_FALSE;
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
                    glBlendFuncSeparatei( drawBufferIdx,
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
                    glBlendEquationSeparatei( drawBufferIdx,
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
                    glBlendFunci( drawBufferIdx,
                                  GLUtil::glBlendTable[to_base( blendingProperties.blendSrc() )],
                                  GLUtil::glBlendTable[to_base( blendingProperties.blendDest() )] );

                    _blendPropertiesGlobal.blendSrc() = blendingProperties.blendSrc();
                    _blendPropertiesGlobal.blendDest() = blendingProperties.blendDest();
                }

                if ( blendingProperties.blendOp() != crtProperties.blendOp() )
                {
                    glBlendEquationi( drawBufferIdx,
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
            glViewport( viewport.x, viewport.y, viewport.z, viewport.w );
            _activeViewport.set( viewport );
            return true;
        }

        return false;
    }

    bool GLStateTracker::setClearColour( const FColour4& colour )
    {
        if ( colour != _activeClearColour )
        {
            glClearColor( colour.r, colour.g, colour.b, colour.a );
            _activeClearColour.set( colour );
            return true;
        }

        return false;
    }

    bool GLStateTracker::setClearDepth( const F32 value )
    {
        if ( !COMPARE( _clearDepthValue, value ) )
        {
            glClearDepth( value );
            _clearDepthValue = value;
            return true;
        }

        return false;
    }

    bool GLStateTracker::setScissor( const Rect<I32>& rect )
    {
        if ( rect != _activeScissor )
        {
            glScissor( rect.x, rect.y, rect.z, rect.w );
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
                glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
            }
            else
            {
                glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
            }

            return true;
        }

        return false;
    }

    GLuint GLStateTracker::getBoundTextureHandle( const U8 slot )  const noexcept
    {
        return _textureBoundMap[slot];
    }

    GLuint GLStateTracker::getBoundSamplerHandle( const U8 slot ) const noexcept
    {
        return _samplerBoundMap[slot];
    }

    GLuint GLStateTracker::getBoundProgramHandle() const noexcept
    {
        return _activeShaderPipelineHandle == 0u ? _activeShaderProgramHandle : _activeShaderPipelineHandle;
    }

    GLuint GLStateTracker::getBoundBuffer( const GLenum target, const GLuint bindIndex ) const noexcept
    {
        size_t offset, range;
        return getBoundBuffer( target, bindIndex, offset, range );
    }

    GLuint GLStateTracker::getBoundBuffer( const GLenum target, const GLuint bindIndex, size_t& offsetOut, size_t& rangeOut ) const noexcept
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
            glDepthMask( state ? GL_TRUE : GL_FALSE );
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
            newBlock._stencilEnabled ? glEnable( GL_STENCIL_TEST ) : glDisable( GL_STENCIL_TEST );
            ret = true;
        }

        if ( _activeState._depthTestEnabled != newBlock._depthTestEnabled )
        {
            newBlock._depthTestEnabled ? glEnable( GL_DEPTH_TEST ) : glDisable( GL_DEPTH_TEST );
            ret = true;
        }

        if ( _activeState._scissorTestEnabled != newBlock._scissorTestEnabled )
        {
            newBlock._scissorTestEnabled ? glEnable( GL_SCISSOR_TEST ) : glDisable( GL_SCISSOR_TEST );
            ret = true;
        }

        if ( setDepthWrite( newBlock._depthWriteEnabled ) )
        {
            ret = true;
        }

        
        if ( _activeState._rasterizationEnabled != newBlock._rasterizationEnabled )
        {
            newBlock._rasterizationEnabled ? glDisable( GL_RASTERIZER_DISCARD )
                                           : glEnable( GL_RASTERIZER_DISCARD );
            ret = true;
        }

        // Toggle primitive restart on or off
        if ( _activeState._primitiveRestartEnabled != newBlock._primitiveRestartEnabled )
        {
            newBlock._primitiveRestartEnabled ? glEnable( GL_PRIMITIVE_RESTART_FIXED_INDEX )
                                              : glDisable( GL_PRIMITIVE_RESTART_FIXED_INDEX );
            ret = true;
        }
        // Check culling mode (back (CW) / front (CCW) by default)
        if ( _activeState._cullMode != newBlock._cullMode )
        {
            if ( newBlock._cullMode != CullMode::NONE )
            {
                if ( _activeState._cullMode == CullMode::NONE )
                {
                    glEnable( GL_CULL_FACE );
                }

                glCullFace( GLUtil::glCullModeTable[to_U32( newBlock._cullMode )] );
            }
            else
            {
                glDisable( GL_CULL_FACE );
            }
            ret = true;
        }

        if ( _activeState._frontFaceCCW != newBlock._frontFaceCCW )
        {
            glFrontFace( newBlock._frontFaceCCW ? GL_CCW : GL_CW );
            ret = true;
        }

        // Check rasterization mode
        if ( _activeState._fillMode != newBlock._fillMode )
        {
            glPolygonMode( GL_FRONT_AND_BACK, GLUtil::glFillModeTable[to_U32( newBlock._fillMode )] );
            ret = true;
        }

        if ( _activeState._tessControlPoints != newBlock._tessControlPoints )
        {
            glPatchParameteri( GL_PATCH_VERTICES, newBlock._tessControlPoints );
            ret = true;
        }

        // Check the depth function
        if ( _activeState._zFunc != newBlock._zFunc )
        {
            glDepthFunc( GLUtil::glCompareFuncTable[to_U32( newBlock._zFunc )] );
            ret = true;
        }

        // Check if we need to change the stencil mask
        if ( _activeState._stencilWriteMask != newBlock._stencilWriteMask )
        {
            glStencilMask( newBlock._stencilWriteMask );
            ret = true;
        }

        // Stencil function is dependent on 3 state parameters set together
        if ( _activeState._stencilFunc != newBlock._stencilFunc ||
             _activeState._stencilRef != newBlock._stencilRef ||
             _activeState._stencilMask != newBlock._stencilMask )
        {
            glStencilFunc( GLUtil::glCompareFuncTable[to_U32( newBlock._stencilFunc )],
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
                glDisable( GL_POLYGON_OFFSET_FILL );
            }
            else
            {
                glEnable( GL_POLYGON_OFFSET_FILL );
                if ( !COMPARE( _activeState._zBias, newBlock._zBias ) || !COMPARE( _activeState._zUnits, newBlock._zUnits ) )
                {
                    glPolygonOffset( newBlock._zBias, newBlock._zUnits );
                }
            }
            ret = true;
        }

        // Check and set colour mask
        if ( _activeState._colourWrite.i != newBlock._colourWrite.i )
        {
            const P32 cWrite = newBlock._colourWrite;
            glColorMask( cWrite.b[0] == 1 ? GL_TRUE : GL_FALSE,   // R
                         cWrite.b[1] == 1 ? GL_TRUE : GL_FALSE,   // G
                         cWrite.b[2] == 1 ? GL_TRUE : GL_FALSE,   // B
                         cWrite.b[3] == 1 ? GL_TRUE : GL_FALSE ); // A

            ret = true;
        }

        if ( ret )
        {
            _activeState = newBlock;
        }

        return ret;
    }

}; //namespace Divide