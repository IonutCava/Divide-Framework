#include "stdafx.h"

#include "Headers/GLWrapper.h"

#include "Core/Headers/Kernel.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/ShaderBuffer/Headers/glUniformBuffer.h"

namespace Divide {

vector<GL_API::ResidentTexture> GL_API::s_residentTextures;

bool GL_API::s_IsFlushingCommandBuffer = false;

SharedMutex GL_API::s_samplerMapLock;
GL_API::SamplerObjectMap GL_API::s_samplerMap;
glHardwareQueryPool* GL_API::s_hardwareQueryPool = nullptr;

GLStateTracker& GL_API::GetStateTracker() noexcept {
    return s_stateTracker;
}

GLUtil::GLMemory::GLMemoryType GL_API::GetMemoryTypeForUsage(const GLenum usage) noexcept {
    assert(usage != GL_NONE);
    switch (usage) {
        case GL_UNIFORM_BUFFER:
        case GL_SHADER_STORAGE_BUFFER:
            return GLUtil::GLMemory::GLMemoryType::SHADER_BUFFER;
        case GL_ELEMENT_ARRAY_BUFFER:
            return GLUtil::GLMemory::GLMemoryType::INDEX_BUFFER;
        case GL_ARRAY_BUFFER:
            return GLUtil::GLMemory::GLMemoryType::VERTEX_BUFFER;
    };

    return GLUtil::GLMemory::GLMemoryType::OTHER;
}

GLUtil::GLMemory::DeviceAllocator& GL_API::GetMemoryAllocator(const GLUtil::GLMemory::GLMemoryType memoryType) noexcept {
    return s_memoryAllocators[to_base(memoryType)];
}

/// Reset as much of the GL default state as possible within the limitations given
void GL_API::clearStates(const DisplayWindow& window, GLStateTracker& stateTracker, const bool global) const {
    if (global) {
        if (stateTracker.bindTextures(0, GFXDevice::GetDeviceInformation()._maxTextureUnits - 1, TextureType::COUNT, nullptr, nullptr) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
        stateTracker.setPixelPackUnpackAlignment();
        stateTracker._activePixelBuffer = nullptr;
    }

    if (stateTracker.setActiveVAO(0) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    if (stateTracker.setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    if (stateTracker.setActiveFB(RenderTarget::RenderTargetUsage::RT_READ_WRITE, 0) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    stateTracker._activeClearColour.set(window.clearColour());
    const U8 blendCount = to_U8(stateTracker._blendEnabled.size());
    for (U8 i = 0u; i < blendCount; ++i) {
        stateTracker.setBlending(i, {});
    }
    stateTracker.setBlendColour({ 0u, 0u, 0u, 0u });

    const vec2<U16>& drawableSize = _context.getDrawableSize(window);
    stateTracker.setScissor({ 0, 0, drawableSize.width, drawableSize.height });

    stateTracker._activePipeline = nullptr;
    stateTracker._activeRenderTarget = nullptr;
    if (stateTracker.setActiveProgram(0u) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    if (stateTracker.setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    if (stateTracker.setStateBlock(RenderStateBlock::defaultHash()) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }

    stateTracker.setDepthWrite(true);
}

bool GL_API::DeleteBuffers(const GLuint count, GLuint* buffers) {
    if (count > 0 && buffers != nullptr) {
        for (GLuint i = 0; i < count; ++i) {
            const GLuint crtBuffer = buffers[i];
            GLStateTracker& stateTracker = GetStateTracker();
            for (GLuint& boundBuffer : stateTracker._activeBufferID) {
                if (boundBuffer == crtBuffer) {
                    boundBuffer = GLUtil::k_invalidObjectID;
                }
            }
            for (auto& boundBuffer : stateTracker._activeVAOIB) {
                if (boundBuffer.second == crtBuffer) {
                    boundBuffer.second = GLUtil::k_invalidObjectID;
                }
            }
        }

        glDeleteBuffers(count, buffers);
        memset(buffers, 0, count * sizeof(GLuint));
        return true;
    }

    return false;
}

bool GL_API::DeleteVAOs(const GLuint count, GLuint* vaos) {
    if (count > 0u && vaos != nullptr) {
        for (GLuint i = 0u; i < count; ++i) {
            if (GetStateTracker()._activeVAOID == vaos[i]) {
                GetStateTracker()._activeVAOID = GLUtil::k_invalidObjectID;
                break;
            }
        }
        glDeleteVertexArrays(count, vaos);
        memset(vaos, 0, count * sizeof(GLuint));
        return true;
    }
    return false;
}

bool GL_API::DeleteFramebuffers(const GLuint count, GLuint* framebuffers) {
    if (count > 0 && framebuffers != nullptr) {
        for (GLuint i = 0; i < count; ++i) {
            const GLuint crtFB = framebuffers[i];
            for (GLuint& activeFB : GetStateTracker()._activeFBID) {
                if (activeFB == crtFB) {
                    activeFB = GLUtil::k_invalidObjectID;
                }
            }
        }
        glDeleteFramebuffers(count, framebuffers);
        memset(framebuffers, 0, count * sizeof(GLuint));
        return true;
    }
    return false;
}

bool GL_API::DeleteShaderPrograms(const GLuint count, GLuint* programs) {
    if (count > 0 && programs != nullptr) {
        for (GLuint i = 0; i < count; ++i) {
            if (GetStateTracker()._activeShaderProgram == programs[i]) {
                if (GetStateTracker().setActiveProgram(0u) == GLStateTracker::BindResult::FAILED) {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }
            glDeleteProgram(programs[i]);
        }
        
        memset(programs, 0, count * sizeof(GLuint));
        return true;
    }
    return false;
}


bool GL_API::DeleteTextures(const GLuint count, GLuint* textures, const TextureType texType) {
    if (count > 0 && textures != nullptr) {
        
        for (GLuint i = 0; i < count; ++i) {
            const GLuint crtTex = textures[i];
            if (crtTex != 0) {
                GLStateTracker& stateTracker = GetStateTracker();

                auto bindingIt = stateTracker._textureBoundMap[to_base(texType)];
                for (GLuint& handle : bindingIt) {
                    if (handle == crtTex) {
                        handle = 0u;
                    }
                }

                for (ImageBindSettings& settings : stateTracker._imageBoundMap) {
                    if (settings._texture == crtTex) {
                        settings.reset();
                    }
                }
            }
        }
        glDeleteTextures(count, textures);
        memset(textures, 0, count * sizeof(GLuint));
        return true;
    }

    return false;
}

bool GL_API::DeleteSamplers(const GLuint count, GLuint* samplers) {
    if (count > 0 && samplers != nullptr) {

        for (GLuint i = 0; i < count; ++i) {
            const GLuint crtSampler = samplers[i];
            if (crtSampler != 0) {
                for (GLuint& boundSampler : GetStateTracker()._samplerBoundMap) {
                    if (boundSampler == crtSampler) {
                        boundSampler = 0;
                    }
                }
            }
        }
        glDeleteSamplers(count, samplers);
        memset(samplers, 0, count * sizeof(GLuint));
        return true;
    }

    return false;
}

};