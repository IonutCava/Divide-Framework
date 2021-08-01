#include "stdafx.h"

#include "Headers/glResources.h"

#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/GLIM/glim.h"

namespace Divide {

glObject::glObject(const glObjectType type, GFXDevice& context)
    : _type(type)
{
}

void VAOBindings::init(const U32 maxBindings) {
    _maxBindings = maxBindings;
}

VAOBindings::VAOData* VAOBindings::getVAOData(const GLuint vao) {
    VAOData* data;
    if (vao == _cachedVao) {
        data = _cachedData;
    } else {
        data = &_bindings[vao];
        _cachedData = data;
        _cachedVao = vao;
    }

    return data;
}

GLuint VAOBindings::instanceDivisor(const GLuint vao, const GLuint index) {
    VAOData* data = getVAOData(vao);

    const size_t count = data->second.size();
    if (count > 0) {
        assert(index <= count);
        return data->second[index];
    }

    assert(_maxBindings != 0);
    data->second.resize(_maxBindings);
    return data->second.front();
}

void VAOBindings::instanceDivisor(const GLuint vao, const GLuint index, const GLuint divisor) {
    VAOData* data = getVAOData(vao);

    const size_t count = data->second.size();
    assert(count > 0 && count > index);
    ACKNOWLEDGE_UNUSED(count);

    data->second[index] = divisor;
}

const VAOBindings::BufferBindingParams& VAOBindings::bindingParams(const GLuint vao, const GLuint index) {
    VAOData* data = getVAOData(vao);
   
    const size_t count = data->first.size();
    if (count > 0) {
        assert(index <= count);
        return data->first[index];
    }

    assert(_maxBindings != 0);
    data->first.resize(_maxBindings);
    return data->first.front();
}

void VAOBindings::bindingParams(const GLuint vao, const GLuint index, const BufferBindingParams& newParams) {
    VAOData* data = getVAOData(vao);

    const size_t count = data->first.size();
    assert(count > 0 && count > index);
    ACKNOWLEDGE_UNUSED(count);

    data->first[index] = newParams;
}

namespace GLUtil {

/*-----------Object Management----*/
GLuint k_invalidObjectID = GL_INVALID_INDEX;
GLuint s_lastQueryResult = GL_INVALID_INDEX;

const DisplayWindow* s_glMainRenderWindow;
thread_local SDL_GLContext s_glSecondaryContext = nullptr;
Mutex s_glSecondaryContextMutex;

std::array<GLenum, to_base(BlendProperty::COUNT)> glBlendTable;
std::array<GLenum, to_base(BlendOperation::COUNT)> glBlendOpTable;
std::array<GLenum, to_base(ComparisonFunction::COUNT)> glCompareFuncTable;
std::array<GLenum, to_base(StencilOperation::COUNT)> glStencilOpTable;
std::array<GLenum, to_base(CullMode::COUNT)> glCullModeTable;
std::array<GLenum, to_base(FillMode::COUNT)> glFillModeTable;
std::array<GLenum, to_base(TextureType::COUNT)> glTextureTypeTable;
std::array<GLenum, to_base(GFXImageFormat::COUNT)> glImageFormatTable;
std::array<GLenum, to_base(PrimitiveType::COUNT)> glPrimitiveTypeTable;
std::array<GLenum, to_base(GFXDataFormat::COUNT)> glDataFormat;
std::array<GLenum, to_base(TextureWrap::COUNT)> glWrapTable;
std::array<GLenum, to_base(TextureFilter::COUNT)> glTextureFilterTable;
std::array<NS_GLIM::GLIM_ENUM, to_base(PrimitiveType::COUNT)> glimPrimitiveType;
std::array<GLenum, to_base(ShaderType::COUNT)> glShaderStageTable;
std::array<UseProgramStageMask, to_base(ShaderType::COUNT) + 1> glProgramStageMask;
std::map<GLenum, size_t> glTypeSizeInBytes;

void fillEnumTables() {
    glBlendTable[to_base(BlendProperty::ZERO)] = GL_ZERO;
    glBlendTable[to_base(BlendProperty::ONE)] = GL_ONE;
    glBlendTable[to_base(BlendProperty::SRC_COLOR)] = GL_SRC_COLOR;
    glBlendTable[to_base(BlendProperty::INV_SRC_COLOR)] = GL_ONE_MINUS_SRC_COLOR;
    glBlendTable[to_base(BlendProperty::SRC_ALPHA)] = GL_SRC_ALPHA;
    glBlendTable[to_base(BlendProperty::INV_SRC_ALPHA)] = GL_ONE_MINUS_SRC_ALPHA;
    glBlendTable[to_base(BlendProperty::DEST_ALPHA)] = GL_DST_ALPHA;
    glBlendTable[to_base(BlendProperty::INV_DEST_ALPHA)] = GL_ONE_MINUS_DST_ALPHA;
    glBlendTable[to_base(BlendProperty::DEST_COLOR)] = GL_DST_COLOR;
    glBlendTable[to_base(BlendProperty::INV_DEST_COLOR)] = GL_ONE_MINUS_DST_COLOR;
    glBlendTable[to_base(BlendProperty::SRC_ALPHA_SAT)] = GL_SRC_ALPHA_SATURATE;

    glBlendOpTable[to_base(BlendOperation::ADD)] = GL_FUNC_ADD;
    glBlendOpTable[to_base(BlendOperation::SUBTRACT)] = GL_FUNC_SUBTRACT;
    glBlendOpTable[to_base(BlendOperation::REV_SUBTRACT)] = GL_FUNC_REVERSE_SUBTRACT;
    glBlendOpTable[to_base(BlendOperation::MIN)] = GL_MIN;
    glBlendOpTable[to_base(BlendOperation::MAX)] = GL_MAX;

    glCompareFuncTable[to_base(ComparisonFunction::NEVER)] = GL_NEVER;
    glCompareFuncTable[to_base(ComparisonFunction::LESS)] = GL_LESS;
    glCompareFuncTable[to_base(ComparisonFunction::EQUAL)] = GL_EQUAL;
    glCompareFuncTable[to_base(ComparisonFunction::LEQUAL)] = GL_LEQUAL;
    glCompareFuncTable[to_base(ComparisonFunction::GREATER)] = GL_GREATER;
    glCompareFuncTable[to_base(ComparisonFunction::NEQUAL)] = GL_NOTEQUAL;
    glCompareFuncTable[to_base(ComparisonFunction::GEQUAL)] = GL_GEQUAL;
    glCompareFuncTable[to_base(ComparisonFunction::ALWAYS)] = GL_ALWAYS;

    glStencilOpTable[to_base(StencilOperation::KEEP)] = GL_KEEP;
    glStencilOpTable[to_base(StencilOperation::ZERO)] = GL_ZERO;
    glStencilOpTable[to_base(StencilOperation::REPLACE)] = GL_REPLACE;
    glStencilOpTable[to_base(StencilOperation::INCR)] = GL_INCR;
    glStencilOpTable[to_base(StencilOperation::DECR)] = GL_DECR;
    glStencilOpTable[to_base(StencilOperation::INV)] = GL_INVERT;
    glStencilOpTable[to_base(StencilOperation::INCR_WRAP)] = GL_INCR_WRAP;
    glStencilOpTable[to_base(StencilOperation::DECR_WRAP)] = GL_DECR_WRAP;

    glCullModeTable[to_base(CullMode::CW)] = GL_BACK;
    glCullModeTable[to_base(CullMode::CCW)] = GL_FRONT;
    glCullModeTable[to_base(CullMode::ALL)] = GL_FRONT_AND_BACK;
    glCullModeTable[to_base(CullMode::NONE)] = GL_NONE;

    glFillModeTable[to_base(FillMode::POINT)] = GL_POINT;
    glFillModeTable[to_base(FillMode::WIREFRAME)] = GL_LINE;
    glFillModeTable[to_base(FillMode::SOLID)] = GL_FILL;

    glTextureTypeTable[to_base(TextureType::TEXTURE_1D)] = GL_TEXTURE_1D;
    glTextureTypeTable[to_base(TextureType::TEXTURE_2D)] = GL_TEXTURE_2D;
    glTextureTypeTable[to_base(TextureType::TEXTURE_3D)] = GL_TEXTURE_3D;
    glTextureTypeTable[to_base(TextureType::TEXTURE_CUBE_MAP)] = GL_TEXTURE_CUBE_MAP;
    glTextureTypeTable[to_base(TextureType::TEXTURE_2D_ARRAY)] = GL_TEXTURE_2D_ARRAY;
    glTextureTypeTable[to_base(TextureType::TEXTURE_CUBE_ARRAY)] = GL_TEXTURE_CUBE_MAP_ARRAY;
    glTextureTypeTable[to_base(TextureType::TEXTURE_2D_MS)] = GL_TEXTURE_2D_MULTISAMPLE;
    glTextureTypeTable[to_base(TextureType::TEXTURE_2D_ARRAY_MS)] = GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
    glImageFormatTable[to_base(GFXImageFormat::RED)] = GL_RED;
    glImageFormatTable[to_base(GFXImageFormat::RG)] = GL_RG;
    glImageFormatTable[to_base(GFXImageFormat::RGB)] = GL_RGB;
    glImageFormatTable[to_base(GFXImageFormat::BGR)] = GL_BGR;
    glImageFormatTable[to_base(GFXImageFormat::BGRA)] = GL_BGRA;
    glImageFormatTable[to_base(GFXImageFormat::RGBA)] = GL_RGBA;
    glImageFormatTable[to_base(GFXImageFormat::DEPTH_COMPONENT)] = GL_DEPTH_COMPONENT;
    glImageFormatTable[to_base(GFXImageFormat::COMPRESSED_RGB_DXT1)] = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    glImageFormatTable[to_base(GFXImageFormat::COMPRESSED_RGBA_DXT1)] = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    glImageFormatTable[to_base(GFXImageFormat::COMPRESSED_RGBA_DXT3)] = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    glImageFormatTable[to_base(GFXImageFormat::COMPRESSED_RGBA_DXT5)] = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    
    glPrimitiveTypeTable[to_base(PrimitiveType::API_POINTS)] = GL_POINTS;
    glPrimitiveTypeTable[to_base(PrimitiveType::LINES)] = GL_LINES;
    glPrimitiveTypeTable[to_base(PrimitiveType::LINE_LOOP)] =  GL_LINE_LOOP;
    glPrimitiveTypeTable[to_base(PrimitiveType::LINE_STRIP)] = GL_LINE_STRIP;
    glPrimitiveTypeTable[to_base(PrimitiveType::TRIANGLES)] = GL_TRIANGLES;
    glPrimitiveTypeTable[to_base(PrimitiveType::TRIANGLE_STRIP)] = GL_TRIANGLE_STRIP;
    glPrimitiveTypeTable[to_base(PrimitiveType::TRIANGLE_FAN)] = GL_TRIANGLE_FAN;
    // glPrimitiveTypeTable[to_base(PrimitiveType::QUADS)] = GL_QUADS; //<Deprecated
    glPrimitiveTypeTable[to_base(PrimitiveType::QUAD_STRIP)] = GL_QUAD_STRIP;
    glPrimitiveTypeTable[to_base(PrimitiveType::POLYGON)] = GL_POLYGON;
    glPrimitiveTypeTable[to_base(PrimitiveType::PATCH)] = GL_PATCHES;

    glDataFormat[to_base(GFXDataFormat::UNSIGNED_BYTE)] = GL_UNSIGNED_BYTE;
    glDataFormat[to_base(GFXDataFormat::UNSIGNED_SHORT)] = GL_UNSIGNED_SHORT;
    glDataFormat[to_base(GFXDataFormat::UNSIGNED_INT)] = GL_UNSIGNED_INT;
    glDataFormat[to_base(GFXDataFormat::SIGNED_BYTE)] = GL_BYTE;
    glDataFormat[to_base(GFXDataFormat::SIGNED_SHORT)] = GL_SHORT;
    glDataFormat[to_base(GFXDataFormat::SIGNED_INT)] = GL_INT;
    glDataFormat[to_base(GFXDataFormat::FLOAT_16)] = GL_HALF_FLOAT;
    glDataFormat[to_base(GFXDataFormat::FLOAT_32)] = GL_FLOAT;

    glWrapTable[to_base(TextureWrap::MIRROR_REPEAT)] = GL_MIRRORED_REPEAT;
    glWrapTable[to_base(TextureWrap::REPEAT)] = GL_REPEAT;
    glWrapTable[to_base(TextureWrap::CLAMP)] = GL_CLAMP;
    glWrapTable[to_base(TextureWrap::CLAMP_TO_EDGE)] = GL_CLAMP_TO_EDGE;
    glWrapTable[to_base(TextureWrap::CLAMP_TO_BORDER)] = GL_CLAMP_TO_BORDER;
    glWrapTable[to_base(TextureWrap::DECAL)] = GL_DECAL;

    glTextureFilterTable[to_base(TextureFilter::LINEAR)] = GL_LINEAR;
    glTextureFilterTable[to_base(TextureFilter::NEAREST)] = GL_NEAREST;
    glTextureFilterTable[to_base(TextureFilter::NEAREST_MIPMAP_NEAREST)] = GL_NEAREST_MIPMAP_NEAREST;
    glTextureFilterTable[to_base(TextureFilter::LINEAR_MIPMAP_NEAREST)] = GL_LINEAR_MIPMAP_NEAREST;
    glTextureFilterTable[to_base(TextureFilter::NEAREST_MIPMAP_LINEAR)] = GL_NEAREST_MIPMAP_LINEAR;
    glTextureFilterTable[to_base(TextureFilter::LINEAR_MIPMAP_LINEAR)] = GL_LINEAR_MIPMAP_LINEAR;

    glimPrimitiveType[to_base(PrimitiveType::API_POINTS)] = NS_GLIM::GLIM_ENUM::GLIM_POINTS;
    glimPrimitiveType[to_base(PrimitiveType::LINES)] = NS_GLIM::GLIM_ENUM::GLIM_LINES;
    glimPrimitiveType[to_base(PrimitiveType::LINE_LOOP)] = NS_GLIM::GLIM_ENUM::GLIM_LINE_LOOP;
    glimPrimitiveType[to_base(PrimitiveType::LINE_STRIP)] = NS_GLIM::GLIM_ENUM::GLIM_LINE_STRIP;
    glimPrimitiveType[to_base(PrimitiveType::TRIANGLES)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLES;
    glimPrimitiveType[to_base(PrimitiveType::TRIANGLE_STRIP)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLE_STRIP;
    glimPrimitiveType[to_base(PrimitiveType::TRIANGLE_FAN)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLE_FAN;
    glimPrimitiveType[to_base(PrimitiveType::QUAD_STRIP)] = NS_GLIM::GLIM_ENUM::GLIM_QUAD_STRIP;
    glimPrimitiveType[to_base(PrimitiveType::POLYGON)] = NS_GLIM::GLIM_ENUM::GLIM_POLYGON;

    glShaderStageTable[to_base(ShaderType::VERTEX)] = GL_VERTEX_SHADER;
    glShaderStageTable[to_base(ShaderType::FRAGMENT)] = GL_FRAGMENT_SHADER;
    glShaderStageTable[to_base(ShaderType::GEOMETRY)] = GL_GEOMETRY_SHADER;
    glShaderStageTable[to_base(ShaderType::TESSELLATION_CTRL)] = GL_TESS_CONTROL_SHADER;
    glShaderStageTable[to_base(ShaderType::TESSELLATION_EVAL)] = GL_TESS_EVALUATION_SHADER;
    glShaderStageTable[to_base(ShaderType::COMPUTE)] = GL_COMPUTE_SHADER;

    glProgramStageMask[to_base(ShaderType::VERTEX)] = GL_VERTEX_SHADER_BIT;
    glProgramStageMask[to_base(ShaderType::FRAGMENT)] = GL_FRAGMENT_SHADER_BIT;
    glProgramStageMask[to_base(ShaderType::GEOMETRY)] = GL_GEOMETRY_SHADER_BIT;
    glProgramStageMask[to_base(ShaderType::TESSELLATION_CTRL)] = GL_TESS_CONTROL_SHADER_BIT;
    glProgramStageMask[to_base(ShaderType::TESSELLATION_EVAL)] = GL_TESS_EVALUATION_SHADER_BIT;
    glProgramStageMask[to_base(ShaderType::COMPUTE)] = GL_COMPUTE_SHADER_BIT;
    glProgramStageMask[to_base(ShaderType::COUNT)] = GL_NONE_BIT;

    glTypeSizeInBytes[GL_FLOAT] = sizeof(float);
    glTypeSizeInBytes[GL_FLOAT_VEC2] = sizeof(float) * 2;
    glTypeSizeInBytes[GL_FLOAT_VEC3] = sizeof(float) * 3;
    glTypeSizeInBytes[GL_FLOAT_VEC4] = sizeof(float) * 4;

    glTypeSizeInBytes[GL_DOUBLE] = sizeof(double);
    glTypeSizeInBytes[GL_DOUBLE_VEC2] = sizeof(double) * 2;
    glTypeSizeInBytes[GL_DOUBLE_VEC3] = sizeof(double) * 3;
    glTypeSizeInBytes[GL_DOUBLE_VEC4] = sizeof(double) * 4;

    glTypeSizeInBytes[GL_SAMPLER_1D] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_2D] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_3D] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_CUBE] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_1D_SHADOW] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_2D_SHADOW] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_1D_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_2D_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_1D_ARRAY_SHADOW] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_2D_ARRAY_SHADOW] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_2D_MULTISAMPLE] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_2D_MULTISAMPLE_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_CUBE_SHADOW] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_BUFFER] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_2D_RECT] = sizeof(int);
    glTypeSizeInBytes[GL_SAMPLER_2D_RECT_SHADOW] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_1D] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_2D] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_3D] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_CUBE] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_1D_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_2D_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_2D_MULTISAMPLE] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_BUFFER] = sizeof(int);
    glTypeSizeInBytes[GL_INT_SAMPLER_2D_RECT] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_1D] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_2D] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_3D] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_CUBE] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_1D_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_2D_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_BUFFER] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_SAMPLER_2D_RECT] = sizeof(int);
    glTypeSizeInBytes[GL_BOOL] = sizeof(int);
    glTypeSizeInBytes[GL_INT] = sizeof(int);
    glTypeSizeInBytes[GL_BOOL_VEC2] = sizeof(int) * 2;
    glTypeSizeInBytes[GL_INT_VEC2] = sizeof(int) * 2;
    glTypeSizeInBytes[GL_BOOL_VEC3] = sizeof(int) * 3;
    glTypeSizeInBytes[GL_INT_VEC3] = sizeof(int) * 3;
    glTypeSizeInBytes[GL_BOOL_VEC4] = sizeof(int) * 4;
    glTypeSizeInBytes[GL_INT_VEC4] = sizeof(int) * 4;

    glTypeSizeInBytes[GL_UNSIGNED_INT] = sizeof(int);
    glTypeSizeInBytes[GL_UNSIGNED_INT_VEC2] = sizeof(int) * 2;
    glTypeSizeInBytes[GL_UNSIGNED_INT_VEC3] = sizeof(int) * 3;
    glTypeSizeInBytes[GL_UNSIGNED_INT_VEC4] = sizeof(int) * 4;

    glTypeSizeInBytes[GL_FLOAT_MAT2] = sizeof(float) * 4;
    glTypeSizeInBytes[GL_FLOAT_MAT3] = sizeof(float) * 9;
    glTypeSizeInBytes[GL_FLOAT_MAT4] = sizeof(float) * 16;
    glTypeSizeInBytes[GL_FLOAT_MAT2x3] = sizeof(float) * 6;
    glTypeSizeInBytes[GL_FLOAT_MAT2x4] = sizeof(float) * 8;
    glTypeSizeInBytes[GL_FLOAT_MAT3x2] = sizeof(float) * 6;
    glTypeSizeInBytes[GL_FLOAT_MAT3x4] = sizeof(float) * 12;
    glTypeSizeInBytes[GL_FLOAT_MAT4x2] = sizeof(float) * 8;
    glTypeSizeInBytes[GL_FLOAT_MAT4x3] = sizeof(float) * 12;
    glTypeSizeInBytes[GL_DOUBLE_MAT2] = sizeof(double) * 4;
    glTypeSizeInBytes[GL_DOUBLE_MAT3] = sizeof(double) * 9;
    glTypeSizeInBytes[GL_DOUBLE_MAT4] = sizeof(double) * 16;
    glTypeSizeInBytes[GL_DOUBLE_MAT2x3] = sizeof(double) * 6;
    glTypeSizeInBytes[GL_DOUBLE_MAT2x4] = sizeof(double) * 8;
    glTypeSizeInBytes[GL_DOUBLE_MAT3x2] = sizeof(double) * 6;
    glTypeSizeInBytes[GL_DOUBLE_MAT3x4] = sizeof(double) * 12;
    glTypeSizeInBytes[GL_DOUBLE_MAT4x2] = sizeof(double) * 8;
    glTypeSizeInBytes[GL_DOUBLE_MAT4x3] = sizeof(double) * 12;
}

