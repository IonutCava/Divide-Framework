

#include "Headers/glSamplerObject.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

namespace {
    [[nodiscard]] inline gl46core::GLenum getMagFilterMode(const TextureFilter magFilter)
    {
        assert(magFilter != TextureFilter::COUNT);

        return magFilter == TextureFilter::LINEAR ? gl46core::GL_LINEAR : gl46core::GL_NEAREST;
    }

    [[nodiscard]] inline gl46core::GLenum getMinFilterMode(const TextureFilter minFilter, const TextureMipSampling mipSampling)
    {
        assert(minFilter != TextureFilter::COUNT);
        assert(mipSampling != TextureMipSampling::COUNT);

        if (mipSampling == TextureMipSampling::NONE)
        {
            return getMagFilterMode(minFilter);
        }

        if (minFilter == TextureFilter::NEAREST)
        {
            return mipSampling == TextureMipSampling::LINEAR ? gl46core::GL_NEAREST_MIPMAP_LINEAR : gl46core::GL_NEAREST_MIPMAP_NEAREST;
        }

        return mipSampling == TextureMipSampling::LINEAR ? gl46core::GL_LINEAR_MIPMAP_LINEAR : gl46core::GL_LINEAR_MIPMAP_NEAREST;
    }
};

gl46core::GLuint glSamplerObject::Construct(const SamplerDescriptor& descriptor)
{
    gl46core::GLuint samplerID = GL_NULL_HANDLE;

    gl46core::glCreateSamplers(1, &samplerID);
    gl46core::glSamplerParameterf(samplerID, gl46core::GL_TEXTURE_LOD_BIAS, descriptor._lod._bias);
    gl46core::glSamplerParameterf(samplerID, gl46core::GL_TEXTURE_MIN_LOD, descriptor._lod._min);
    gl46core::glSamplerParameterf(samplerID, gl46core::GL_TEXTURE_MAX_LOD, descriptor._lod._max);
    gl46core::glSamplerParameteri(samplerID, gl46core::GL_TEXTURE_MIN_FILTER, getMinFilterMode(descriptor._minFilter, descriptor._mipSampling));
    gl46core::glSamplerParameteri(samplerID, gl46core::GL_TEXTURE_MAG_FILTER, getMagFilterMode(descriptor._magFilter));
    gl46core::glSamplerParameteri(samplerID, gl46core::GL_TEXTURE_WRAP_S, to_U32(GLUtil::glWrapTable[to_U32(descriptor._wrapU)]));
    gl46core::glSamplerParameteri(samplerID, gl46core::GL_TEXTURE_WRAP_T, to_U32(GLUtil::glWrapTable[to_U32(descriptor._wrapV)]));
    gl46core::glSamplerParameteri(samplerID, gl46core::GL_TEXTURE_WRAP_R, to_U32(GLUtil::glWrapTable[to_U32(descriptor._wrapW)]));

    constexpr gl46core::GLint black_transparent_int[] = { 0, 0, 0, 0 };
    constexpr gl46core::GLint black_opaque_int[] = { 0, 0, 0, 1 };
    constexpr gl46core::GLint white_opaque_int[] = { 1, 1, 1, 1 };
    constexpr gl46core::GLfloat black_transparent_float[] = { 0.f, 0.f, 0.f, 0.f };
    constexpr gl46core::GLfloat black_opaque_float[] = { 0.f, 0.f, 0.f, 1.f };
    constexpr gl46core::GLfloat white_opaque_float[] = { 1.f, 1.f, 1.f, 1.f };

    switch (descriptor._borderColour)
    {
        default:
        case TextureBorderColour::TRANSPARENT_BLACK_INT: gl46core::glSamplerParameteriv(samplerID, gl46core::GL_TEXTURE_BORDER_COLOR, black_transparent_int);   break;
        case TextureBorderColour::TRANSPARENT_BLACK_F32: gl46core::glSamplerParameterfv(samplerID, gl46core::GL_TEXTURE_BORDER_COLOR, black_transparent_float); break;
        case TextureBorderColour::OPAQUE_BLACK_INT     : gl46core::glSamplerParameteriv(samplerID, gl46core::GL_TEXTURE_BORDER_COLOR, black_opaque_int);        break;
        case TextureBorderColour::OPAQUE_BLACK_F32     : gl46core::glSamplerParameterfv(samplerID, gl46core::GL_TEXTURE_BORDER_COLOR, black_opaque_float);      break;
        case TextureBorderColour::OPAQUE_WHITE_INT     : gl46core::glSamplerParameteriv(samplerID, gl46core::GL_TEXTURE_BORDER_COLOR, white_opaque_int);        break;
        case TextureBorderColour::OPAQUE_WHITE_F32     : gl46core::glSamplerParameterfv(samplerID, gl46core::GL_TEXTURE_BORDER_COLOR, white_opaque_float);      break;
        case TextureBorderColour::CUSTOM_INT:
        {
            gl46core::GLuint customColour[] =
            {
                descriptor._customBorderColour.r,
                descriptor._customBorderColour.g,
                descriptor._customBorderColour.b,
                descriptor._customBorderColour.a
            };
            gl46core::glSamplerParameterIuiv(samplerID, gl46core::GL_TEXTURE_BORDER_COLOR, customColour);
        } break;
        case TextureBorderColour::CUSTOM_F32:
        {
            const FColour4 fColour = Util::ToFloatColour(descriptor._customBorderColour);
            gl46core::glSamplerParameterfv(samplerID, gl46core::GL_TEXTURE_BORDER_COLOR, fColour._v);
        } break;
    }

    if (descriptor._depthCompareFunc != ComparisonFunction::COUNT)
    {
        gl46core::glSamplerParameteri( samplerID, gl46core::GL_TEXTURE_COMPARE_FUNC, to_base( GLUtil::glCompareFuncTable[to_base( descriptor._depthCompareFunc )] ) );
        gl46core::glSamplerParameteri(samplerID, gl46core::GL_TEXTURE_COMPARE_MODE, to_base( gl46core::GL_COMPARE_REF_TO_TEXTURE));
    }
    else
    {
        gl46core::glSamplerParameteri(samplerID, gl46core::GL_TEXTURE_COMPARE_MODE, to_base( gl46core::GL_NONE));
    }

    const F32 maxAnisotropy = std::min(to_F32(descriptor._anisotropyLevel), to_F32(GFXDevice::GetDeviceInformation()._maxAnisotropy));
    if ( descriptor._mipSampling != TextureMipSampling::NONE && maxAnisotropy > 0.f )
    {
        gl46core::glSamplerParameterf(samplerID, gl46core::GL_TEXTURE_MAX_ANISOTROPY, maxAnisotropy);
    }
    else
    {
        gl46core::glSamplerParameterf( samplerID, gl46core::GL_TEXTURE_MAX_ANISOTROPY, 1.f );
    }

    if constexpr ( Config::ENABLE_GPU_VALIDATION )
    {
        gl46core::glObjectLabel( gl46core::GL_SAMPLER,
                                 samplerID,
                                 -1,
                                 Util::StringFormat("SAMPLER_{}", GetHash(descriptor)).c_str() );
    }

    return samplerID;
}

void glSamplerObject::Destruct( gl46core::GLuint& handle)
{
    if (handle != GL_NULL_HANDLE )
    {
        gl46core::glDeleteSamplers(1, &handle);
        handle = GL_NULL_HANDLE;
    }
}

} //namespace Divide
