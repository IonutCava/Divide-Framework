#include "stdafx.h"

#include "Headers/GLStateTracker.h"

#include "Headers/GLWrapper.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

#include "Utility/Headers/Localization.h"

namespace Divide {
    
namespace {
    // GL_NONE returns the count
    FORCE_INLINE GLint GetBufferTargetIndex(const GLenum target) noexcept {
        // Select the appropriate index in the array based on the buffer target
        switch (target) {
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

GLStateTracker::GLStateTracker()
{
    _blendPropertiesGlobal.blendSrc(BlendProperty::ONE);
    _blendPropertiesGlobal.blendDest(BlendProperty::ZERO);
    _blendPropertiesGlobal.blendOp(BlendOperation::ADD);
    _blendPropertiesGlobal.enabled(false);

    _vaoBufferData.init(GFXDevice::GetDeviceInformation()._maxVertAttributeBindings);
    _samplerBoundMap.resize(GFXDevice::GetDeviceInformation()._maxTextureUnits, 0u);
    _textureTypeBoundMap.resize(GFXDevice::GetDeviceInformation()._maxTextureUnits, TextureType::COUNT);

    _imageBoundMap.resize(GFXDevice::GetDeviceInformation()._maxTextureUnits);
    for (auto& it : _textureBoundMap) {
        it.resize(GFXDevice::GetDeviceInformation()._maxTextureUnits, 0u);
    }

    _blendProperties.resize(GFXDevice::GetDeviceInformation()._maxRTColourAttachments, BlendingSettings());
    _blendEnabled.resize(GFXDevice::GetDeviceInformation()._maxRTColourAttachments, GL_FALSE);

    _activeBufferID = create_array<13, GLuint>(GLUtil::k_invalidObjectID);
}

void GLStateTracker::setAttributesInternal(const GLuint vaoID, const AttributeMap& attributes) {
    // Update vertex attributes if needed (e.g. if offsets changed)
    for(U8 idx = 0u; idx < to_base(AttribLocation::COUNT); ++idx) {
        const AttributeDescriptor& descriptor = attributes[idx];

        if (descriptor._dataType == GFXDataFormat::COUNT) {
            glDisableVertexArrayAttrib(vaoID, idx);
        } else {
            glEnableVertexArrayAttrib(vaoID, idx);
            glVertexArrayAttribBinding(vaoID, idx, descriptor._bindingIndex);
            const bool isIntegerType = descriptor._dataType != GFXDataFormat::FLOAT_16 &&
                                       descriptor._dataType != GFXDataFormat::FLOAT_32;

            if (!isIntegerType || descriptor._normalized) {
                glVertexArrayAttribFormat(vaoID,
                                          idx,
                                          descriptor._componentsPerElement,
                                          GLUtil::glDataFormat[to_U32(descriptor._dataType)],
                                          descriptor._normalized ? GL_TRUE : GL_FALSE,
                                          static_cast<GLuint>(descriptor._strideInBytes));
            } else {
                glVertexArrayAttribIFormat(vaoID,
                                           idx,
                                           descriptor._componentsPerElement,
                                           GLUtil::glDataFormat[to_U32(descriptor._dataType)],
                                           static_cast<GLuint>(descriptor._strideInBytes));
            }

            const GLuint instanceDivisor = descriptor._instanceDivisor;
            if (_vaoBufferData.instanceDivisor(vaoID, idx) != instanceDivisor) {
                glVertexArrayBindingDivisor(vaoID, idx, instanceDivisor);
                _vaoBufferData.instanceDivisor(vaoID, idx, instanceDivisor);
            }

        }
    }
}

bool GLStateTracker::getOrCreateVAO(const size_t attributeHash, GLuint& vaoOut) {
    static U32 s_VAOidx = 0u;

    DIVIDE_ASSERT(Runtime::isMainThread());

    vaoOut = GLUtil::k_invalidObjectID;

    // See if we already have a matching VAO
    const auto it = GL_API::s_vaoCache.find(attributeHash);
    if (it != std::cend(GL_API::s_vaoCache)) {
        // Remember it if we do
        vaoOut = it->second;
        // Return true on a cache hit;
        return true;
    }

    // Otherwise allocate a new VAO and save it in the cache
    glCreateVertexArrays(1, &vaoOut);
    DIVIDE_ASSERT(vaoOut != GLUtil::k_invalidObjectID, Locale::Get(_ID("ERROR_VAO_INIT")));
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_VERTEX_ARRAY, vaoOut, -1, Util::StringFormat("GENERIC_VAO_%d", s_VAOidx++).c_str());
    }
    insert(GL_API::s_vaoCache, attributeHash, vaoOut);
    return false;
}

void GLStateTracker::setVertexFormat(const PrimitiveTopology topology, const bool primitiveRestartEnabled, const AttributeMap& attributes, const size_t attributeHash) {
    _activeTopology = topology;

    GLuint vao = GLUtil::k_invalidObjectID;
    if (!getOrCreateVAO(attributeHash, vao)) {
        // cache miss
        setAttributesInternal(vao, attributes);
    }

    if (setActiveVAO(vao) == BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }

    togglePrimitiveRestart(primitiveRestartEnabled);
}

GLStateTracker::BindResult GLStateTracker::setStateBlock(size_t stateBlockHash) {
    if (stateBlockHash == 0) {
        stateBlockHash = RenderStateBlock::DefaultHash();
    }

    // If the new state hash is different from the previous one
    if (stateBlockHash != _activeState.getHash()) {
        bool currentStateValid = false;
        const RenderStateBlock& currentState = RenderStateBlock::Get(stateBlockHash, currentStateValid);

        DIVIDE_ASSERT(currentStateValid, "GL_API error: Invalid state blocks detected on activation!");

        // Activate the new render state block in an rendering API dependent way
        activateStateBlock(currentState);
        return BindResult::JUST_BOUND;
    }

    return BindResult::ALREADY_BOUND;
}

/// Pixel pack alignment is usually changed by textures, PBOs, etc
bool GLStateTracker::setPixelPackAlignment(const GLint packAlignment,
                                           const GLint rowLength,
                                           const GLint skipRows,
                                           const GLint skipPixels) {
    // Keep track if we actually affect any OpenGL state
    bool changed = false;
    if (_activePackUnpackAlignments[0] != packAlignment) {
        glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
        _activePackUnpackAlignments[0] = packAlignment;
        changed = true;
    }

    if (_activePackUnpackRowLength[0] != rowLength) {
        glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
        _activePackUnpackRowLength[0] = rowLength;
        changed = true;
    }

    if (_activePackUnpackSkipRows[0] != skipRows) {
        glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
        _activePackUnpackSkipRows[0] = skipRows;
        changed = true;
    }

    if (_activePackUnpackSkipPixels[0] != skipPixels) {
        glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
        _activePackUnpackSkipPixels[0] = skipPixels;
        changed = true;
    }

    // We managed to change at least one entry
    return changed;
}

/// Pixel unpack alignment is usually changed by textures, PBOs, etc
bool GLStateTracker::setPixelUnpackAlignment(const GLint unpackAlignment,
                                             const GLint rowLength,
                                             const GLint skipRows,
                                             const GLint skipPixels) {
    // Keep track if we actually affect any OpenGL state
    bool changed = false;
    if (_activePackUnpackAlignments[1] != unpackAlignment) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
        _activePackUnpackAlignments[1] = unpackAlignment;
        changed = true;
    }