GLenum internalFormat(const GFXImageFormat baseFormat, const GFXDataFormat dataType, const bool srgb, const bool normalized) {
    switch (baseFormat) {
        case GFXImageFormat::RED:{
            assert(!srgb);
            switch (dataType) {
                case GFXDataFormat::UNSIGNED_BYTE: return normalized ? GL_R8 : GL_R8UI;
                case GFXDataFormat::UNSIGNED_SHORT: return normalized ? GL_R16 : GL_R16UI;
                case GFXDataFormat::UNSIGNED_INT: { assert(!normalized && "Format not supported"); return GL_R32UI; }
                case GFXDataFormat::SIGNED_BYTE: return normalized ? GL_R8_SNORM : GL_R8I;
                case GFXDataFormat::SIGNED_SHORT: return normalized ? GL_R16_SNORM : GL_R16I;
                case GFXDataFormat::SIGNED_INT: { assert(!normalized && "Format not supported"); return GL_R32I; }
                case GFXDataFormat::FLOAT_16: return GL_R16F;
                case GFXDataFormat::FLOAT_32: return GL_R32F;
                default: DIVIDE_UNEXPECTED_CALL();
            };
        }break;
        case GFXImageFormat::RG: {
            assert(!srgb);
            switch (dataType) {
                case GFXDataFormat::UNSIGNED_BYTE: return normalized ? GL_RG8 : GL_RG8UI;
                case GFXDataFormat::UNSIGNED_SHORT: return normalized ? GL_RG16 : GL_RG16UI;
                case GFXDataFormat::UNSIGNED_INT: { assert(!normalized && "Format not supported"); return GL_RG32UI; }
                case GFXDataFormat::SIGNED_BYTE: return normalized ? GL_RG8_SNORM : GL_RG8I;
                case GFXDataFormat::SIGNED_SHORT: return normalized ? GL_RG16_SNORM : GL_RG16I;
                case GFXDataFormat::SIGNED_INT: { assert(!normalized && "Format not supported"); return GL_RG32I; }
                case GFXDataFormat::FLOAT_16: return GL_RG16F;
                case GFXDataFormat::FLOAT_32: return GL_RG32F;
                default: DIVIDE_UNEXPECTED_CALL();
            };
        }break;
        case GFXImageFormat::BGR:
        case GFXImageFormat::RGB:
        {
            assert(!srgb || srgb == (dataType == GFXDataFormat::UNSIGNED_BYTE && normalized));
            switch (dataType) {
                case GFXDataFormat::UNSIGNED_BYTE: return normalized ? (srgb ? GL_SRGB8 : GL_RGB8) : GL_RGB8UI;
                case GFXDataFormat::UNSIGNED_SHORT: return normalized ? GL_RGB16 : GL_RGB16UI;
                case GFXDataFormat::UNSIGNED_INT: { assert(!normalized && "Format not supported"); return GL_RGB32UI; }
                case GFXDataFormat::SIGNED_BYTE: return normalized ? GL_RGB8_SNORM : GL_RGB8I;
                case GFXDataFormat::SIGNED_SHORT: return normalized ? GL_RGB16_SNORM : GL_RGB16I;
                case GFXDataFormat::SIGNED_INT: { assert(!normalized && "Format not supported"); return GL_RGB32I; }
                case GFXDataFormat::FLOAT_16: return GL_RGB16F;
                case GFXDataFormat::FLOAT_32: return GL_RGB32F;
                default: DIVIDE_UNEXPECTED_CALL();
            };
        }break;
        case GFXImageFormat::BGRA:
        case GFXImageFormat::RGBA:
        {
            assert(!srgb || srgb == (dataType == GFXDataFormat::UNSIGNED_BYTE && normalized));
            switch (dataType) {
                case GFXDataFormat::UNSIGNED_BYTE: return normalized ? (srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8) : GL_RGBA8UI;
                case GFXDataFormat::UNSIGNED_SHORT: return normalized ? GL_RGBA16 : GL_RGBA16UI;
                case GFXDataFormat::UNSIGNED_INT: { assert(!normalized && "Format not supported"); return GL_RGBA32UI; }
                case GFXDataFormat::SIGNED_BYTE: return normalized ? GL_RGBA8_SNORM : GL_RGBA8I;
                case GFXDataFormat::SIGNED_SHORT: return normalized ? GL_RGBA16_SNORM : GL_RGBA16I;
                case GFXDataFormat::SIGNED_INT: { assert(!normalized && "Format not supported"); return GL_RGBA32I; }
                case GFXDataFormat::FLOAT_16: return GL_RGBA16F;
                case GFXDataFormat::FLOAT_32: return GL_RGBA32F;
                default: DIVIDE_UNEXPECTED_CALL();
            };
        }break;
        case GFXImageFormat::DEPTH_COMPONENT:
        {
            switch (dataType) {
                case GFXDataFormat::SIGNED_BYTE:
                case GFXDataFormat::UNSIGNED_BYTE: return GL_DEPTH_COMPONENT16;
                case GFXDataFormat::SIGNED_SHORT:
                case GFXDataFormat::UNSIGNED_SHORT: return GL_DEPTH_COMPONENT24;
                case GFXDataFormat::SIGNED_INT:
                case GFXDataFormat::UNSIGNED_INT: return GL_DEPTH_COMPONENT32;
                case GFXDataFormat::FLOAT_16:
                case GFXDataFormat::FLOAT_32: return GL_DEPTH_COMPONENT32F;
                default: DIVIDE_UNEXPECTED_CALL();
            };
        }break;
        case GFXImageFormat::COMPRESSED_RGB_DXT1:
        {
            if (srgb) {
                return GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
            }

            return glImageFormatTable[to_base(baseFormat)];
        };
        case GFXImageFormat::COMPRESSED_RGBA_DXT1:
        {
            if (srgb) {
                return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
            }

            return glImageFormatTable[to_base(baseFormat)];
        };
        case GFXImageFormat::COMPRESSED_RGBA_DXT3:
        {
            if (srgb) {
                return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
            }

            return glImageFormatTable[to_base(baseFormat)];
        };
        case GFXImageFormat::COMPRESSED_RGBA_DXT5:
        {
            if (srgb) {
                return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
            }

            return glImageFormatTable[to_base(baseFormat)];
        };
        default: DIVIDE_UNEXPECTED_CALL();
    }

    return GL_NONE;
}

