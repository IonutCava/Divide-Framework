#include "stdafx.h"

#include "Headers/glResources.h"
#include "Headers/glHardwareQuery.h"

#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/GLIM/glim.h"

#include "Core/Headers/StringHelper.h"

namespace Divide {

void VAOBindings::init(const U32 maxBindings) noexcept {
    _maxBindings = maxBindings;
}

VAOBindings::VAOData* VAOBindings::getVAOData(const GLuint vao) {
    if (vao == _cachedVao) {
        return _cachedData;
    }

    _cachedVao = vao;
    _cachedData = &_bindings[_cachedVao];
    return _cachedData;
}

bool VAOBindings::instanceDivisorFlag(const GLuint vao, const GLuint index) {
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

void VAOBindings::instanceDivisorFlag(const GLuint vao, const GLuint index, const bool divisorFlag) {
    VAOData* data = getVAOData(vao);

    [[maybe_unused]] const size_t count = data->second.size();
    assert(count > 0 && count > index);

    data->second[index] = divisorFlag;
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

    [[maybe_unused]] const size_t count = data->first.size();
    assert(count > 0 && count > index);

    data->first[index] = newParams;
}

namespace GLUtil {

/*-----------Object Management----*/
GLuint k_invalidObjectID = GL_INVALID_INDEX;
GLuint s_lastQueryResult = GL_INVALID_INDEX;
size_t k_invalidSyncID = SIZE_MAX;

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
std::array<GLenum, to_base(PrimitiveTopology::COUNT)> glPrimitiveTypeTable;
std::array<GLenum, to_base(GFXDataFormat::COUNT)> glDataFormat;
std::array<GLenum, to_base(TextureWrap::COUNT)> glWrapTable;
std::array<GLenum, to_base(ShaderType::COUNT)> glShaderStageTable;

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

    glCullModeTable[to_base(CullMode::BACK)] = GL_BACK;
    glCullModeTable[to_base(CullMode::FRONT)] = GL_FRONT;
    glCullModeTable[to_base(CullMode::ALL)] = GL_FRONT_AND_BACK;
    glCullModeTable[to_base(CullMode::NONE)] = GL_NONE;

    glFillModeTable[to_base(FillMode::POINT)] = GL_POINT;
    glFillModeTable[to_base(FillMode::WIREFRAME)] = GL_LINE;
    glFillModeTable[to_base(FillMode::SOLID)] = GL_FILL;

    glTextureTypeTable[to_base(TextureType::TEXTURE_1D)] = GL_TEXTURE_1D;
    glTextureTypeTable[to_base(TextureType::TEXTURE_2D)] = GL_TEXTURE_2D;
    glTextureTypeTable[to_base(TextureType::TEXTURE_3D)] = GL_TEXTURE_3D;
    glTextureTypeTable[to_base(TextureType::TEXTURE_CUBE_MAP)] = GL_TEXTURE_CUBE_MAP;
    glTextureTypeTable[to_base(TextureType::TEXTURE_1D_ARRAY)] = GL_TEXTURE_1D_ARRAY;
    glTextureTypeTable[to_base(TextureType::TEXTURE_2D_ARRAY)] = GL_TEXTURE_2D_ARRAY;
    glTextureTypeTable[to_base(TextureType::TEXTURE_CUBE_ARRAY)] = GL_TEXTURE_CUBE_MAP_ARRAY;

    glImageFormatTable[to_base(GFXImageFormat::RED)] = GL_RED;
    glImageFormatTable[to_base(GFXImageFormat::RG)] = GL_RG;
    glImageFormatTable[to_base(GFXImageFormat::RGB)] = GL_RGB;
    glImageFormatTable[to_base(GFXImageFormat::BGR)] = GL_BGR;
    glImageFormatTable[to_base(GFXImageFormat::BGRA)] = GL_BGRA;
    glImageFormatTable[to_base(GFXImageFormat::RGBA)] = GL_RGBA;
    glImageFormatTable[to_base(GFXImageFormat::DEPTH_COMPONENT)] = GL_DEPTH_COMPONENT;
    glImageFormatTable[to_base(GFXImageFormat::DEPTH_STENCIL_COMPONENT)] = GL_DEPTH_COMPONENT;

    glImageFormatTable[to_base(GFXImageFormat::BC3n)] = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    glImageFormatTable[to_base(GFXImageFormat::BC4s)] = GL_COMPRESSED_SIGNED_RED_RGTC1_EXT;
    glImageFormatTable[to_base(GFXImageFormat::BC4u)] = GL_COMPRESSED_RED_RGTC1_EXT;
    glImageFormatTable[to_base(GFXImageFormat::BC5s)] = GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT;
    glImageFormatTable[to_base(GFXImageFormat::BC5u)] = GL_COMPRESSED_RED_GREEN_RGTC2_EXT;
    glImageFormatTable[to_base(GFXImageFormat::BC6s)] = GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;
    glImageFormatTable[to_base(GFXImageFormat::BC6u)] = GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
    glImageFormatTable[to_base(GFXImageFormat::BC7)] = GL_COMPRESSED_RGBA_BPTC_UNORM;
    glImageFormatTable[to_base(GFXImageFormat::BC7_SRGB)] = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB;

    glImageFormatTable[to_base(GFXImageFormat::DXT1_RGB)] = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;   //BC1
    glImageFormatTable[to_base(GFXImageFormat::DXT1_RGBA)] = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; //BC1a
    glImageFormatTable[to_base(GFXImageFormat::DXT3_RGBA)] = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; //BC2
    glImageFormatTable[to_base(GFXImageFormat::DXT5_RGBA)] = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; //BC3

    glImageFormatTable[to_base(GFXImageFormat::DXT1_RGB_SRGB)] = GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
    glImageFormatTable[to_base(GFXImageFormat::DXT1_RGBA_SRGB)] = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
    glImageFormatTable[to_base(GFXImageFormat::DXT3_RGBA_SRGB)] = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
    glImageFormatTable[to_base(GFXImageFormat::DXT5_RGBA_SRGB)] = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;

    glPrimitiveTypeTable[to_base(PrimitiveTopology::POINTS)] = GL_POINTS;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::LINES)] = GL_LINES;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::LINE_STRIP)] = GL_LINE_STRIP;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLES)] = GL_TRIANGLES;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLE_STRIP)] = GL_TRIANGLE_STRIP;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLE_FAN)] = GL_TRIANGLE_FAN;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::LINES_ADJANCENCY)] = GL_LINES_ADJACENCY;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::LINE_STRIP_ADJACENCY)] = GL_LINE_STRIP_ADJACENCY;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLES_ADJACENCY)] = GL_TRIANGLES_ADJACENCY;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY)] = GL_TRIANGLE_STRIP_ADJACENCY;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::PATCH)] = GL_PATCHES;
    glPrimitiveTypeTable[to_base(PrimitiveTopology::COMPUTE)] = GL_NONE;

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
    glWrapTable[to_base(TextureWrap::CLAMP_TO_EDGE)] = GL_CLAMP_TO_EDGE;
    glWrapTable[to_base(TextureWrap::CLAMP_TO_BORDER)] = GL_CLAMP_TO_BORDER;
    glWrapTable[to_base(TextureWrap::MIRROR_CLAMP_TO_EDGE)] = GL_MIRROR_CLAMP_TO_EDGE;

    glShaderStageTable[to_base(ShaderType::VERTEX)] = GL_VERTEX_SHADER;
    glShaderStageTable[to_base(ShaderType::FRAGMENT)] = GL_FRAGMENT_SHADER;
    glShaderStageTable[to_base(ShaderType::GEOMETRY)] = GL_GEOMETRY_SHADER;
    glShaderStageTable[to_base(ShaderType::TESSELLATION_CTRL)] = GL_TESS_CONTROL_SHADER;
    glShaderStageTable[to_base(ShaderType::TESSELLATION_EVAL)] = GL_TESS_EVALUATION_SHADER;
    glShaderStageTable[to_base(ShaderType::COMPUTE)] = GL_COMPUTE_SHADER;
}