    if (rowLength != -1 && _activePackUnpackRowLength[1] != rowLength) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
        _activePackUnpackRowLength[1] = rowLength;
        changed = true;
    }

    if (skipRows != -1 && _activePackUnpackSkipRows[1] != skipRows) {
        glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
        _activePackUnpackSkipRows[1] = skipRows;
        changed = true;
    }

    if (skipPixels != -1 && _activePackUnpackSkipPixels[1] != skipPixels) {
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
        _activePackUnpackSkipPixels[1] = skipPixels;
        changed = true;
    }

    // We managed to change at least one entry
    return changed;
}

/// Enable or disable primitive restart and ensure that the correct index size is used
void GLStateTracker::togglePrimitiveRestart(const bool state) {
    // Toggle primitive restart on or off
    if (_primitiveRestartEnabled != state) {
        _primitiveRestartEnabled = state;
        state ? glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX)
              : glDisable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
    }
}

/// Enable or disable primitive rasterization
void GLStateTracker::toggleRasterization(const bool state) {
    // Toggle primitive restart on or off
    if (_rasterizationEnabled != state) {
        _rasterizationEnabled = state;
        state ? glDisable(GL_RASTERIZER_DISCARD)
              : glEnable(GL_RASTERIZER_DISCARD);
    }
}

GLStateTracker::BindResult GLStateTracker::bindSamplers(const GLushort unitOffset,
                                                        const GLuint samplerCount,
                                                        const GLuint* const samplerHandles) 
{
    if (samplerCount > 0 && unitOffset + samplerCount < GFXDevice::GetDeviceInformation()._maxTextureUnits)
    {
        if (samplerCount == 1) {
            GLuint& handle = _samplerBoundMap[unitOffset];
            const GLuint targetHandle = samplerHandles ? samplerHandles[0] : 0u;
            if (handle != targetHandle) {
                glBindSampler(unitOffset, targetHandle);
                handle = targetHandle;
                return BindResult::JUST_BOUND;
            }
            return BindResult::ALREADY_BOUND;
        } else {
            glBindSamplers(unitOffset, samplerCount, samplerHandles);
            if (samplerHandles != nullptr) {
                memcpy(&_samplerBoundMap[unitOffset], samplerHandles, sizeof(GLuint) * samplerCount);
            } else {
                memset(_samplerBoundMap.data(), 0u, sizeof(GLuint) * samplerCount);
            }
            return BindResult::JUST_BOUND;
        } 
    }

    return BindResult::FAILED;
}

