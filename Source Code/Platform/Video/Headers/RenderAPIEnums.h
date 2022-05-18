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

constexpr U32 RT_MAX_COLOUR_ATTACHMENTS = 6;

using SamplerAddress = U64;

using RenderTargetID = U32;
constexpr RenderTargetID INVALID_RENDER_TARGET_ID = std::numeric_limits<RenderTargetID>::max();

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

/// A list of built-in sampler slots. Use these if possible and keep them sorted by how often they are used
enum class TextureUsage : U8 {
    UNIT0 = 0u,
    NORMALMAP,
    HEIGHTMAP,
    SHADOW_LAYERED,
    DEPTH,
    SHADOW_SINGLE,
    SHADOW_CUBE,
    OPACITY,
    SPECULAR,
    METALNESS,
    ROUGHNESS,
    OCCLUSION,
    EMISSIVE,
    UNIT1,
    PROJECTION,
    REFLECTION_PLANAR,
    REFLECTION_CUBE,
    REFRACTION_PLANAR,
    REFLECTION_PREFILTERED,
    IRRADIANCE,
    SSR_SAMPLE,
    SSAO_SAMPLE,
    BRDF_LUT,
    SSAO,
    TRANSMITANCE,
    SCENE_NORMALS,
    COUNT
};

static_assert(to_base(TextureUsage::COUNT) <= 32);

namespace Names {
    static constexpr char* textureUsage[] = {
        "UNIT0",
        "NORMALMAP",
        "HEIGHTMAP",
        "SHADOW_LAYERED",
        "DEPTH",
        "SHADOW_SINGLE",
        "SHADOW_CUBE",
        "OPACITY",
        "SPECULAR",
        "METALNESS",
        "ROUGHNESS",
        "OCCLUSION",
        "EMISSIVE",
        "UNIT1",
        "PROJECTION",
        "REFLECTION_PLANAR",
        "REFLECTION_CUBE",
        "REFRACTION_PLANAR",
        "REFLECTION_PREFILTERED",
        "IRRADIANCE",
        "SSR_SAMPLE",
        "SSAO_SAMPLE",
        "BRDF_LUT",
        "SSAO",
        "TRANSMITANCE",
        "SCENE_NORMALS",
        "NONE"
    };
};

static_assert(std::size(Names::textureUsage) == to_base(TextureUsage::COUNT) + 1);

enum class DescriptorSetUsage : U8 {
    PER_FRAME_SET = 0,
    PER_PASS_SET,
    PER_BATCH_SET,
    PER_DRAW_SET,
    COUNT
};

namespace Names {
    static constexpr char* descriptorSetUsage[] = {
         "PER_FRAME_SET", "PER_PASS_SET", "PER_BATCH_SET", "PER_DRAW_SET", "NONE"
    };
};

static_assert(std::size(Names::descriptorSetUsage) == to_base(DescriptorSetUsage::COUNT) + 1);

enum class ReflectorType : U8
{
    PLANAR = 0,
    CUBE,
    COUNT
};