namespace {

void SubmitIndirectCommand(const IndirectDrawCommand& cmd,
                           const U32 drawCount,
                           const GLenum mode,
                           const GLenum internalFormat,
                           const bool drawIndexed,
                           const GLuint cmdBufferOffset)
{
    const size_t offset = cmdBufferOffset * sizeof(IndirectDrawCommand);
    if (drawCount == 1) {
        // We could just submit a multi-draw with a draw count of 1, but this might avoid some CPU overhead.
        // Either I or the driver has to do the count check/loop, but I can profile my own code.
        if (drawIndexed) {
            glDrawElementsIndirect(mode, internalFormat, (bufferPtr)offset);
        } else {
            // This needs a different command buffer and different IndirectDrawCommand (16byte instead of 20)
            glDrawArraysInstancedBaseInstance(mode, cmd.firstIndex, cmd.indexCount, cmd.primCount, cmd.baseInstance);
        }
    } else {
        if (drawIndexed) {
            glMultiDrawElementsIndirect(mode, internalFormat, (bufferPtr)offset, drawCount, sizeof(IndirectDrawCommand));
        } else {
            glMultiDrawArraysIndirect(mode, (bufferPtr)offset, drawCount, sizeof(IndirectDrawCommand));
        }
    }
}

void SubmitDirectCommand(const IndirectDrawCommand& cmd,
                         const U32 drawCount,
                         const GLenum mode,
                         const GLenum internalFormat,
                         const bool drawIndexed,
                         size_t* countData,
                         const bufferPtr indexData) 
{
    if (drawCount > 1 && cmd.primCount == 1) {
        // We could just submit a multi-draw with a draw count of 1, but this might avoid some CPU overhead.
        // Either I or the driver has to do the count check/loop, but I can profile my own code.
        if (drawIndexed) {
            glMultiDrawElements(mode, reinterpret_cast<GLsizei*>(countData), internalFormat, static_cast<void* const*>(indexData), drawCount);
        } else {
            glMultiDrawArrays(mode, static_cast<GLint*>(indexData), reinterpret_cast<GLsizei*>(countData), drawCount);
        }
    } else {
        if (drawIndexed) {
            const size_t elementSize = internalFormat == GL_UNSIGNED_SHORT ? sizeof(GLushort) : sizeof(GLuint);
            for (GLuint i = 0; i < drawCount; ++i) {
                glDrawElementsInstancedBaseVertexBaseInstance(mode,
                                                              cmd.indexCount,
                                                              internalFormat,
                                                              (bufferPtr)(cmd.firstIndex * elementSize),
                                                              cmd.primCount,
                                                              cmd.baseVertex,
                                                              cmd.baseInstance);
            }
        } else {
            for (GLuint i = 0; i < drawCount; ++i) {
                glDrawArraysInstancedBaseInstance(mode,
                                                  cmd.firstIndex,
                                                  cmd.indexCount,
                                                  cmd.primCount,
                                                  cmd.baseInstance);
            }
        }
    }
}

void SubmitRenderCommand(const GLenum primitiveType,
                         const GenericDrawCommand& drawCommand,
                         const bool drawIndexed,
                         const bool useIndirectBuffer,
                         const GLenum internalFormat,
                         size_t* countData,
                         const bufferPtr indexData)
{
    
    if (useIndirectBuffer) {
        SubmitIndirectCommand(drawCommand._cmd, drawCommand._drawCount, primitiveType, internalFormat, drawIndexed, drawCommand._commandOffset);
    } else {
        SubmitDirectCommand(drawCommand._cmd, drawCommand._drawCount, primitiveType, internalFormat, drawIndexed, countData, indexData);
    }
}
}