bool GLStateTracker::unbindTexture(const TextureType type, const GLuint handle) {
    auto& boundMap = _textureBoundMap[to_base(type)];
    for (GLushort idx = 0u; idx < boundMap.size(); ++idx) {
        if (boundMap[idx] == handle) {
            boundMap[idx] = 0u;
            glBindTextureUnit(idx, 0u);
            if (bindSamplers(idx, 1, nullptr) == BindResult::FAILED) {
                DIVIDE_UNEXPECTED_CALL();
            }
            return true;
        }
    }

    return false;
}

bool GLStateTracker::unbindTextures() {
    for (auto& map : _textureBoundMap) {
        std::memset(map.data(), 0u, map.size() * sizeof(GLuint));
    }
    std::memset(_samplerBoundMap.data(), 0u, _samplerBoundMap.size() * sizeof(GLuint));

    glBindTextures(0u, GFXDevice::GetDeviceInformation()._maxTextureUnits - 1, nullptr);
    glBindSamplers(0u, GFXDevice::GetDeviceInformation()._maxTextureUnits - 1, nullptr);

    return true;
}

/// Bind a texture specified by a GL handle and GL type to the specified unit using the sampler object defined by hash value
GLStateTracker::BindResult GLStateTracker::bindTexture(const GLushort unit, TextureType type, const GLuint handle, const GLuint samplerHandle) {
    // Fail if we specified an invalid unit. Assert instead of returning false because this might be related to a bad algorithm
    DIVIDE_ASSERT(unit < GFXDevice::GetDeviceInformation()._maxTextureUnits, "GLStates error: invalid texture unit specified as a texture binding slot!");
    for (U8 typeIt = 0u; type == TextureType::COUNT && typeIt < to_base(TextureType::COUNT); ++typeIt) {
        auto& map = _textureBoundMap[typeIt];
        for (const GLuint it : map) {
            if (it == handle) {
                type = static_cast<TextureType>(typeIt);
                break;
            }
        }
    }

    BindResult result = BindResult::FAILED;

    DIVIDE_ASSERT(type != TextureType::COUNT);
    GLuint& crtEntry = _textureBoundMap[to_base(type)][unit];
    if (crtEntry == handle) {
        result = BindResult::ALREADY_BOUND;
    } else {
        crtEntry = handle;
        glBindTextureUnit(unit, handle);
        result = BindResult::JUST_BOUND;
    }

    GLuint& crtSampler = _samplerBoundMap[unit];
    if (crtSampler != samplerHandle) {
        crtSampler = samplerHandle;
        glBindSampler(unit, samplerHandle);
        result = BindResult::JUST_BOUND;
    }

    return result;
}

GLStateTracker::BindResult GLStateTracker::bindTextures(const GLushort unitOffset,
                                                        const GLuint textureCount,
                                                        const TextureType texturesType,
                                                        const GLuint* const textureHandles,
                                                        const GLuint* const samplerHandles) {

    BindResult result = BindResult::FAILED;

    // This trick will save us from looking up the desired handle from the array twice (for single textures)
    // and also provide an easy way of figuring out if we bound anything
    GLuint lastValidHandle = GLUtil::k_invalidObjectID;
    if (textureCount > 0 && unitOffset + textureCount < GFXDevice::GetDeviceInformation()._maxTextureUnits) {

          if (texturesType == TextureType::COUNT) {
              // Due to the use of DSA we can't specify the texture type directly.
              // We have to actually specify the type for single textures
              assert(textureCount > 1);

              lastValidHandle = 0u;
              for (U8 type = 0; type < to_base(TextureType::COUNT); ++type) {
                  std::memset(&_textureBoundMap[type][unitOffset], 0, textureCount * sizeof(GLuint));
              }
          } else {
              auto& boundMap = _textureBoundMap[to_base(texturesType)];

              for (GLuint idx = 0; idx < textureCount; ++idx) {
                  // Handles should always contain just the range we need regardless of unitOffset
                  // First handle should always be the first element in the array
                  const GLuint targetHandle = textureHandles ? textureHandles[idx] : 0u;

                  GLuint& crtHandle = boundMap[to_size(unitOffset) + idx];
                  if (targetHandle != crtHandle) {
                      crtHandle = targetHandle;
                      lastValidHandle = targetHandle;
                  }
              }
          }

          if (lastValidHandle != GLUtil::k_invalidObjectID) {
              if (textureCount == 1) {
                  glBindTextureUnit(unitOffset, lastValidHandle);
              } else {
                  glBindTextures(unitOffset, textureCount, textureHandles);
              }
              eastl::fill_n(&_textureTypeBoundMap[unitOffset], textureCount, texturesType);
              result = BindResult::JUST_BOUND;
          } else {
              result = BindResult::ALREADY_BOUND;
          }

        if (bindSamplers(unitOffset, textureCount, samplerHandles) == BindResult::FAILED) {
            result = BindResult::FAILED;
        }
    }

    return result;
}

