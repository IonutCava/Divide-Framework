#include "stdafx.h"

#include "Headers/glPixelBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

size_t glPixelBuffer::SizeOf(const GLenum dataType) noexcept {
    switch (dataType) {
        case GL_FLOAT:
            return sizeof(GLfloat);
        case GL_UNSIGNED_BYTE:
            return sizeof(GLubyte);
        case GL_UNSIGNED_INT:
            return sizeof(GLuint);
        case GL_UNSIGNED_SHORT:
            return sizeof(GLushort);
        case GL_BYTE:
            return sizeof(GLbyte);
        case GL_SHORT:
            return sizeof(GLshort);
        case GL_INT:
            return sizeof(GLint);
        default:
            break;
    };
    return 0;
}

glPixelBuffer::glPixelBuffer(GFXDevice& context, const PBType type, const char* name)
    : PixelBuffer(context, type, name)
    , _bufferSize(0)
    , _dataSizeBytes(0)
    , _dataType(GL_NONE)
    , _format(GL_NONE)
    , _internalFormat(GL_NONE)
{
    switch (_pbtype) {
        case PBType::PB_TEXTURE_1D:
            _textureType = TextureType::TEXTURE_1D;
            break;
        case PBType::PB_TEXTURE_2D:
            _textureType = TextureType::TEXTURE_2D;
            break;
        case PBType::PB_TEXTURE_3D:
            _textureType = TextureType::TEXTURE_3D;
            break;
        default:
            Console::errorfn(Locale::Get(_ID("ERROR_PB_INVALID_TYPE")));
            break;
    };
}

glPixelBuffer::~glPixelBuffer()
{
    GL_API::DeleteTextures(1, &_textureID, _textureType);
    GLUtil::freeBuffer(_pixelBufferHandle);
}

bufferPtr glPixelBuffer::begin() const {
    GL_API::GetStateTracker().setPixelPackUnpackAlignment();
    glNamedBufferSubData(_pixelBufferHandle, 0, _bufferSize, nullptr);
    GL_API::GetStateTracker().setActiveBuffer(GL_PIXEL_UNPACK_BUFFER, _pixelBufferHandle);

    switch (_pbtype) {
        case PBType::PB_TEXTURE_1D:
            glTextureSubImage1D(_textureID,
                                0,
                                0,
                                _width,
                                _format,
                                _dataType,
                                nullptr);
            break;
        case PBType::PB_TEXTURE_2D:
            glTextureSubImage2D(_textureID,
                                0,
                                0,
                                0,
                                _width,
                                _height,
                                _format,
                                _dataType,
                                nullptr);
            break;
        case PBType::PB_TEXTURE_3D:
            glTextureSubImage3D(_textureID,
                                0,
                                0,
                                0,
                                0,
                                _width,
                                _height,
                                _depth,
                                _format,
                                _dataType,
                                nullptr);
            break;
        default: break;;
    };

    return glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
}

void glPixelBuffer::end() const {
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);  // release the mapped buffer
    GL_API::GetStateTracker().setActiveBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

bool glPixelBuffer::create(GLushort width, GLushort height, const GLushort depth,
                           const GFXImageFormat formatEnum,
                           const GFXDataFormat dataTypeEnum,
                           const bool normalized) {
    const GLenum textureTypeEnum = GLUtil::glTextureTypeTable[to_U32(_textureType)];
    _internalFormat = GLUtil::internalFormat(formatEnum, dataTypeEnum, false, normalized);
    _format = GLUtil::glImageFormatTable[to_U32(formatEnum)];
    _dataType = GLUtil::glDataFormat[to_U32(dataTypeEnum)];

    Console::printfn(Locale::Get(_ID("GL_PB_GEN")), width, height);
    _width = width;
    _height = height;
    _depth = depth;

    _bufferSize = _width * 4;
    switch (_pbtype) {
        case PBType::PB_TEXTURE_2D:
            _bufferSize *= _height;
            break;
        case PBType::PB_TEXTURE_3D:
            _bufferSize *= _height * _depth;
            break;
        default: break;
    };

    switch (_dataType) {
        case GL_SHORT:
        case GL_HALF_FLOAT:
        case GL_UNSIGNED_SHORT:
            _dataSizeBytes = 2;
            break;
        case GL_FLOAT:
        case GL_INT:
        case GL_UNSIGNED_INT:
            _dataSizeBytes = 4;
            break;
        default:
            break;
    }
    _bufferSize *= _dataSizeBytes;

    GL_API::DeleteTextures(1, &_textureID, _textureType);

    glCreateTextures(textureTypeEnum, 1, &_textureID);
    glTextureParameteri(_textureID, GL_GENERATE_MIPMAP, 0);
    glTextureParameteri(_textureID, GL_TEXTURE_MIN_FILTER, to_I32(GL_NEAREST));
    glTextureParameteri(_textureID, GL_TEXTURE_MAG_FILTER, to_I32(GL_NEAREST));
    glTextureParameteri(_textureID, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(_textureID, GL_TEXTURE_MAX_LEVEL, 1000);
    glTextureParameteri(_textureID, GL_TEXTURE_WRAP_S, to_I32(GL_REPEAT));

    if (_pbtype != PBType::PB_TEXTURE_1D) {
        glTextureParameteri(_textureID, GL_TEXTURE_WRAP_T, to_I32(GL_REPEAT));
    }
    if (_pbtype == PBType::PB_TEXTURE_3D) {
        glTextureParameteri(_textureID, GL_TEXTURE_WRAP_R, to_I32(GL_REPEAT));
    }

    const U16 mipLevels = to_U16(std::floor(std::log2(std::max(_width, _height))) + 1);
    GL_API::GetStateTracker().setPixelPackUnpackAlignment();
    switch (_pbtype) {
        case PBType::PB_TEXTURE_1D:
            glTextureStorage1D(_textureID, mipLevels, _internalFormat, _width);
            break;
        case PBType::PB_TEXTURE_2D:
            glTextureStorage2D(_textureID, mipLevels, _internalFormat, _width, _height);
            break;
        case PBType::PB_TEXTURE_3D:
            glTextureStorage3D(_textureID, mipLevels, _internalFormat, _width, _height, _depth);
            break;
        default: break;
    };

    if (_pixelBufferHandle > 0) {
        GLUtil::freeBuffer(_pixelBufferHandle);
    }

    GLUtil::createAndAllocBuffer(_bufferSize, 
                                 GL_STREAM_DRAW,
                                 _pixelBufferHandle,
                                 {nullptr, 0u},
                                 _name.empty() ? nullptr : _name.c_str());

    return _pixelBufferHandle != 0 && _textureID != 0;
}

void glPixelBuffer::updatePixels(const GLfloat* const pixels, const GLuint pixelCount) {
    if (pixels && pixelCount == _bufferSize / _dataSizeBytes) {
        const bufferPtr ptr = begin();
        memcpy(ptr, pixels, _bufferSize);
        end();
    }
}
};