GLenum internalFormat(const GFXImageFormat baseFormat, const GFXDataFormat dataType, const bool srgb, const bool normalized)  noexcept {
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
                case GFXDataFormat::UNSIGNED_BYTE:
                case GFXDataFormat::SIGNED_SHORT:
                case GFXDataFormat::UNSIGNED_SHORT: return GL_DEPTH_COMPONENT16;
                case GFXDataFormat::SIGNED_INT:
                case GFXDataFormat::UNSIGNED_INT: return GL_DEPTH_COMPONENT24;
                case GFXDataFormat::FLOAT_16:
                case GFXDataFormat::FLOAT_32: return GL_DEPTH_COMPONENT32F;
                default: DIVIDE_UNEXPECTED_CALL();
            };
        }break;  
        case GFXImageFormat::DEPTH_STENCIL_COMPONENT:
        {
            switch (dataType) {
                case GFXDataFormat::SIGNED_BYTE:
                case GFXDataFormat::UNSIGNED_BYTE:
                case GFXDataFormat::SIGNED_SHORT:
                case GFXDataFormat::UNSIGNED_SHORT:
                case GFXDataFormat::SIGNED_INT:
                case GFXDataFormat::UNSIGNED_INT: return GL_DEPTH24_STENCIL8;
                case GFXDataFormat::FLOAT_16:
                case GFXDataFormat::FLOAT_32: return GL_DEPTH32F_STENCIL8;
                default: DIVIDE_UNEXPECTED_CALL();
            };
        }break;
        // compressed formats
        case GFXImageFormat::DXT1_RGB_SRGB:
        case GFXImageFormat::DXT1_RGBA_SRGB:
        case GFXImageFormat::DXT3_RGBA_SRGB:
        case GFXImageFormat::DXT5_RGBA_SRGB:
        case GFXImageFormat::BC1:
        case GFXImageFormat::BC1a:
        case GFXImageFormat::BC2:
        case GFXImageFormat::BC3:
        case GFXImageFormat::BC3n:
        case GFXImageFormat::BC4s:
        case GFXImageFormat::BC4u:
        case GFXImageFormat::BC5s:
        case GFXImageFormat::BC5u:
        case GFXImageFormat::BC6s:
        case GFXImageFormat::BC6u:
        case GFXImageFormat::BC7:
        case GFXImageFormat::BC7_SRGB:
        {
            return glImageFormatTable[to_base(baseFormat)];
        };
        default: DIVIDE_UNEXPECTED_CALL();
    }

    return GL_NONE;
}