GLStateTracker::BindResult GLStateTracker::bindTextureImage(const GLushort unit, const GLuint handle, const GLint level,
                                                            const bool layered, const GLint layer, const GLenum access, 
                                                            const GLenum format)
{
    ImageBindSettings tempSettings = {handle, level, layered ? GL_TRUE : GL_FALSE, layer, access, format};

    ImageBindSettings& settings = _imageBoundMap[unit];
    if (settings != tempSettings) {
        glBindImageTexture(unit, handle, level, layered ? GL_TRUE : GL_FALSE, layer, access, format);
        settings = tempSettings;
        return BindResult::JUST_BOUND;
    }

    return BindResult::ALREADY_BOUND;
}

/// Single place to change buffer objects for every target available
GLStateTracker::BindResult GLStateTracker::bindActiveBuffer(const GLuint location, GLuint bufferID, size_t offset, size_t stride) {
    DIVIDE_ASSERT(_activeVAOID != GLUtil::k_invalidObjectID && _activeVAOID != 0u);

    const VAOBindings::BufferBindingParams& bindings = _vaoBufferData.bindingParams(_activeVAOID, location);
    const VAOBindings::BufferBindingParams currentParams(bufferID, offset, stride);

    if (bindings != currentParams) {
        // Bind the specified buffer handle to the desired buffer target
        glVertexArrayVertexBuffer(_activeVAOID, location, bufferID, static_cast<GLintptr>(offset), static_cast<GLsizei>(stride));
        // Remember the new binding for future reference
        _vaoBufferData.bindingParams(_activeVAOID, location, currentParams);
        return BindResult::JUST_BOUND;
    }

    return BindResult::ALREADY_BOUND;
}

GLStateTracker::BindResult GLStateTracker::bindActiveBuffers(const GLuint location, const GLsizei count, GLuint* bufferIDs, GLintptr* offsets, GLsizei* strides) {
    DIVIDE_ASSERT(_activeVAOID != GLUtil::k_invalidObjectID && _activeVAOID != 0u);

    bool needsBind = false;
    for (GLsizei i = 0u; i < count; ++i) {
        const VAOBindings::BufferBindingParams& bindings = _vaoBufferData.bindingParams(_activeVAOID, i);
        const VAOBindings::BufferBindingParams currentParams(bufferIDs[i], offsets[i], strides[i]);
        if (bindings != currentParams) {
            _vaoBufferData.bindingParams(_activeVAOID, location + i, currentParams);
            needsBind = true;
        }
    }
    if (needsBind) {
        glVertexArrayVertexBuffers(_activeVAOID, location, count, bufferIDs, offsets, strides);
        return BindResult::JUST_BOUND;
    }
    
    return BindResult::ALREADY_BOUND;
}

GLStateTracker::BindResult GLStateTracker::setActiveFB(const RenderTarget::RenderTargetUsage usage, const GLuint ID) {
    GLuint temp = 0;
    return setActiveFB(usage, ID, temp);
}

/// Switch the current framebuffer by binding it as either a R/W buffer, read
/// buffer or write buffer
GLStateTracker::BindResult GLStateTracker::setActiveFB(const RenderTarget::RenderTargetUsage usage, GLuint ID, GLuint& previousID) {
    // We may query the active framebuffer handle and get an invalid handle in
    // return and then try to bind the queried handle
    // This is, for example, in save/restore FB scenarios. An invalid handle
    // will just reset the buffer binding
    if (ID == GLUtil::k_invalidObjectID) {
        ID = 0;
    }
    previousID = _activeFBID[to_U32(usage)];

    // Prevent double bind
    if (_activeFBID[to_U32(usage)] == ID) {
        if (usage == RenderTarget::RenderTargetUsage::RT_READ_WRITE) {
            if (_activeFBID[to_base(RenderTarget::RenderTargetUsage::RT_READ_ONLY)] == ID &&
                _activeFBID[to_base(RenderTarget::RenderTargetUsage::RT_WRITE_ONLY)] == ID) {
                return BindResult::ALREADY_BOUND;
            }
        } else {
            return BindResult::ALREADY_BOUND;
        }
    }

    // Bind the requested buffer to the appropriate target
    switch (usage) {
        case RenderTarget::RenderTargetUsage::RT_READ_WRITE: {
            // According to documentation this is equivalent to independent
            // calls to
            // bindFramebuffer(read, ID) and bindFramebuffer(write, ID)
            glBindFramebuffer(GL_FRAMEBUFFER, ID);
            // This also overrides the read and write bindings
            _activeFBID[to_base(RenderTarget::RenderTargetUsage::RT_READ_ONLY)] = ID;
            _activeFBID[to_base(RenderTarget::RenderTargetUsage::RT_WRITE_ONLY)] = ID;
        } break;
        case RenderTarget::RenderTargetUsage::RT_READ_ONLY: {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, ID);
        } break;
        case RenderTarget::RenderTargetUsage::RT_WRITE_ONLY: {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ID);
        } break;
        default: DIVIDE_UNEXPECTED_CALL(); break;
    };

    // Remember the new binding state for future reference
    _activeFBID[to_U32(usage)] = ID;

    return BindResult::JUST_BOUND;
}