namespace Names {
    static const char* reflectorType[] = {
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
    static const char* refractorType[] = {
        "PLANAR", "NONE"
    };
};

static_assert(std::size(Names::refractorType) == to_base(RefractorType::COUNT) + 1);

/// The different types of lights supported
enum class LightType : U8
{
    DIRECTIONAL = 0,
    POINT = 1,
    SPOT = 2,
    COUNT
};
namespace Names {
    static const char* lightType[] = {
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
    static const char* frustumCollision[] = {
        "FRUSTUM_OUT", "FRUSTUM_IN", "FRUSTUM_INTERSECT", "NONE"
    };
};

static_assert(std::size(Names::frustumCollision) == to_base(FrustumCollision::COUNT) + 1);

enum class FrustumPlane : U8
{
    PLANE_LEFT = 0,
    PLANE_RIGHT,
    PLANE_NEAR,
    PLANE_FAR,
    PLANE_TOP,
    PLANE_BOTTOM,
    COUNT
};

namespace Names {
    static const char* frustumPlane[] = {
        "PLANE_LEFT", "PLANE_RIGHT", "PLANE_NEAR", "PLANE_FAR",
        "PLANE_TOP", "PLANE_BOTTOM", "NONE"
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
    static const char* frustumPoints[] = {
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
    static const char* attribLocation[] = {
        "POSITION", "TEXCOORD", "NORMAL", "TANGENT",
        "COLOR", "BONE_WEIGHT", "BONE_INDICE", "WIDTH",
        "GENERIC", "NONE"
    };
};

static_assert(std::size(Names::attribLocation) == to_base(AttribLocation::COUNT) + 1);

enum class ShaderBufferLocation : U8 {
    CAM_BLOCK = 0,
    GPU_COMMANDS,
    LIGHT_NORMAL,
    LIGHT_SCENE,
    LIGHT_SHADOW,
    LIGHT_INDICES,
    LIGHT_GRID,
    LIGHT_INDEX_COUNT,
    LIGHT_CLUSTER_AABBS,
    NODE_TRANSFORM_DATA,
    NODE_MATERIAL_DATA,
    NODE_TEXTURE_DATA,
    NODE_INDIRECTION_DATA,
    BONE_TRANSFORMS,
    BONE_TRANSFORMS_PREV,
    SCENE_DATA,
    PROBE_DATA,
    GRASS_DATA,
    TREE_DATA,
    CMD_BUFFER,
    LUMINANCE_HISTOGRAM,
    ATOMIC_COUNTER,
    UNIFORM_BLOCK,
    COUNT
};

namespace Names {
    static const char* shaderBufferLocation[] = {
        "CAM_BLOCK",
        "GPU_COMMANDS",
        "LIGHT_NORMAL",
        "LIGHT_SCENE",
        "LIGHT_SHADOW",
        "LIGHT_INDICES",
        "LIGHT_GRID",
        "LIGHT_INDEX_COUNT",
        "LIGHT_CLUSTER_AABBS",
        "NODE_TRANSFORM_DATA",
        "NODE_MATERIAL_DATA",
        "NODE_TEXTURE_DATA",
        "NODE_INDIRECTION_DATA",
        "BONE_TRANSFORMS",
        "BONE_TRANSFORMS_PREV",
        "SCENE_DATA",
        "PROBE_DATA",
        "GRASS_DATA",
        "TREE_DATA",
        "CMD_BUFFER",
        "LUMINANCE_HISTOGRAM",
        "ATOMIC_COUNTER",
        "UNIFORM_BLOCK",
        "NONE"
    };
};

static_assert(std::size(Names::shaderBufferLocation) == to_base(ShaderBufferLocation::COUNT) + 1);

enum class RenderStage : U8 {
    SHADOW = 0,
    REFLECTION = 1,
    REFRACTION = 2,
    DISPLAY = 3,
    COUNT
};

namespace Names {
    static const char* renderStage[] = {
        "SHADOW", "REFLECTION", "REFRACTION", "DISPLAY", "NONE"
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
    static const char* renderPassType[] = {
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
    static const char* pbType[] = {
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
    COUNT
};

namespace Names {
    static const char* primitiveType[] = {
        "POINTS", "LINES", "LINE_STRIP", "TRIANGLES", "TRIANGLE_STRIP",
        "TRIANGLE_FAN", "LINES_ADJANCENCY", "LINE_STRIP_ADJACENCY",
        "TRIANGLES_ADJACENCY", "TRIANGLE_STRIP_ADJACENCY", "PATCH", "NONE"
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
    static const char* blendProperty[] = {
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
    static const char* blendOperation[] = {
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
    static const char* compFunctionNames[] = {
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
    static const char* cullModes[] = {
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
    static const char* shaderTypes[] = {
        "Fragment", "Vertex", "Geometry", "TessellationC", "TessellationE", "Compute", "ERROR!"
    };
};

static_assert(std::size(Names::shaderTypes) == to_base(ShaderType::COUNT) + 1);

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
    static const char* stencilOpNames[] = {
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
    static const char* fillMode[] = {
        "Point", "Wireframe", "Solid", "ERROR!"
    };
};

static_assert(std::size(Names::fillMode) == to_base(FillMode::COUNT) + 1);

enum class TextureType : U8 {
    TEXTURE_1D = 0,
    TEXTURE_2D,
    TEXTURE_3D,
    TEXTURE_CUBE_MAP,
    TEXTURE_2D_ARRAY,
    TEXTURE_CUBE_ARRAY,
    TEXTURE_2D_MS,
    TEXTURE_2D_ARRAY_MS,
    COUNT
};

namespace Names {
    static const char* textureType[] = {
        "TEXTURE_1D", "TEXTURE_2D", "TEXTURE_3D", "TEXTURE_CUBE_MAP", "TEXTURE_2D_ARRAY", "TEXTURE_CUBE_ARRAY", "TEXTURE_2D_MS", "TEXTURE_2D_ARRAY_MS", "NONE"
    };
};

static_assert(std::size(Names::textureType) == to_base(TextureType::COUNT) + 1);

enum class TextureFilter : U8 {
    LINEAR = 0,
    NEAREST,
    COUNT
};

namespace Names {
    static const char* textureFilter[] = {
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
    static const char* textureMipSampling[] = {
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
    static const char* textureWrap[] = {
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
    DEPTH_COMPONENT,
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
    BC7_SRGB,
    //BC3_RGBM,
    //ETC1,
    //ETC2_R,
    //ETC2_RG,
    //ETC2_RGB,
    //ETC2_RGBA,
    //ETC2_RGB_A1,
    //ETC2_RGBM,
    DXT1_RGB_SRGB,
    DXT1_RGBA_SRGB,
    DXT3_RGBA_SRGB,
    DXT5_RGBA_SRGB,
    COUNT,
    DXT1_RGB = BC1,
    DXT1_RGBA = BC1a,
    DXT3_RGBA = BC2,
    DXT5_RGBA = BC3,
};
namespace Names {
    static const char* GFXImageFormat[] = {
        "RED", "RG", "BGR", "RGB", "BGRA", "RGBA", "DEPTH_COMPONENT", "BC1/DXT1_RGB", "BC1a/DXT1_RGBA", "BC2/DXT3_RGBA",
        "BC3/DXT5_RGBA", "BC3n", "BC4s", "BC4u", "BC5s", "BC5u", "BC6s", "BC6u", "BC7", "BC7_SRGB",
        "DXT1_RGB_SRGB", "DXT1_RGBA_SRGB", "DXT3_RGBA_SRGB", "DXT5_RGBA_SRGB", "NONE",
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
    static const char* GFXDataFormat[] = {
        "UNSIGNED_BYTE", "UNSIGNED_SHORT", "UNSIGNED_INT", "SIGNED_BYTE", "SIGNED_SHORT", "SIGNED_INT",
        "FLOAT_16", "FLOAT_32", "ERROR"
    };
};

static_assert(std::size(Names::GFXDataFormat) == to_base(GFXDataFormat::COUNT) + 1);

enum class MemoryBarrierType : U32 {
    BUFFER_UPDATE = toBit(1),
    SHADER_STORAGE = toBit(2),
    COMMAND_BUFFER = toBit(3),
    ATOMIC_COUNTER = toBit(4),
    QUERY = toBit(5),
    RENDER_TARGET = toBit(6),
    TEXTURE_UPDATE = toBit(7),
    TEXTURE_FETCH = toBit(8),
    SHADER_IMAGE = toBit(9),
    TRANSFORM_FEEDBACK = toBit(10),
    VERTEX_ATTRIB_ARRAY = toBit(11),
    INDEX_ARRAY = toBit(12),
    UNIFORM_DATA = toBit(13),
    PIXEL_BUFFER = toBit(14),
    PERSISTENT_BUFFER = toBit(15),
    ALL_MEM_BARRIERS = PERSISTENT_BUFFER + 256,
    TEXTURE_BARRIER = toBit(16), //This is not included in ALL!
    COUNT = 15
};

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
    OTHER,
    COUNT
};

namespace Names {
    static const char* GPUVendor[] = {
        "NVIDIA", "AMD", "INTEL", "MICROSOFT", "IMAGINATION_TECH", "ARM",
        "QUALCOMM", "VIVANTE", "ALPHAMOSAIC", "WEBGL", "OTHER", "ERROR"
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
    COUNT
};

namespace Names {
    static const char* GPURenderer[] = {
        "UNKNOWN", "ADRENO", "GEFORCE", "INTEL", "MALI", "POWERVR",
        "RADEON", "VIDEOCORE", "VIVANTE", "WEBGL", "GDI", "ERROR"
    };
};

static_assert(std::size(Names::GPURenderer) == to_base(GPURenderer::COUNT) + 1);

enum class BufferUpdateUsage : U8 {
    CPU_W_GPU_R = 0, //DRAW
    CPU_R_GPU_W = 1, //READ
    GPU_R_GPU_W = 2, //COPY
    GPU_W_CPU_R = GPU_R_GPU_W, //COPY? Again?
    COUNT
};

namespace Names {
    static const char* bufferUpdateUsage[] = {
        "CPU_W_GPU_R", "CPU_R_GPU_W", "GPU_R_GPU_W / GPU_W_CPU_R", "NONE"
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
    static const char* bufferUpdateFrequency[] = {
        "ONCE", "OCASSIONAL", "OFTEN", "NONE"
    };
};

static_assert(std::size(Names::bufferUpdateFrequency) == to_base(BufferUpdateFrequency::COUNT) + 1);

enum class QueryType : U8 {
    VERTICES_SUBMITTED = 0,
    PRIMITIVES_GENERATED,
    TESSELLATION_PATCHES,
    TESSELLATION_CTRL_INVOCATIONS,
    GPU_TIME,
    COUNT
};

namespace Names {
    static const char* queryType[] = {
        "VERTICES_SUBMITTED", "PRIMITIVES_GENERATED", "TESSELLATION_PATCHES", "TESSELLATION_CTRL_INVOCATIONS", "GPU_TIME", "NONE"
    };
};

static_assert(std::size(Names::queryType) == to_base(QueryType::COUNT) + 1);

};  // namespace Divide

#endif
