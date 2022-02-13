#include "stdafx.h"

#include "Headers/GLWrapper.h"

#include "Core/Headers/Kernel.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/ShaderBuffer/Headers/glUniformBuffer.h"

namespace Divide {

/// The following static variables are used to remember the current OpenGL state
GLuint GL_API::s_UBOffsetAlignment = 0u;
GLuint GL_API::s_UBMaxSize = 0u;
GLuint GL_API::s_SSBOffsetAlignment = 0u;
GLuint GL_API::s_SSBMaxSize = 0u;
GLuint GL_API::s_dummyVAO = GLUtil::k_invalidObjectID;
GLuint GL_API::s_maxTextureUnits = 0;
GLuint GL_API::s_maxAtomicBufferBindingIndices = 0u;
GLuint GL_API::s_maxAttribBindings = 0u;
GLuint GL_API::s_maxFBOAttachments = 0u;
GLuint GL_API::s_maxAnisotropicFilteringLevel = 0u;
GLuint GL_API::s_maxWorgroupInvocations = 0u;
GLuint GL_API::s_maxComputeSharedMemory = 0u;
GLuint GL_API::s_maxWorgroupCount[3] = { 0u, 0u, 0u };
GLuint GL_API::s_maxWorgroupSize[3] = { 0u, 0u, 0u };
bool GL_API::s_UseBindlessTextures = false;
bool GL_API::s_DebugBindlessTextures = false;
SharedMutex GL_API::s_mipmapQueueSetLock;
eastl::unordered_set<GLuint> GL_API::s_mipmapQueue;

vector<GL_API::ResidentTexture> GL_API::s_residentTextures;

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
        stateTracker.bindTextures(0, s_maxTextureUnits - 1, TextureType::COUNT, nullptr, nullptr);
        stateTracker.setPixelPackUnpackAlignment();
        stateTracker._activePixelBuffer = nullptr;
    }

    stateTracker.setActiveVAO(0);
    stateTracker.setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    stateTracker.setActiveFB(RenderTarget::RenderTargetUsage::RT_READ_WRITE, 0);
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
    stateTracker.setActiveProgram(0u);
    stateTracker.setActiveShaderPipeline(0u);
    stateTracker.setStateBlock(RenderStateBlock::defaultHash());
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
                GetStateTracker().setActiveProgram(0u);
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