GLStateTracker::BindResult GLStateTracker::setActiveVAO(const GLuint ID) {
    GLuint temp = 0;
    return setActiveVAO(ID, temp);
}

/// Switch the currently active vertex array object
GLStateTracker::BindResult GLStateTracker::setActiveVAO(const GLuint ID, GLuint& previousID) {
    assert(ID != GLUtil::k_invalidObjectID);

    previousID = _activeVAOID;
    // Prevent double bind
    if (_activeVAOID != ID) {
        // Remember the new binding for future reference
        _activeVAOID = ID;
        // Activate the specified VAO
        glBindVertexArray(ID);
        return BindResult::JUST_BOUND;
    }

    return BindResult::ALREADY_BOUND;
}


/// Single place to change buffer objects for every target available
GLStateTracker::BindResult GLStateTracker::setActiveBuffer(const GLenum target, const GLuint bufferHandle, GLuint& previousID) {
    GLuint& crtBinding = target != GL_ELEMENT_ARRAY_BUFFER 
                                 ? _activeBufferID[GetBufferTargetIndex(target)]
                                 : _activeVAOIB[_activeVAOID];
    previousID = crtBinding;

    // Prevent double bind (hope that this is the most common case. Should be.)
    if (previousID == bufferHandle) {
        return BindResult::ALREADY_BOUND;
    }

    // Remember the new binding for future reference
    crtBinding = bufferHandle;
    // Bind the specified buffer handle to the desired buffer target
    glBindBuffer(target, bufferHandle);
    return BindResult::JUST_BOUND;
}

GLStateTracker::BindResult GLStateTracker::setActiveBuffer(const GLenum target, const GLuint bufferHandle) {
    GLuint temp = 0u;
    return setActiveBuffer(target, bufferHandle, temp);
}

GLStateTracker::BindResult GLStateTracker::setActiveBufferIndexRange(const GLenum target, const GLuint bufferHandle, const GLuint bindIndex, const size_t offsetInBytes, const size_t rangeInBytes, GLuint& previousID) {
    BindConfig& crtConfig = _currentBindConfig[GetBufferTargetIndex(target)];
    DIVIDE_ASSERT(bindIndex < crtConfig.size());

    BindConfigEntry& entry = crtConfig[bindIndex];

    if (entry._handle != bufferHandle ||
        entry._offset != offsetInBytes ||
        entry._range != rangeInBytes)
    {
        previousID = entry._handle;
        entry = { bufferHandle, offsetInBytes, rangeInBytes };
        if (offsetInBytes == 0u && rangeInBytes == 0u) {
            glBindBufferBase(target, bindIndex, bufferHandle);
        } else {
            glBindBufferRange(target, bindIndex, bufferHandle, offsetInBytes, rangeInBytes);
        }
        return BindResult::JUST_BOUND;
    }

    return BindResult::ALREADY_BOUND;
}

GLStateTracker::BindResult GLStateTracker::setActiveBufferIndex(const GLenum target, const GLuint bufferHandle, const GLuint bindIndex) {
    GLuint temp = 0u;
    return setActiveBufferIndex(target, bufferHandle, bindIndex, temp);
}

GLStateTracker::BindResult GLStateTracker::setActiveBufferIndex(const GLenum target, const GLuint bufferHandle, const GLuint bindIndex, GLuint& previousID) {
    return setActiveBufferIndexRange(target, bufferHandle, bindIndex, 0u, 0u, previousID);
}

GLStateTracker::BindResult GLStateTracker::setActiveBufferIndexRange(const GLenum target, const GLuint bufferHandle, const GLuint bindIndex, const size_t offsetInBytes, const size_t rangeInBytes) {
    GLuint temp = 0u;
    return setActiveBufferIndexRange(target, bufferHandle, bindIndex, offsetInBytes, rangeInBytes, temp);
}

/// Change the currently active shader program. Passing null will unbind shaders (will use program 0)
GLStateTracker::BindResult GLStateTracker::setActiveProgram(const GLuint programHandle) {
    // Check if we are binding a new program or unbinding all shaders
    // Prevent double bind
    if (_activeShaderProgram != programHandle) {
        if (setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }

        // Remember the new binding for future reference
        _activeShaderProgram = programHandle;
        // Bind the new program
        glUseProgram(programHandle);
        return BindResult::JUST_BOUND;
    }

    return BindResult::ALREADY_BOUND;
}

/// Change the currently active shader pipeline. Passing null will unbind shaders (will use pipeline 0)
GLStateTracker::BindResult GLStateTracker::setActiveShaderPipeline(const GLuint pipelineHandle) {
    // Check if we are binding a new program or unbinding all shaders
    // Prevent double bind
    if (_activeShaderPipeline != pipelineHandle) {
        if (setActiveProgram(0u) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }

        // Remember the new binding for future reference
        _activeShaderPipeline = pipelineHandle;
        // Bind the new pipeline
        glBindProgramPipeline(pipelineHandle);
        return BindResult::JUST_BOUND;
    }

    return BindResult::ALREADY_BOUND;
}

