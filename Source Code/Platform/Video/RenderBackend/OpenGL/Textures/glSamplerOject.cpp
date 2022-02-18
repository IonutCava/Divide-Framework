#include "stdafx.h"

#include "Headers/glSamplerObject.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"


namespace Divide {

GLuint glSamplerObject::construct(const SamplerDescriptor& descriptor) {
    GLuint samplerID = 0;

    glCreateSamplers(1, &samplerID);
    glSamplerParameterf(samplerID, GL_TEXTURE_LOD_BIAS, to_F32(descriptor.biasLOD()));
    glSamplerParameterf(samplerID, GL_TEXTURE_MIN_LOD, to_F32(descriptor.minLOD()));
    glSamplerParameterf(samplerID, GL_TEXTURE_MAX_LOD, to_F32(descriptor.maxLOD()));
    glSamplerParameteri(samplerID, GL_TEXTURE_MIN_FILTER, to_U32(GLUtil::glTextureFilterTable[to_U32(descriptor.minFilter())]));
    glSamplerParameteri(samplerID, GL_TEXTURE_MAG_FILTER, to_U32(GLUtil::glTextureFilterTable[to_U32(descriptor.magFilter())]));
    glSamplerParameteri(samplerID, GL_TEXTURE_WRAP_S, to_U32(GLUtil::glWrapTable[to_U32(descriptor.wrapU())]));
    glSamplerParameteri(samplerID, GL_TEXTURE_WRAP_T, to_U32(GLUtil::glWrapTable[to_U32(descriptor.wrapV())]));
    glSamplerParameteri(samplerID, GL_TEXTURE_WRAP_R, to_U32(GLUtil::glWrapTable[to_U32(descriptor.wrapW())]));

    if (descriptor.wrapU() == TextureWrap::CLAMP_TO_BORDER ||
        descriptor.wrapV() == TextureWrap::CLAMP_TO_BORDER ||
        descriptor.wrapW() == TextureWrap::CLAMP_TO_BORDER)
    {
        glSamplerParameterfv(samplerID, GL_TEXTURE_BORDER_COLOR, descriptor.borderColour()._v);
    }

    if (descriptor.useRefCompare()) {
        glSamplerParameteri(samplerID, GL_TEXTURE_COMPARE_FUNC, to_U32(GLUtil::glCompareFuncTable[to_U32(descriptor.cmpFunc())]));
        glSamplerParameteri(samplerID, GL_TEXTURE_COMPARE_MODE, to_base(GL_COMPARE_REF_TO_TEXTURE));
    } else {
        glSamplerParameteri(samplerID, GL_TEXTURE_COMPARE_MODE, to_base(GL_NONE));
    }

    const GLfloat minAnisotropy = std::min<GLfloat>(to_F32(descriptor.anisotropyLevel()), to_F32(GFXDevice::GetDeviceInformation()._maxAnisotropy));
    if (minAnisotropy > 1) {
        glSamplerParameterf(samplerID, GL_TEXTURE_MAX_ANISOTROPY, minAnisotropy);
    }

    return samplerID;
}

void glSamplerObject::destruct(GLuint& handle) {
    if (handle > 0) {
        glDeleteSamplers(1, &handle);
        handle = 0;
    }
}

};