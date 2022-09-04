#include "stdafx.h"

#include "Headers/glSamplerObject.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"


namespace Divide {
namespace {
    [[nodiscard]] inline GLenum getMagFilterMode(const TextureFilter magFilter) {
        assert(magFilter != TextureFilter::COUNT);

        return magFilter == TextureFilter::LINEAR ? GL_LINEAR : GL_NEAREST;
    }

    [[nodiscard]] inline GLenum getMinFilterMode(const TextureFilter minFilter, const TextureMipSampling mipSampling) {
        assert(minFilter != TextureFilter::COUNT);
        assert(mipSampling != TextureMipSampling::COUNT);

        if (mipSampling == TextureMipSampling::NONE) {
            return getMagFilterMode(minFilter);
        }

        if (minFilter == TextureFilter::NEAREST) {
            return mipSampling == TextureMipSampling::LINEAR ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
        }

        return mipSampling == TextureMipSampling::LINEAR ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
    }
};

GLuint glSamplerObject::Construct(const SamplerDescriptor& descriptor) {
    GLuint samplerID = 0;

    glCreateSamplers(1, &samplerID);
    glSamplerParameterf(samplerID, GL_TEXTURE_LOD_BIAS, descriptor.biasLOD());
    glSamplerParameterf(samplerID, GL_TEXTURE_MIN_LOD, descriptor.minLOD());
    glSamplerParameterf(samplerID, GL_TEXTURE_MAX_LOD, descriptor.maxLOD());
    glSamplerParameteri(samplerID, GL_TEXTURE_MIN_FILTER, getMinFilterMode(descriptor.minFilter(), descriptor.mipSampling()));
    glSamplerParameteri(samplerID, GL_TEXTURE_MAG_FILTER, getMagFilterMode(descriptor.magFilter()));
    glSamplerParameteri(samplerID, GL_TEXTURE_WRAP_S, to_U32(GLUtil::glWrapTable[to_U32(descriptor.wrapU())]));
    glSamplerParameteri(samplerID, GL_TEXTURE_WRAP_T, to_U32(GLUtil::glWrapTable[to_U32(descriptor.wrapV())]));
    glSamplerParameteri(samplerID, GL_TEXTURE_WRAP_R, to_U32(GLUtil::glWrapTable[to_U32(descriptor.wrapW())]));

    constexpr GLint black_transparent_int[] = { 0, 0, 0, 0 };
    constexpr GLint black_opaque_int[] = { 0, 0, 0, 1 };
    constexpr GLint white_opaque_int[] = { 1, 1, 1, 1 };
    constexpr GLfloat black_transparent_float[] = { 0.f, 0.f, 0.f, 0.f };
    constexpr GLfloat black_opaque_float[] = { 0.f, 0.f, 0.f, 1.f };
    constexpr GLfloat white_opaque_float[] = { 1.f, 1.f, 1.f, 1.f };

    switch (descriptor.borderColour()) {
        default:
        case TextureBorderColour::TRANSPARENT_BLACK_INT: glSamplerParameteriv(samplerID, GL_TEXTURE_BORDER_COLOR, black_transparent_int);   break;
        case TextureBorderColour::TRANSPARENT_BLACK_F32: glSamplerParameterfv(samplerID, GL_TEXTURE_BORDER_COLOR, black_transparent_float); break;
        case TextureBorderColour::OPAQUE_BLACK_INT     : glSamplerParameteriv(samplerID, GL_TEXTURE_BORDER_COLOR, black_opaque_int);        break;
        case TextureBorderColour::OPAQUE_BLACK_F32     : glSamplerParameterfv(samplerID, GL_TEXTURE_BORDER_COLOR, black_opaque_float);      break;
        case TextureBorderColour::OPAQUE_WHITE_INT     : glSamplerParameteriv(samplerID, GL_TEXTURE_BORDER_COLOR, white_opaque_int);        break;
        case TextureBorderColour::OPAQUE_WHITE_F32     : glSamplerParameterfv(samplerID, GL_TEXTURE_BORDER_COLOR, white_opaque_float);      break;
        case TextureBorderColour::CUSTOM_INT: {
            GLuint customColour[] = {
                descriptor.customBorderColour().r,
                descriptor.customBorderColour().g,
                descriptor.customBorderColour().b,
                descriptor.customBorderColour().a
            };
            glSamplerParameterIuiv(samplerID, GL_TEXTURE_BORDER_COLOR, customColour);
        } break;
        case TextureBorderColour::CUSTOM_F32: {
            const FColour4 fColour = Util::ToFloatColour(descriptor.customBorderColour());
            glSamplerParameterfv(samplerID, GL_TEXTURE_BORDER_COLOR, fColour._v);
        } break;
    }

    if (descriptor.useRefCompare()) {
        glSamplerParameteri(samplerID, GL_TEXTURE_COMPARE_FUNC, to_U32(GLUtil::glCompareFuncTable[to_U32(descriptor.cmpFunc())]));
        glSamplerParameteri(samplerID, GL_TEXTURE_COMPARE_MODE, to_base(GL_COMPARE_REF_TO_TEXTURE));
    } else {
        glSamplerParameteri(samplerID, GL_TEXTURE_COMPARE_MODE, to_base(GL_NONE));
    }

    const F32 maxAnisotropy = std::min(to_F32(descriptor.anisotropyLevel()), to_F32(GFXDevice::GetDeviceInformation()._maxAnisotropy));
    if (maxAnisotropy > 1.f) {
        glSamplerParameterf(samplerID, GL_TEXTURE_MAX_ANISOTROPY, maxAnisotropy);
    }

    return samplerID;
}

void glSamplerObject::Destruct(GLuint& handle) {
    if (handle > 0) {
        glDeleteSamplers(1, &handle);
        handle = 0;
    }
}

};