void GLStateTracker::setDepthRange(F32 nearVal, F32 farVal) {
    CLAMP_01(nearVal);
    CLAMP_01(farVal);
    if (!COMPARE(nearVal, _depthNearVal) && !COMPARE(farVal, _depthFarVal)) {
        glDepthRange(nearVal, farVal);
        _depthNearVal = nearVal;
        _depthFarVal = farVal;
    }
}

void GLStateTracker::setClippingPlaneState(const bool lowerLeftOrigin, const bool negativeOneToOneDepth) {
    if (_lowerLeftOrigin != lowerLeftOrigin || _negativeOneToOneDepth != negativeOneToOneDepth) {

        glClipControl(
            lowerLeftOrigin ? GL_LOWER_LEFT : GL_UPPER_LEFT,
            negativeOneToOneDepth ? GL_NEGATIVE_ONE_TO_ONE : GL_ZERO_TO_ONE
        );

        _lowerLeftOrigin = lowerLeftOrigin;
        _negativeOneToOneDepth = negativeOneToOneDepth;
    }
}

void GLStateTracker::setBlendColour(const UColour4& blendColour) {
    if (_blendColour != blendColour) {
        _blendColour.set(blendColour);

        const FColour4 floatColour = Util::ToFloatColour(blendColour);
        glBlendColor(floatColour.r, floatColour.g, floatColour.b, floatColour.a);
    }
}

void GLStateTracker::setBlending(const BlendingSettings& blendingProperties) {
    OPTICK_EVENT();

    const bool enable = blendingProperties.enabled();

    if (_blendEnabledGlobal == GL_TRUE != enable) {
        enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        _blendEnabledGlobal = enable ? GL_TRUE : GL_FALSE;
        std::fill(std::begin(_blendEnabled), std::end(_blendEnabled), _blendEnabledGlobal);
    }

    if (enable && _blendPropertiesGlobal != blendingProperties) {
        if (blendingProperties.blendOpAlpha() != BlendOperation::COUNT) {
            if (blendingProperties.blendSrc() != _blendPropertiesGlobal.blendSrc() ||
                blendingProperties.blendDest() != _blendPropertiesGlobal.blendDest() ||
                blendingProperties.blendSrcAlpha() != _blendPropertiesGlobal.blendSrcAlpha() ||
                blendingProperties.blendDestAlpha() != _blendPropertiesGlobal.blendDestAlpha())
            {
                    glBlendFuncSeparate(GLUtil::glBlendTable[to_base(blendingProperties.blendSrc())],
                                        GLUtil::glBlendTable[to_base(blendingProperties.blendDest())],
                                        GLUtil::glBlendTable[to_base(blendingProperties.blendSrcAlpha())],
                                        GLUtil::glBlendTable[to_base(blendingProperties.blendDestAlpha())]);
            }
                
            if (blendingProperties.blendOp() != _blendPropertiesGlobal.blendOp() ||
                blendingProperties.blendOpAlpha() != _blendPropertiesGlobal.blendOpAlpha())
            {
                    glBlendEquationSeparate(GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                                                                                 ? to_base(blendingProperties.blendOp())
                                                                                                 : to_base(BlendOperation::ADD)],
                                            GLUtil::glBlendOpTable[to_base(blendingProperties.blendOpAlpha())]);
            }
        } else {
            if (blendingProperties.blendSrc() != _blendPropertiesGlobal.blendSrc() ||
                blendingProperties.blendDest() != _blendPropertiesGlobal.blendDest())
            {
                    glBlendFunc(GLUtil::glBlendTable[to_base(blendingProperties.blendSrc())],
                                GLUtil::glBlendTable[to_base(blendingProperties.blendDest())]);
            }
            if (blendingProperties.blendOp() != _blendPropertiesGlobal.blendOp())
            {
                glBlendEquation(GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                                                                     ? to_base(blendingProperties.blendOp())
                                                                                     : to_base(BlendOperation::ADD)]);
            }

        }

        _blendPropertiesGlobal = blendingProperties;

        std::fill(std::begin(_blendProperties), std::end(_blendProperties), _blendPropertiesGlobal);
    }
}