void SubmitRenderCommand(const GenericDrawCommand& drawCommand,
                         const bool drawIndexed,
                         const bool useIndirectBuffer,
                         const GLenum internalFormat,
                         size_t* countData,
                         const bufferPtr indexData)
{
    DIVIDE_ASSERT(drawCommand._primitiveType != PrimitiveType::COUNT,
                  "GLUtil::submitRenderCommand error: Draw command's type is not valid!");

    GL_API::getStateTracker().toggleRasterization(!isEnabledOption(drawCommand, CmdRenderOptions::RENDER_NO_RASTERIZE));

    if (isEnabledOption(drawCommand, CmdRenderOptions::RENDER_GEOMETRY)) {
        bool runQueries = false;
        glHardwareQueryRing* primitiveQuery = nullptr;
        glHardwareQueryRing* sampleCountQuery = nullptr;
        glHardwareQueryRing* anySamplesQuery = nullptr;

        if (isEnabledOption(drawCommand, CmdRenderOptions::QUERY_PRIMITIVE_COUNT)) {
            primitiveQuery = &GL_API::s_hardwareQueryPool->allocate(GL_PRIMITIVES_GENERATED);
            primitiveQuery->begin();
            runQueries = true;
        }
        if (isEnabledOption(drawCommand, CmdRenderOptions::QUERY_SAMPLE_COUNT)) {
            sampleCountQuery = &GL_API::s_hardwareQueryPool->allocate(GL_SAMPLES_PASSED);
            sampleCountQuery->begin();
            runQueries = true;
        }
        if (isEnabledOption(drawCommand, CmdRenderOptions::QUERY_ANY_SAMPLE_RENDERED)) {
            anySamplesQuery = &GL_API::s_hardwareQueryPool->allocate(GL_ANY_SAMPLES_PASSED);
            anySamplesQuery->begin();
            runQueries = true;
        }

        //----- DRAW ------
        const GLenum primitiveType = glPrimitiveTypeTable[to_base(drawCommand._primitiveType)];
        SubmitRenderCommand(primitiveType, drawCommand, drawIndexed, useIndirectBuffer, internalFormat, countData, indexData);
        //-----------------

        if (runQueries) {
            auto& results = GenericDrawCommandResults::g_queryResults[drawCommand._sourceBuffer._id];
            if (primitiveQuery != nullptr) {
               results._primitivesGenerated = to_U64(primitiveQuery->getResultNoWait());
            }
            if (sampleCountQuery != nullptr) {
                results._samplesPassed = to_U32(sampleCountQuery->getResultNoWait());
            }
            if (anySamplesQuery != nullptr) {
                results._anySamplesPassed = to_U32(anySamplesQuery->getResultNoWait());
            }
        }
    }

    if (isEnabledOption(drawCommand, CmdRenderOptions::RENDER_WIREFRAME)) {
        SubmitRenderCommand(GL_LINE_LOOP, drawCommand, drawIndexed, useIndirectBuffer, internalFormat, countData, indexData);
    }
}

