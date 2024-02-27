/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _GFX_ENUMS_H
#define _GFX_ENUMS_H

namespace Divide {

using RenderTargetID = U32;
constexpr RenderTargetID INVALID_RENDER_TARGET_ID = std::numeric_limits<RenderTargetID>::max();
constexpr RenderTargetID SCREEN_TARGET_ID = std::numeric_limits<RenderTargetID>::max() - 1u;
constexpr U8 INVALID_TEXTURE_BINDING = U8_MAX;

// 4 should be more than enough even for batching multiple render targets together
enum class RTColourAttachmentSlot : U8
{
    SLOT_0 = 0,
    SLOT_1,
    SLOT_2,
    SLOT_3,
    COUNT
};

enum class RenderAPI : U8 {
    None,      ///< No rendering. Used for testing or server code
    OpenGL,    ///< 4.x+
    Vulkan,    ///< not supported yet
    COUNT
};

namespace Names {
    static const char* renderAPI[] = {
        "None", "OpenGL", "Vulkan", "ERROR"
    };
};

static_assert(std::size(Names::renderAPI) == to_base(RenderAPI::COUNT) + 1);

enum class DescriptorSetUsage : U8 {
    PER_DRAW = 0,
    PER_BATCH,
    PER_PASS,
    PER_FRAME,
    COUNT
};

namespace Names {
    static constexpr const char* descriptorSetUsage[] = {
         "PER_DRAW", "PER_BATCH", "PER_PASS", "PER_FRAME", "NONE"
    };
};

static_assert(std::size(Names::descriptorSetUsage) == to_base(DescriptorSetUsage::COUNT) + 1);

static_assert(to_base(DescriptorSetUsage::PER_DRAW) == 0u, "PER_DRAW must be set to 0 to provide compatibility with the OpenGL renderer!");

enum class ReflectorType : U8
{
    PLANAR = 0,
    CUBE,
    COUNT
};

namespace Names {
    static constexpr const char* reflectorType[] = {
        "PLANAR", "CUBE", "NONE"
    };
};

static_assert(std::size(Names::reflectorType) == to_base(ReflectorType::COUNT) + 1);

enum class RefractorType : U8
{
    PLANAR = 0,
    COUNT
};

namespace Names {
    static constexpr const char* refractorType[] = {
        "PLANAR", "NONE"
    };
};

static_assert(std::size(Names::refractorType) == to_base(RefractorType::COUNT) + 1);


enum class ImageUsage : U8
{
    UNDEFINED = 0,
    SHADER_READ, // Image read or sampled read
    SHADER_WRITE,
    SHADER_READ_WRITE, //General usage
    CPU_READ,
    RT_COLOUR_ATTACHMENT,
    RT_DEPTH_ATTACHMENT,
    RT_DEPTH_STENCIL_ATTACHMENT,
    COUNT
};

namespace Names {
    static constexpr const char* imageUsage[] = {
        "UNDEFINED", "SHADER_READ", "SHADER_WRITE", "SHADER_READ_WRITE", "CPU_READ", "RT_COLOUR_ATTACHMENT", "RT_DEPTH_ATTACHMENT", "RT_DEPTH_STENCIL_ATTACHMENT", "UNKNOWN"
    };
};

static_assert(std::size(Names::imageUsage) == to_base(ImageUsage::COUNT) + 1);

/// The different types of lights supported
enum class LightType : U8
{
    DIRECTIONAL = 0,
    POINT = 1,
    SPOT = 2,
    COUNT
};
namespace Names {
    static constexpr const char* lightType[] = {
          "DIRECTIONAL", "POINT", "SPOT", "UNKNOWN"
    };
};

static_assert(std::size(Names::lightType) == to_base(LightType::COUNT) + 1);

enum class FrustumCollision : U8
{
    FRUSTUM_OUT = 0,
    FRUSTUM_IN,
    FRUSTUM_INTERSECT,
    COUNT
};

namespace Names {
    static constexpr const char* frustumCollision[] = {
        "FRUSTUM_OUT", "FRUSTUM_IN", "FRUSTUM_INTERSECT", "NONE"
    };
};

static_assert(std::size(Names::frustumCollision) == to_base(FrustumCollision::COUNT) + 1);

enum class FrustumPlane : U8
{
    PLANE_LEFT = 0,
    PLANE_RIGHT,
    PLANE_TOP,
    PLANE_BOTTOM,
    PLANE_NEAR,
    PLANE_FAR,
    COUNT
};

namespace Names {
    static constexpr const char* frustumPlane[] = {
        "PLANE_LEFT", "PLANE_RIGHT", "PLANE_TOP", "PLANE_BOTTOM", "PLANE_NEAR", "PLANE_FAR", "NONE"
    };
};

static_assert(std::size(Names::frustumPlane) == to_base(FrustumPlane::COUNT) + 1);

enum class FrustumPoints : U8
{
    NEAR_LEFT_TOP = 0,
    NEAR_RIGHT_TOP,
    NEAR_RIGHT_BOTTOM,
    NEAR_LEFT_BOTTOM,
    FAR_LEFT_TOP,
    FAR_RIGHT_TOP,
    FAR_RIGHT_BOTTOM,
    FAR_LEFT_BOTTOM,
    COUNT
};

namespace Names {
    static constexpr const char* frustumPoints[] = {
        "NEAR_LEFT_TOP", "NEAR_RIGHT_TOP", "NEAR_RIGHT_BOTTOM", "NEAR_LEFT_BOTTOM",
        "FAR_LEFT_TOP", "FAR_RIGHT_TOP", "FAR_RIGHT_BOTTOM", "FAR_LEFT_BOTTOM",
        "NONE"
    };
};

static_assert(std::size(Names::frustumPoints) == to_base(FrustumPoints::COUNT) + 1);

/// State the various attribute locations to use in shaders with VAO/VB's
enum class AttribLocation : U8 {
    POSITION = 0,
    TEXCOORD = 1,
    NORMAL = 2,
    TANGENT = 3,
    COLOR = 4,
    BONE_WEIGHT = 5,
    BONE_INDICE = 6,
    WIDTH = 7,
    GENERIC = 8,
    COUNT
};

namespace Names {
    static constexpr const char* attribLocation[] = {
        "POSITION", "TEXCOORD", "NORMAL", "TANGENT",
        "COLOR", "BONE_WEIGHT", "BONE_INDICE", "WIDTH",
        "GENERIC", "NONE"
    };
};

static_assert(std::size(Names::attribLocation) == to_base(AttribLocation::COUNT) + 1);

enum class RenderStage : U8 {
    DISPLAY = 0,
    REFLECTION,
    REFRACTION,
    NODE_PREVIEW,
    SHADOW,
    COUNT
};

namespace Names {
    static constexpr const char* renderStage[] = {
        "DISPLAY", "REFLECTION", "REFRACTION", "NODE_PREVIEW", "SHADOW", "NONE"
    };
};

static_assert(std::size(Names::renderStage) == to_base(RenderStage::COUNT) + 1);

enum class RenderPassType : U8 {
    PRE_PASS = 0,
    MAIN_PASS = 1,
    OIT_PASS = 2,
    TRANSPARENCY_PASS = 3,
    COUNT
};

namespace Names {
    static constexpr const char* renderPassType[] = {
        "PRE_PASS", "MAIN_PASS", "OIT_PASS", "TRANSPARENCY_PASS", "NONE"
    };
};

static_assert(std::size(Names::renderPassType) == to_base(RenderPassType::COUNT) + 1);

enum class PBType : U8 { 
    PB_TEXTURE_1D, 
    PB_TEXTURE_2D, 
    PB_TEXTURE_3D,
    COUNT
};

namespace Names {
    static constexpr const char* pbType[] = {
        "PB_TEXTURE_1D", "PB_TEXTURE_2D", "PB_TEXTURE_3D", "NONE"
    };
};

static_assert(std::size(Names::pbType) == to_base(PBType::COUNT) + 1);

enum class PrimitiveTopology : U8 {
    POINTS = 0,
    LINES,
    //LINE_LOOP, No Vulkan support
    LINE_STRIP,
    TRIANGLES,
    TRIANGLE_STRIP,
    TRIANGLE_FAN,
    //QUADS, //Deprecated and No Vulkan support
    //QUAD_STRIP, //No Vulkan support
    //POLYGON,//No Vulkan support
    LINES_ADJANCENCY,
    LINE_STRIP_ADJACENCY,
    TRIANGLES_ADJACENCY,
    TRIANGLE_STRIP_ADJACENCY,
    PATCH,
    COMPUTE,
    COUNT
};

namespace Names {
    static constexpr const char* primitiveType[] = {
        "POINTS", "LINES", "LINE_STRIP", "TRIANGLES", "TRIANGLE_STRIP",
        "TRIANGLE_FAN", "LINES_ADJANCENCY", "LINE_STRIP_ADJACENCY",
        "TRIANGLES_ADJACENCY", "TRIANGLE_STRIP_ADJACENCY", "PATCH", "COMPUTE", "NONE"
    };
};

static_assert(std::size(Names::primitiveType) == to_base(PrimitiveTopology::COUNT) + 1);

/// Specifies how the red, green, blue, and alpha source blending factors are computed.
enum class BlendProperty : U8 {
    ZERO = 0,
    ONE,
    SRC_COLOR,
    INV_SRC_COLOR,
    /// Transparency is best implemented using blend function (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
    /// with primitives sorted from farthest to nearest.
    SRC_ALPHA,
    INV_SRC_ALPHA,
    DEST_ALPHA,
    INV_DEST_ALPHA,
    DEST_COLOR,
    INV_DEST_COLOR,
    /// Polygon antialiasing is optimized using blend function
    /// (SRC_ALPHA_SATURATE, GL_ONE)
    /// with polygons sorted from nearest to farthest.
    SRC_ALPHA_SAT,
    /// Place all properties above this.
    COUNT
};
namespace Names {
    static constexpr const char* blendProperty[] = {
       "ZERO", "ONE", "SRC_COLOR", "INV_SRC_COLOR", "SRC_ALPHA", "INV_SRC_ALPHA", "DEST_ALPHA", "INV_DEST_ALPHA",
       "DEST_COLOR", "INV_DEST_COLOR", "SRC_ALPHA_SAT", "NONE"
    };
};

static_assert(std::size(Names::blendProperty) == to_base(BlendProperty::COUNT) + 1);

/// Specifies how source and destination colours are combined.
enum class BlendOperation : U8 {
    /// The ADD equation is useful for antialiasing and transparency, among
    /// other things.
    ADD = 0,
    SUBTRACT,
    REV_SUBTRACT,
    /// The MIN and MAX equations are useful for applications that analyze image
    /// data
    /// (image thresholding against a constant colour, for example).
    MIN,
    /// The MIN and MAX equations are useful for applications that analyze image
    /// data
    /// (image thresholding against a constant colour, for example).
    MAX,
    /// Place all properties above this.
    COUNT
};

namespace Names {
    static constexpr const char* blendOperation[] = {
        "ADD", "SUBTRACT", "REV_SUBTRACT", "MIN", "MAX", "NONE"
    };
};

static_assert(std::size(Names::blendOperation) == to_base(BlendOperation::COUNT) + 1);

/// Valid comparison functions for most states
/// YYY = test value using this function
enum class ComparisonFunction : U8 {
    /// Never passes.
    NEVER = 0,
    /// Passes if the incoming YYY value is less than the stored YYY value.
    LESS,
    /// Passes if the incoming YYY value is equal to the stored YYY value.
    EQUAL,
    /// Passes if the incoming YYY value is less than or equal to the stored YYY
    /// value.
    LEQUAL,
    /// Passes if the incoming YYY value is greater than the stored YYY value.
    GREATER,
    /// Passes if the incoming YYY value is not equal to the stored YYY value.
    NEQUAL,
    /// Passes if the incoming YYY value is greater than or equal to the stored
    /// YYY value.
    GEQUAL,
    /// Always passes.
    ALWAYS,
    /// Place all properties above this.
    COUNT
};

namespace Names {
    static constexpr const char* compFunctionNames[] = {
        "NEVER", "LESS", "EQUAL", "LEQUAL", "GREATER", "NEQUAL", "GEQUAL", "ALWAYS", "ERROR"
    };
};

static_assert(std::size(Names::compFunctionNames) == to_base(ComparisonFunction::COUNT) + 1);

/// Specifies whether front- or back-facing facets are candidates for culling.
enum class CullMode : U8 {
    NONE = 0,
    /// Cull Back facing polygons (aka CW)
    BACK,
    /// Cull Front facing polygons (aka CCW)
    FRONT,
    /// Cull All polygons
    ALL,
    /// Place all properties above this.
    COUNT
};

namespace Names {
    static constexpr const char* cullModes[] = {
        "None", "BACK", "FRONT", "ALL", "ERROR!"
    };
};

static_assert(std::size(Names::cullModes) == to_base(CullMode::COUNT) + 1);

/// Available shader stages
enum class ShaderType : U8 {
    FRAGMENT = 0,
    VERTEX = 1,
    GEOMETRY = 2,
    TESSELLATION_CTRL = 3,
    TESSELLATION_EVAL = 4,
    COMPUTE = 5,
    COUNT
};

namespace Names {
    static constexpr const char* shaderTypes[] = {
        "Fragment", "Vertex", "Geometry", "TessellationC", "TessellationE", "Compute", "ERROR!"
    };
};

static_assert(std::size(Names::shaderTypes) == to_base(ShaderType::COUNT) + 1);

enum class ShaderStageVisibility : U16 {
    NONE = 0,
    VERTEX = toBit(1),
    GEOMETRY = toBit(2),
    TESS_CONTROL = toBit(3),
    TESS_EVAL = toBit(4),
    FRAGMENT = toBit(5),
    COMPUTE = toBit(6),
    /*MESH = toBit(7),
    TASK = toBit(8),*/
    COUNT = 7,
    ALL_GEOMETRY = /*MESH | TASK |*/ VERTEX | GEOMETRY | TESS_CONTROL | TESS_EVAL,
    ALL_DRAW = ALL_GEOMETRY | FRAGMENT,
    COMPUTE_AND_DRAW = FRAGMENT | COMPUTE,
    COMPUTE_AND_GEOMETRY = ALL_GEOMETRY | COMPUTE,
    ALL = ALL_DRAW | COMPUTE,
};

/// Valid front and back stencil test actions
enum class StencilOperation : U8 {
    /// Keeps the current value.
    KEEP = 0,
    /// Sets the stencil buffer value to 0.
    ZERO,
    /// Sets the stencil buffer value to ref, as specified by StencilFunc.
    REPLACE,
    /// Increments the current stencil buffer value. Clamps to the maximum
    /// representable unsigned value.
    INCR,
    ///  Decrements the current stencil buffer value. Clamps to 0.
    DECR,
    /// Bitwise inverts the current stencil buffer value.
    INV,
    /// Increments the current stencil buffer value.
    /// Wraps stencil buffer value to zero when incrementing the maximum
    /// representable unsigned value.
    INCR_WRAP,
    /// Decrements the current stencil buffer value.
    /// Wraps stencil buffer value to the maximum representable unsigned value
    /// when decrementing a stencil buffer value of zero.
    DECR_WRAP,
    /// Place all properties above this.
    COUNT
};

namespace Names {
    static constexpr const char* stencilOpNames[] = {
        "KEEP", "ZERO", "REPLACE", "INCREMENT", "DECREMENT", "INVERT", "INCREMENT_WRAP", "DECREMENT_WRAP", "ERROR"
    };
};

static_assert(std::size(Names::stencilOpNames) == to_base(StencilOperation::COUNT) + 1);

/// Defines all available fill modes for primitives
enum class FillMode : U8 {
    /// Polygon vertices that are marked as the start of a boundary edge are
    /// drawn as points.
    POINT = 0,
    /// Boundary edges of the polygon are drawn as line segments.
    WIREFRAME,
    /// The interior of the polygon is filled.
    SOLID,
    /// Place all properties above this.
    COUNT
};

namespace Names {
    static constexpr const char* fillMode[] = {
        "Point", "Wireframe", "Solid", "ERROR!"
    };
};

static_assert(std::size(Names::fillMode) == to_base(FillMode::COUNT) + 1);

enum class TextureType : U8 {
    TEXTURE_1D = 0,
    TEXTURE_2D,
    TEXTURE_3D,
    TEXTURE_CUBE_MAP,
    TEXTURE_1D_ARRAY,
    TEXTURE_2D_ARRAY,
    TEXTURE_CUBE_ARRAY,
    COUNT
};

namespace Names {
    static constexpr const char* textureType[] = {
        "TEXTURE_1D",
        "TEXTURE_2D",
        "TEXTURE_3D",
        "TEXTURE_CUBE_MAP",
        "TEXTURE_1D_ARRAY",
        "TEXTURE_2D_ARRAY",
        "TEXTURE_CUBE_ARRAY",
        "NONE"
    };
};

static_assert(std::size(Names::textureType) == to_base(TextureType::COUNT) + 1);

enum class TextureBorderColour : U8 {
    TRANSPARENT_BLACK_INT = 0,
    TRANSPARENT_BLACK_F32,
    OPAQUE_BLACK_INT,
    OPAQUE_BLACK_F32,
    OPAQUE_WHITE_INT,
    OPAQUE_WHITE_F32,
    CUSTOM_INT,
    CUSTOM_F32,
    COUNT
};

namespace Names {
    static constexpr const char* textureBorderColour[] = {
        "TRANSPARENT_BLACK_INT", "TRANSPARENT_BLACK_F32", "OPAQUE_BLACK_INT", "OPAQUE_BLACK_F32", "OPAQUE_WHITE_INT", "OPAQUE_WHITE_F32", "CUSTOM_INT", "CUSTOM_F32", "NONE"
    };
};

static_assert(std::size(Names::textureBorderColour) == to_base(TextureBorderColour::COUNT) + 1);

enum class TextureFilter : U8 {
    LINEAR = 0,
    NEAREST,
    COUNT
};

namespace Names {
    static constexpr const char* textureFilter[] = {
        "LINEAR", "NEAREST", "NONE"
    };
};

static_assert(std::size(Names::textureFilter) == to_base(TextureFilter::COUNT) + 1);

enum class TextureMipSampling : U8 {
    LINEAR = 0,
    NEAREST,
    NONE,
    COUNT
};


namespace Names {
    static constexpr const char* textureMipSampling[] = {
        "LINEAR", "NEAREST", "NONE", "ERROR"
    };
};

static_assert(std::size(Names::textureMipSampling) == to_base(TextureMipSampling::COUNT) + 1);

enum class TextureWrap : U8 {
    CLAMP_TO_EDGE = 0,
    CLAMP_TO_BORDER,
    REPEAT,
    MIRROR_REPEAT,
    MIRROR_CLAMP_TO_EDGE,
    COUNT
};

namespace Names {
    static constexpr const char* textureWrap[] = {
        "CLAMP_TO_EDGE", "CLAMP_TO_BORDER", "REPEAT", "MIRROR_REPEAT", "MIRROR_CLAMP_TO_EDGE", "NONE"
    };
};

static_assert(std::size(Names::textureWrap) == to_base(TextureWrap::COUNT) + 1);

enum class GFXImageFormat : U8 {
    RED = 0,
    RG,
    BGR,
    RGB,
    BGRA,
    RGBA,
    BC1,
    BC1a,
    BC2,
    BC3,
    BC3n,
    BC4s,
    BC4u,
    BC5s,
    BC5u,
    BC6s,
    BC6u,
    BC7,
    //BC3_RGBM,
    //ETC1,
    //ETC2_R,
    //ETC2_RG,
    //ETC2_RGB,
    //ETC2_RGBA,
    //ETC2_RGB_A1,
    //ETC2_RGBM,
    COUNT,
    DXT1_RGB = BC1,
    DXT1_RGBA = BC1a,
    DXT3_RGBA = BC2,
    DXT5_RGBA = BC3,
};
namespace Names {
    static constexpr const char* GFXImageFormat[] = {
        "RED", "RG", "BGR", "RGB", "BGRA", "RGBA", "BC1/DXT1_RGB", "BC1a/DXT1_RGBA", "BC2/DXT3_RGBA",
        "BC3/DXT5_RGBA", "BC3n", "BC4s", "BC4u", "BC5s", "BC5u", "BC6s", "BC6u", "BC7", "NONE",
    };
};

static_assert(std::size(Names::GFXImageFormat) == to_base(GFXImageFormat::COUNT) + 1);

enum class GFXDataFormat : U8 {
    UNSIGNED_BYTE = 0,
    UNSIGNED_SHORT,
    UNSIGNED_INT,
    SIGNED_BYTE,
    SIGNED_SHORT,
    SIGNED_INT,
    FLOAT_16,
    FLOAT_32,
    COUNT
};

namespace Names {
    static constexpr const char* GFXDataFormat[] = {
        "UNSIGNED_BYTE", "UNSIGNED_SHORT", "UNSIGNED_INT", "SIGNED_BYTE", "SIGNED_SHORT", "SIGNED_INT",
        "FLOAT_16", "FLOAT_32", "ERROR"
    };
};

static_assert(std::size(Names::GFXDataFormat) == to_base(GFXDataFormat::COUNT) + 1);

enum class GFXImagePacking : U8
{
    NORMALIZED,
    NORMALIZED_SRGB,
    UNNORMALIZED, //Don't blame me, found in Vulkan spec 16.1.1 -Ionut
    RGB_565,
    RGBA_4444,
    DEPTH,
    DEPTH_STENCIL,
    COUNT
};

namespace Names
{
    static constexpr const char* GFXImagePacking[] = {
        "NORMALIZED", "NORMALIZED_SRGB", "UNNORMALIZED", "RGB_565", "RGBA_4444", "DEPTH", "DEPTH_STENCIL", "ERROR"
    };
};

static_assert(std::size( Names::GFXImagePacking ) == to_base( GFXImagePacking::COUNT ) + 1);

enum class GPUVendor : U8 {
    NVIDIA = 0,
    AMD,
    INTEL,
    MICROSOFT,
    IMAGINATION_TECH,
    ARM,
    QUALCOMM,
    VIVANTE,
    ALPHAMOSAIC,
    WEBGL, //Khronos
    MESA,
    OTHER,
    COUNT
};

namespace Names {
    static constexpr const char* GPUVendor[] = {
        "NVIDIA", "AMD", "INTEL", "MICROSOFT", "IMAGINATION_TECH", "ARM",
        "QUALCOMM", "VIVANTE", "ALPHAMOSAIC", "WEBGL", "MESA", "OTHER", "ERROR"
    };
};

static_assert(std::size(Names::GPUVendor) == to_base(GPUVendor::COUNT) + 1);

enum class GPURenderer : U8 {
    UNKNOWN = 0,
    ADRENO,
    GEFORCE,
    INTEL,
    MALI,
    POWERVR,
    RADEON,
    VIDEOCORE,
    VIVANTE,
    WEBGL,
    GDI, //Driver not working properly?
    SOFTWARE,
    COUNT
};

namespace Names {
    static constexpr const char* GPURenderer[] = {
        "UNKNOWN", "ADRENO", "GEFORCE", "INTEL", "MALI", "POWERVR",
        "RADEON", "VIDEOCORE", "VIVANTE", "WEBGL", "GDI", "SOFTWARE", "ERROR"
    };
};

static_assert(std::size(Names::GPURenderer) == to_base(GPURenderer::COUNT) + 1);

enum class BufferUsageType : U8 {
    VERTEX_BUFFER = 0,
    INDEX_BUFFER,
    STAGING_BUFFER,
    CONSTANT_BUFFER,
    UNBOUND_BUFFER,
    COMMAND_BUFFER,
    COUNT
};

namespace Names {
    static constexpr const char* bufferUsageType[] = {
        "VERTEX_BUFFER", "INDEX_BUFFER", "STAGING_BUFFER", "CONSTANT_BUFFER", "UNBOUND_BUFFER", "COMMAND_BUFFER", "NONE"
    };
};

static_assert(std::size(Names::bufferUsageType) == to_base(BufferUsageType::COUNT) + 1);

enum class BufferSyncUsage : U8
{
    CPU_WRITE_TO_GPU_READ = 0,
    GPU_WRITE_TO_CPU_READ,
    GPU_WRITE_TO_GPU_READ,
    GPU_WRITE_TO_GPU_WRITE,
    GPU_READ_TO_GPU_WRITE,
    CPU_WRITE_TO_CPU_READ,
    CPU_READ_TO_CPU_WRITE,
    CPU_WRITE_TO_CPU_WRITE,
    COUNT
};

namespace Names
{
    static constexpr const char* bufferSyncUsage[] = {
        "CPU_WRITE_TO_GPU_READ", "GPU_WRITE_TO_CPU_READ", "GPU_WRITE_TO_GPU_READ", "GPU_WRITE_TO_GPU_WRITE", "GPU_READ_TO_GPU_WRITE", "CPU_WRITE_TO_CPU_READ", "CPU_READ_TO_CPU_WRITE", "CPU_WRITE_TO_CPU_WRITE", "NONE"
    };
};

static_assert(std::size( Names::bufferSyncUsage ) == to_base( BufferSyncUsage::COUNT ) + 1);

enum class BufferUpdateUsage : U8 {
    CPU_TO_GPU = 0, //DRAW
    GPU_TO_CPU = 1, //READ
    GPU_TO_GPU = 2, //COPY
    COUNT
};

namespace Names {
    static constexpr const char* bufferUpdateUsage[] = {
        "CPU_TO_GPU", "GPU_TO_CPU", "GPU_TO_GPU", "NONE"
    };
};

static_assert(std::size(Names::bufferUpdateUsage) == to_base(BufferUpdateUsage::COUNT) + 1);

enum class BufferUpdateFrequency : U8 {
    ONCE = 0,       //STATIC
    OCASSIONAL = 1, //DYNAMIC
    OFTEN = 2,      //STREAM
    COUNT
};

namespace Names {
    static constexpr const char* bufferUpdateFrequency[] = {
        "ONCE", "OCASSIONAL", "OFTEN", "NONE"
    };
};

static_assert(std::size(Names::bufferUpdateFrequency) == to_base(BufferUpdateFrequency::COUNT) + 1);


enum class QueryType : U8 {
    VERTICES_SUBMITTED = toBit(1),
    PRIMITIVES_GENERATED = toBit(2),
    TESSELLATION_PATCHES = toBit(3),
    TESSELLATION_EVAL_INVOCATIONS = toBit(4),
    GPU_TIME = toBit(5),
    SAMPLE_COUNT = toBit(6),
    ANY_SAMPLE_RENDERED = toBit(7),
    COUNT = 7
};

namespace Names {
    static constexpr const char* queryType[] = {
        "VERTICES_SUBMITTED", "PRIMITIVES_GENERATED", "TESSELLATION_PATCHES", "TESSELLATION_EVAL_INVOCATIONS", "GPU_TIME", "SAMPLE_COUNT", "ANY_SAMPLE_RENDERED", "NONE"
    };
};

static_assert(std::size(Names::queryType) == to_base(QueryType::COUNT) + 1);

enum class PushConstantSize : U8
{
    BYTE = 0,
    WORD,
    DWORD,
    QWORD,
    COUNT
};

//ToDo: Make this more generic. Also used by the Editor -Ionut
enum class PushConstantType : U8
{
    BOOL = 0,
    INT,
    UINT,
    FLOAT,
    DOUBLE,
    //BVEC2, use vec2<I32>(1/0, 1/0)
    //BVEC3, use vec3<I32>(1/0, 1/0, 1/0)
    //BVEC4, use vec4<I32>(1/0, 1/0, 1/0, 1/0)
    IVEC2,
    IVEC3,
    IVEC4,
    UVEC2,
    UVEC3,
    UVEC4,
    VEC2,
    VEC3,
    VEC4,
    DVEC2,
    DVEC3,
    DVEC4,
    IMAT2,
    IMAT3,
    IMAT4,
    UMAT2,
    UMAT3,
    UMAT4,
    MAT2,
    MAT3,
    MAT4,
    DMAT2,
    DMAT3,
    DMAT4,
    FCOLOUR3,
    FCOLOUR4,
    //MAT_N_x_M,
    COUNT
};

using QueryResults = std::array<std::pair<QueryType, I64>, to_base(QueryType::COUNT)>;

using AttributeFlags = std::array<bool, to_base(AttribLocation::COUNT)>;
using AttributeOffsets = std::array<size_t, to_base(AttribLocation::COUNT)>;

};  // namespace Divide

#endif