void GLStateTracker::setBlending(const GLuint drawBufferIdx,const BlendingSettings& blendingProperties) {
    const bool enable = blendingProperties.enabled();

    assert(drawBufferIdx < GFXDevice::GetDeviceInformation()._maxRTColourAttachments);

    if (_blendEnabled[drawBufferIdx] == GL_TRUE != enable) {
        enable ? glEnablei(GL_BLEND, drawBufferIdx) : glDisablei(GL_BLEND, drawBufferIdx);
        _blendEnabled[drawBufferIdx] = enable ? GL_TRUE : GL_FALSE;
        if (!enable) {
            _blendEnabledGlobal = GL_FALSE;
        }
    }

    BlendingSettings& crtProperties = _blendProperties[drawBufferIdx];

    if (enable && crtProperties != blendingProperties) {
        if (blendingProperties.blendOpAlpha() != BlendOperation::COUNT) {
            if (blendingProperties.blendSrc() != crtProperties.blendSrc() ||
                blendingProperties.blendDest() != crtProperties.blendDest() ||
                blendingProperties.blendSrcAlpha() != crtProperties.blendSrcAlpha() ||
                blendingProperties.blendDestAlpha() != crtProperties.blendDestAlpha())
            {
                    glBlendFuncSeparatei(drawBufferIdx,
                                         GLUtil::glBlendTable[to_base(blendingProperties.blendSrc())],
                                         GLUtil::glBlendTable[to_base(blendingProperties.blendDest())],
                                         GLUtil::glBlendTable[to_base(blendingProperties.blendSrcAlpha())],
                                         GLUtil::glBlendTable[to_base(blendingProperties.blendDestAlpha())]);

                    _blendPropertiesGlobal.blendSrc() = blendingProperties.blendSrc();
                    _blendPropertiesGlobal.blendDest() = blendingProperties.blendDest();
                    _blendPropertiesGlobal.blendSrcAlpha() = blendingProperties.blendSrcAlpha();
                    _blendPropertiesGlobal.blendDestAlpha() = blendingProperties.blendDestAlpha();
            }

            if (blendingProperties.blendOp() != crtProperties.blendOp() ||
                blendingProperties.blendOpAlpha() != crtProperties.blendOpAlpha())
            {
                glBlendEquationSeparatei(drawBufferIdx, 
                                         GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                                                                              ? to_base(blendingProperties.blendOp())
                                                                                              : to_base(BlendOperation::ADD)],
                                         GLUtil::glBlendOpTable[to_base(blendingProperties.blendOpAlpha())]);

                _blendPropertiesGlobal.blendOp() = blendingProperties.blendOp();
                _blendPropertiesGlobal.blendOpAlpha() = blendingProperties.blendOpAlpha();
            }
        } else {
            if (blendingProperties.blendSrc() != crtProperties.blendSrc() ||
                blendingProperties.blendDest() != crtProperties.blendDest())
            {
                    glBlendFunci(drawBufferIdx,
                                 GLUtil::glBlendTable[to_base(blendingProperties.blendSrc())],
                                 GLUtil::glBlendTable[to_base(blendingProperties.blendDest())]);

                    _blendPropertiesGlobal.blendSrc() = blendingProperties.blendSrc();
                    _blendPropertiesGlobal.blendDest() = blendingProperties.blendDest();
            }

            if (blendingProperties.blendOp() != crtProperties.blendOp())
            {
                glBlendEquationi(drawBufferIdx,
                                 GLUtil::glBlendOpTable[blendingProperties.blendOp() != BlendOperation::COUNT
                                                                                      ? to_base(blendingProperties.blendOp())
                                                                                      : to_base(BlendOperation::ADD)]);
                _blendPropertiesGlobal.blendOp() = blendingProperties.blendOp();
            }
        }

        crtProperties = blendingProperties;
    }
}

/// Change the current viewport area. Redundancy check is performed in GFXDevice class
bool GLStateTracker::setViewport(const Rect<I32>& viewport) {
    if (viewport.z > 0 && viewport.w > 0 && viewport != _activeViewport) {
        glViewport(viewport.x, viewport.y, viewport.z, viewport.w);
        _activeViewport.set(viewport);
        return true;
    }

    return false;
}

bool GLStateTracker::setClearColour(const FColour4& colour) {
    if (colour != _activeClearColour) {
        glClearColor(colour.r, colour.g, colour.b, colour.a);
        _activeClearColour.set(colour);
        return true;
    }

    return false;
}

bool GLStateTracker::setScissor(const Rect<I32>& rect) {
    if (rect != _activeScissor) {
        glScissor(rect.x, rect.y, rect.z, rect.w);
        _activeScissor.set(rect);
        return true;
    }

    return false;
}

bool GLStateTracker::setDepthWrite(const bool state) {
    if (_depthWriteEnabled != state) {
        _depthWriteEnabled = state;
        glDepthMask(state ? GL_TRUE : GL_FALSE);
        return true;
    }

    return false;
}

GLuint GLStateTracker::getBoundTextureHandle(const U8 slot, const TextureType type)  const {
    return type == TextureType::COUNT ? 0u : _textureBoundMap[to_base(type)][slot];
}

GLuint GLStateTracker::getBoundSamplerHandle(const U8 slot) const {
    return _samplerBoundMap[slot];
}

TextureType GLStateTracker::getBoundTextureType(const U8 slot) const {
    return _textureTypeBoundMap[slot];
}

GLuint GLStateTracker::getBoundProgramHandle() const noexcept {
    return _activeShaderPipeline == 0u ? _activeShaderProgram : _activeShaderPipeline;
}

GLuint GLStateTracker::getBoundBuffer(const GLenum target, const GLuint bindIndex) const {
    size_t offset, range;
    return getBoundBuffer(target, bindIndex, offset, range);
}