void glTextureViewCache::init(const U32 poolSize)
{
      _usageMap.resize(poolSize, State::FREE);
      _handles.resize(poolSize, 0u);
      _lifeLeft.resize(poolSize, 0u);
      _tempBuffer.resize(poolSize, 0u);

      glGenTextures(static_cast<GLsizei>(poolSize), _handles.data());
}

void glTextureViewCache::onFrameEnd() {
    OPTICK_EVENT("Texture Pool: onFrameEnd");

    ScopedLock<SharedMutex> w_lock(_lock);
    GLuint count = 0;
    const U32 entryCount = to_U32(_tempBuffer.size());
    for (U32 i = 0; i < entryCount; ++i) {
        if (_usageMap[i] != State::CLEAN) {
            continue;
        }

        U32& lifeLeft = _lifeLeft[i];

        if (lifeLeft > 0) {
            lifeLeft -= 1u;
        }

        if (lifeLeft == 0) {
            _tempBuffer[count++] = _handles[i];
            GL_API::DequeueComputeMipMap(_handles[i]);
        }
    }

    if (count > 0) {
        glDeleteTextures(count, _tempBuffer.data());
        glGenTextures(count, _tempBuffer.data());

        U32 newIndex = 0;
        for (U32 i = 0; i < entryCount; ++i) {
            if (_lifeLeft[i] == 0 && _usageMap[i] == State::CLEAN) {
                _usageMap[i] = State::FREE;
                _handles[i] = _tempBuffer[newIndex++];
                erase_if(_cache, [i](const auto& idx) { return i == idx.second; });
            }
        }
        std::memset(_tempBuffer.data(), 0, sizeof(GLuint) * count);
    }
}