GLenum internalTextureType(const TextureType type, const U8 msaaSamples) {
    if (msaaSamples > 0u) {
        switch (type) {
        case TextureType::TEXTURE_2D: return GL_TEXTURE_2D_MULTISAMPLE;
        case TextureType::TEXTURE_2D_ARRAY: return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
        }
    }

    return GLUtil::glTextureTypeTable[to_base(type)];
}

namespace {

void SubmitMultiIndirectCommand(const U32 drawCount,
                                const GLenum mode,
                                const GLenum internalFormat,
                                const size_t offset)
{
    if (internalFormat != GL_NONE) {
        glMultiDrawElementsIndirect(mode, internalFormat, (bufferPtr)offset, drawCount, sizeof(IndirectDrawCommand));
    } else {
        glMultiDrawArraysIndirect(mode, (bufferPtr)offset, drawCount, sizeof(IndirectDrawCommand));
    }
}

void SubmitSingleIndirectCommand(const IndirectDrawCommand& cmd,
                                 const GLenum mode,
                                 const GLenum internalFormat,
                                 const size_t offset)
{
    if (internalFormat != GL_NONE) {
        glDrawElementsIndirect(mode, internalFormat, (bufferPtr)offset);
    } else {
        // This needs a different command buffer and different IndirectDrawCommand (16byte instead of 20)
        if (cmd.primCount > 1u) {
            if (cmd.baseInstance > 0u) {
                glDrawArraysInstancedBaseInstance(mode, cmd.firstIndex, cmd.indexCount, cmd.primCount, cmd.baseInstance);
            } else {
                glDrawArraysInstanced(mode, cmd.firstIndex, cmd.indexCount, cmd.primCount);
            }
        } else {
            if (cmd.baseInstance > 0u) {
                glDrawArraysInstancedBaseInstance(mode, cmd.firstIndex, cmd.indexCount, 1u, cmd.baseInstance);
            } else {
                glDrawArrays(mode, cmd.firstIndex, cmd.indexCount);
            }
        }
    }
}

void SubmitSingleDirectCommand(const GLenum mode,
                               const GLenum internalFormat,
                               const U32 indexCount,
                               const U32 firstIndex,
                               const U32 baseVertex,
                               const U32 baseInstance,
                               const size_t offset)
{
    if (internalFormat != GL_NONE) {
        const size_t elementSize = internalFormat == GL_UNSIGNED_SHORT ? sizeof(GLushort) : sizeof(GLuint);
        if (baseInstance > 0u) {
            glDrawElementsInstancedBaseVertexBaseInstance(mode, indexCount, internalFormat, (bufferPtr)(offset * elementSize), 1, baseVertex, baseInstance);
        } else {
            if (baseVertex > 0u) {
                glDrawElementsBaseVertex(mode, indexCount, internalFormat, (bufferPtr)(offset * elementSize), baseVertex);
            } else {
                glDrawElements(mode, indexCount, internalFormat, (bufferPtr)(offset * elementSize));
            }
        }
    } else {
        if (baseInstance > 0u) {
            glDrawArraysInstancedBaseInstance(mode, firstIndex, indexCount, 1u, baseInstance);
        } else {
            glDrawArrays(mode, firstIndex, indexCount);
        }
    }
}

void SubmitMultiDirectCommand(const U32 drawCount,
                              const GLenum mode,
                              const GLenum internalFormat,
                              const GLsizei* const count,
                              const bufferPtr indexData)
{
    if (internalFormat != GL_NONE) {
        glMultiDrawElements(mode, count, internalFormat, static_cast<void* const*>(indexData), drawCount);
    } else {
        glMultiDrawArrays(mode, static_cast<GLint*>(indexData), count, drawCount);
    }
}

void SubmitSingleDirectCommandInstanced(const GLenum mode, 
                                        const GLenum internalFormat,
                                        const U32 indexCount,
                                        const U32 firstIndex,
                                        const U32 primCount,
                                        const U32 baseVertex,
                                        const U32 baseInstance)
{
    if (internalFormat != GL_NONE) {
        const size_t elementSize = internalFormat == GL_UNSIGNED_SHORT ? sizeof(GLushort) : sizeof(GLuint);
        if (baseInstance > 0u) {
            glDrawElementsInstancedBaseVertexBaseInstance(mode, indexCount, internalFormat, (bufferPtr)(firstIndex * elementSize), primCount, baseVertex, baseInstance);
        } else {
            if (baseVertex > 0u) {
                glDrawElementsInstancedBaseVertex(mode, indexCount, internalFormat, (bufferPtr)(firstIndex * elementSize), primCount, baseVertex);
            } else {
                glDrawElementsInstanced(mode, indexCount, internalFormat, (bufferPtr)(firstIndex * elementSize), primCount);
            }
        }
    } else {
        if (baseInstance > 0u) {
            glDrawArraysInstancedBaseInstance(mode, firstIndex, indexCount, primCount, baseInstance);
        } else {
            glDrawArraysInstanced(mode, firstIndex, indexCount, primCount);
        }
    }
}

void SubmitIndirectCommand(const IndirectDrawCommand& cmd,
                           const U32 drawCount,
                           const GLenum mode,
                           const GLenum internalFormat,
                           const GLuint cmdBufferOffset)
{
    const size_t offset = (cmdBufferOffset * sizeof(IndirectDrawCommand)) + GL_API::GetStateTracker()._commandBufferOffset;

    if (drawCount > 1u) {
        SubmitMultiIndirectCommand(drawCount, mode, internalFormat, offset);
    } else {
        assert(drawCount == 1u);
        SubmitSingleIndirectCommand(cmd, mode, internalFormat, offset);
    }
}

void SubmitDirectCommand(const IndirectDrawCommand& cmd,
                         const U32 drawCount,
                         const GLenum mode,
                         const GLenum internalFormat,
                         const GLsizei* const count,
                         const bufferPtr indexData) 
{
    if (cmd.primCount > 1u) {
        if (drawCount > 1u) {
            DIVIDE_UNEXPECTED_CALL_MSG("Multi-draw is incompatible with instancing as gl_DrawID will have the wrong value. Split the call into multiple draw commands with manual uniform-updates in-between!");
        }

        SubmitSingleDirectCommandInstanced(mode, internalFormat, cmd.indexCount, cmd.firstIndex, cmd.primCount, cmd.baseVertex, cmd.baseInstance);
    } else {
        if (drawCount > 1u) {
            SubmitMultiDirectCommand(drawCount, mode, internalFormat, count, indexData);
        } else {
            assert(drawCount == 1u);
            SubmitSingleDirectCommand(mode, internalFormat, cmd.indexCount, cmd.firstIndex, cmd.baseVertex, cmd.baseInstance, cmd.firstIndex);
        }
    }
}

void SubmitRenderCommand(const GLenum primitiveType,
                         const GenericDrawCommand& drawCommand,
                         const bool useIndirectBuffer,
                         const GLenum internalFormat,
                         const GLsizei* const count,
                         const bufferPtr indexData)
{
    if (useIndirectBuffer) {
        SubmitIndirectCommand(drawCommand._cmd, drawCommand._drawCount, primitiveType, internalFormat, drawCommand._commandOffset);
    } else {
        SubmitDirectCommand(drawCommand._cmd, drawCommand._drawCount, primitiveType, internalFormat, count, indexData);
    }
}

} //namespace