GLuint GLStateTracker::getBoundBuffer(const GLenum target, const GLuint bindIndex, size_t& offsetOut, size_t& rangeOut) const {
    const BindConfigEntry& entry = _currentBindConfig[GetBufferTargetIndex(target)][bindIndex];
    offsetOut = entry._offset;
    rangeOut = entry._range;
    return entry._handle;
}

void GLStateTracker::getActiveViewport(GLint* const vp) const noexcept {
    if (vp != nullptr) {
        *vp = *_activeViewport._v;
    }
}

/// A state block should contain all rendering state changes needed for the next draw call.
/// Some may be redundant, so we check each one individually
void GLStateTracker::activateStateBlock(const RenderStateBlock& newBlock) {

    if (_activeState.cullEnabled() != newBlock.cullEnabled()) {
        newBlock.cullEnabled() ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    }

    if (_activeState.stencilEnable() != newBlock.stencilEnable()) {
        newBlock.stencilEnable() ? glEnable(GL_STENCIL_TEST) : glDisable(GL_STENCIL_TEST);
    }

    if (_activeState.depthTestEnabled() != newBlock.depthTestEnabled()) {
        newBlock.depthTestEnabled() ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    }

    if (_activeState.scissorTestEnabled() != newBlock.scissorTestEnabled()) {
        newBlock.scissorTestEnabled() ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    }

    // Check culling mode (back (CW) / front (CCW) by default)
    if (_activeState.cullMode() != newBlock.cullMode()) {
        if (newBlock.cullMode() != CullMode::NONE) {
            const GLenum targetMode = GLUtil::glCullModeTable[to_U32(newBlock.cullMode())];
            if (_currentCullMode != targetMode) {
                glCullFace(targetMode);
                _currentCullMode = targetMode;
            }
        }
    }

    if (_activeState.frontFaceCCW() != newBlock.frontFaceCCW()) {
        _currentFrontFace = newBlock.frontFaceCCW() ? GL_CCW : GL_CW;
        glFrontFace(_currentFrontFace);
    }

    // Check rasterization mode
    if (_activeState.fillMode() != newBlock.fillMode()) {
        glPolygonMode(GL_FRONT_AND_BACK, GLUtil::glFillModeTable[to_U32(newBlock.fillMode())]);
    }

    if (_activeState.tessControlPoints() != newBlock.tessControlPoints()) {
        glPatchParameteri(GL_PATCH_VERTICES, newBlock.tessControlPoints());
    }

    // Check the depth function
    if (_activeState.zFunc() != newBlock.zFunc()) {
        glDepthFunc(GLUtil::glCompareFuncTable[to_U32(newBlock.zFunc())]);
    }

    // Check if we need to change the stencil mask
    if (_activeState.stencilWriteMask() != newBlock.stencilWriteMask()) {
        glStencilMask(newBlock.stencilWriteMask());
    }
    // Stencil function is dependent on 3 state parameters set together
    if (_activeState.stencilFunc() != newBlock.stencilFunc() ||
        _activeState.stencilRef()  != newBlock.stencilRef() ||
        _activeState.stencilMask() != newBlock.stencilMask()) {
        glStencilFunc(GLUtil::glCompareFuncTable[to_U32(newBlock.stencilFunc())],
                      newBlock.stencilRef(),
                      newBlock.stencilMask());
    }
    // Stencil operation is also dependent  on 3 state parameters set together
    if (_activeState.stencilFailOp()  != newBlock.stencilFailOp() ||
        _activeState.stencilZFailOp() != newBlock.stencilZFailOp() ||
        _activeState.stencilPassOp()  != newBlock.stencilPassOp()) {
        glStencilOp(GLUtil::glStencilOpTable[to_U32(newBlock.stencilFailOp())],
                    GLUtil::glStencilOpTable[to_U32(newBlock.stencilZFailOp())],
                    GLUtil::glStencilOpTable[to_U32(newBlock.stencilPassOp())]);
    }
    // Check and set polygon offset
    if (!COMPARE(_activeState.zBias(), newBlock.zBias())) {
        if (IS_ZERO(newBlock.zBias())) {
            glDisable(GL_POLYGON_OFFSET_FILL);
        } else {
            glEnable(GL_POLYGON_OFFSET_FILL);
            if (!COMPARE(_activeState.zBias(), newBlock.zBias()) || !COMPARE(_activeState.zUnits(), newBlock.zUnits())) {
                glPolygonOffset(newBlock.zBias(), newBlock.zUnits());
            }
        }
    }

    // Check and set colour mask
    if (_activeState.colourWrite().i != newBlock.colourWrite().i) {
        const P32 cWrite = newBlock.colourWrite();
        glColorMask(cWrite.b[0] == 1 ? GL_TRUE : GL_FALSE,   // R
                    cWrite.b[1] == 1 ? GL_TRUE : GL_FALSE,   // G
                    cWrite.b[2] == 1 ? GL_TRUE : GL_FALSE,   // B
                    cWrite.b[3] == 1 ? GL_TRUE : GL_FALSE);  // A
    }

    _activeState.from(newBlock);
}

}; //namespace Divide