void glTextureViewCache::destroy() {
    ScopedLock<SharedMutex> w_lock(_lock);
    const U32 entryCount = to_U32(_tempBuffer.size());
    glDeleteTextures(static_cast<GLsizei>(entryCount), _handles.data());
    std::memset(_handles.data(), 0, sizeof(GLuint) * entryCount);
    std::memset(_lifeLeft.data(), 0, sizeof(U32) * entryCount);
    std::fill(begin(_usageMap), end(_usageMap), State::CLEAN);
    _cache.clear();
}

GLuint glTextureViewCache::allocate(const bool retry) {
    return allocate(0u, retry).first;
}

std::pair<GLuint, bool> glTextureViewCache::allocate(const size_t hash, const bool retry) {
    {
        ScopedLock<SharedMutex> w_lock(_lock);

        if (hash != 0u) {
            U32 idx = INVALID_IDX;
            const auto& cacheIt = _cache.find(hash);
            if (cacheIt != cend(_cache)) {
                idx = cacheIt->second;
            }


            if (idx != INVALID_IDX) {
                assert(_usageMap[idx] != State::FREE);
                _usageMap[idx] = State::USED;
                _lifeLeft[idx] += 1;
                return std::make_pair(_handles[idx], true);
            }
        }

        const U32 count = to_U32(_handles.size());
        for (U32 i = 0; i < count; ++i) {
            if (_usageMap[i] == State::FREE) {
                _usageMap[i] = State::USED;
                if (hash != 0) {
                    _cache[hash] = i;
                }

                return std::make_pair(_handles[i], false);
            }
        }
    }

    if (!retry) {
        onFrameEnd();
        return allocate(hash, true);
    }

    DIVIDE_UNEXPECTED_CALL();
    return std::make_pair(0u, false);
}

void glTextureViewCache::deallocate(GLuint& handle, const U32 frameDelay) {

    ScopedLock<SharedMutex> w_lock(_lock);
    const U32 count = to_U32(_handles.size());
    for (U32 i = 0; i < count; ++i) {
        if (_handles[i] == handle) {
            handle = 0;
            _lifeLeft[i] = frameDelay;
            _usageMap[i] = State::CLEAN;
            return;
        }
    }
    
    DIVIDE_UNEXPECTED_CALL();
}

}  // namespace GLUtil

}  //namespace Divide