void SubmitRenderCommand(const GenericDrawCommand& drawCommand,
                         const bool useIndirectBuffer,
                         const GLenum internalFormat,
                         const GLsizei* const count,
                         const bufferPtr indexData)
{
    auto& stateTracker = GL_API::GetStateTracker();

    stateTracker.toggleRasterization(!isEnabledOption(drawCommand, CmdRenderOptions::RENDER_NO_RASTERIZE));

    if (isEnabledOption(drawCommand, CmdRenderOptions::RENDER_GEOMETRY)) {
        SubmitRenderCommand(glPrimitiveTypeTable[to_base(stateTracker._activeTopology)], drawCommand, useIndirectBuffer, internalFormat, count, indexData);
    }
    if (isEnabledOption(drawCommand, CmdRenderOptions::RENDER_WIREFRAME)) {
        SubmitRenderCommand(GL_LINE_LOOP, drawCommand, useIndirectBuffer, internalFormat, count, indexData);
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
    PROFILE_SCOPE("Texture Pool: onFrameEnd");

    ScopedLock<SharedMutex> w_lock(_lock);
    GLuint count = 0u;
    const U32 entryCount = to_U32(_tempBuffer.size());
    for (U32 i = 0u; i < entryCount; ++i) {
        if (_usageMap[i] != State::CLEAN) {
            continue;
        }

        U32& lifeLeft = _lifeLeft[i];

        if (lifeLeft > 0u) {
            lifeLeft -= 1u;
        }

        if (lifeLeft == 0u) {
            _tempBuffer[count++] = _handles[i];
        }
    }

    if (count > 0u) {
        glDeleteTextures(count, _tempBuffer.data());
        glGenTextures(count, _tempBuffer.data());

        U32 newIndex = 0u;
        for (U32 i = 0u; i < entryCount; ++i) {
            if (_lifeLeft[i] == 0u && _usageMap[i] == State::CLEAN) {
                _usageMap[i] = State::FREE;
                _handles[i] = _tempBuffer[newIndex++];
                erase_if(_cache, [i](const auto& idx) { return i == idx.second; });
            }
        }
        memset(_tempBuffer.data(), 0, sizeof(GLuint) * count);
    }
}

void glTextureViewCache::destroy() {
    ScopedLock<SharedMutex> w_lock(_lock);
    const U32 entryCount = to_U32(_tempBuffer.size());
    glDeleteTextures(static_cast<GLsizei>(entryCount), _handles.data());
    memset(_handles.data(), 0, sizeof(GLuint) * entryCount);
    memset(_lifeLeft.data(), 0, sizeof(U32) * entryCount);
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
            U32 idx = GLUtil::k_invalidObjectID;
            const auto& cacheIt = _cache.find(hash);
            if (cacheIt != cend(_cache)) {
                idx = cacheIt->second;
            }


            if (idx != GLUtil::k_invalidObjectID) {
                assert(_usageMap[idx] != State::FREE);
                _usageMap[idx] = State::USED;
                _lifeLeft[idx] += 1;
                return std::make_pair(_handles[idx], true);
            }
        }

        const U32 count = to_U32(_handles.size());
        for (U32 i = 0u; i < count; ++i) {
            if (_usageMap[i] == State::FREE) {
                _usageMap[i] = State::USED;
                _lifeLeft[i] = 1u;
                if (hash != 0u) {
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

void glTextureViewCache::deallocate(const GLuint handle, const U32 frameDelay) {

    ScopedLock<SharedMutex> w_lock(_lock);
    const U32 count = to_U32(_handles.size());
    for (U32 i = 0u; i < count; ++i) {
        if (_handles[i] == handle) {
            _lifeLeft[i] = frameDelay;
            _usageMap[i] = State::CLEAN;
            return;
        }
    }
    
    DIVIDE_UNEXPECTED_CALL();
}

/// Print OpenGL specific messages
void DebugCallback(const GLenum source,
                   const GLenum type,
                   const GLuint id,
                   const GLenum severity,
                   [[maybe_unused]] const GLsizei length,
                   const GLchar* message,
                   const void* userParam) {

    if (type == GL_DEBUG_TYPE_OTHER && severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        // Really don't care about these
        return;
    }

    // Translate message source
    const char* gl_source = "Unknown Source";
    if (source == GL_DEBUG_SOURCE_API) {
        gl_source = "OpenGL";
    } else if (source == GL_DEBUG_SOURCE_WINDOW_SYSTEM) {
        gl_source = "Windows";
    } else if (source == GL_DEBUG_SOURCE_SHADER_COMPILER) {
        gl_source = "Shader Compiler";
    } else if (source == GL_DEBUG_SOURCE_THIRD_PARTY) {
        gl_source = "Third Party";
    } else if (source == GL_DEBUG_SOURCE_APPLICATION) {
        gl_source = "Application";
    } else if (source == GL_DEBUG_SOURCE_OTHER) {
        gl_source = "Other";
    }
    // Translate message type
    const char* gl_type = "Unknown Type";
    if (type == GL_DEBUG_TYPE_ERROR) {
        gl_type = "Error";
    } else if (type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR) {
        gl_type = "Deprecated behavior";
    } else if (type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR) {
        gl_type = "Undefined behavior";
    } else if (type == GL_DEBUG_TYPE_PORTABILITY) {
        gl_type = "Portability";
    } else if (type == GL_DEBUG_TYPE_PERFORMANCE) {
        gl_type = "Performance";
    } else if (type == GL_DEBUG_TYPE_OTHER) {
        gl_type = "Other";
    } else if (type == GL_DEBUG_TYPE_MARKER) {
        gl_type = "Marker";
    } else if (type == GL_DEBUG_TYPE_PUSH_GROUP) {
        gl_type = "Push";
    } else if (type == GL_DEBUG_TYPE_POP_GROUP) {
        gl_type = "Pop";
    }

    // Translate message severity
    const char* gl_severity = "Unknown Severity";
    if (severity == GL_DEBUG_SEVERITY_HIGH) {
        gl_severity = "High";
    } else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
        gl_severity = "Medium";
    } else if (severity == GL_DEBUG_SEVERITY_LOW) {
        gl_severity = "Low";
    } else if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        gl_severity = "Info";
    }

    std::string fullScope = "GL";
    for (U8 i = 0u; i < GL_API::GetStateTracker()._debugScopeDepth; ++i) {
        fullScope.append("::");
        fullScope.append(GL_API::GetStateTracker()._debugScope[i].first);
    }
    // Print the message and the details
    const GLuint activeProgram = GL_API::GetStateTracker()._activeShaderProgramHandle;
    const GLuint activePipeline = GL_API::GetStateTracker()._activeShaderPipelineHandle;

    const char* programMsg = "[%s Thread][Source: %s][Type: %s][ID: %d][Severity: %s][Bound Program : %d][DebugGroup: %s][Message: %s]";
    const char* pipelineMsg = "[%s Thread][Source: %s][Type: %s][ID: %d][Severity: %s][Bound Pipeline : %d][DebugGroup: %s][Message: %s]";

    const string outputError = Util::StringFormat(
        activeProgram != 0u ? programMsg : pipelineMsg,
        userParam == nullptr ? "Main" : "Worker",
        gl_source,
        gl_type,
        id, 
        gl_severity, 
        activeProgram != 0u ? activeProgram : activePipeline,
        fullScope.c_str(),
        message);

    const bool isConsoleIM = Console::immediateModeEnabled();
    Console::toggleImmediateMode(true);
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        Console::printfn(outputError.c_str());
    } else if (severity == GL_DEBUG_SEVERITY_LOW) {
        Console::warnfn(outputError.c_str());
    } else {
        Console::errorfn(outputError.c_str());
    }
    Console::toggleImmediateMode(isConsoleIM);

}

}  // namespace GLUtil

}  //namespace Divide
