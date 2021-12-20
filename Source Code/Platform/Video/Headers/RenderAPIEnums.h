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

enum class RenderAPI : U8 {
    None,      ///< not supported yet
    OpenGL,    ///< 4.x+
    OpenGLES,  ///< 3.x+
    Vulkan,    ///< not supported yet
    COUNT
};

enum class RenderTargetUsage : U8 {
    SCREEN = 0,
    SCREEN_MS,
    OIT,
    OIT_MS,
    OIT_REFLECT,
    POSTFX_DATA,
    SSR_RESULT,
    HI_Z,
    HI_Z_REFLECT,
    REFLECTION_PLANAR,
    REFLECTION_PLANAR_BLUR,
    REFRACTION_PLANAR,
    REFLECTION_CUBE,
    ENVIRONMENT,
    IBL,
    SHADOW,
    EDITOR,
    OTHER,
    COUNT
};
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
    REFRACTION_CUBE,
    REFLECTION_PREFILTERED,
    IRRADIANCE,
    BRDF_LUT,
    POST_FX_DATA,
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
        "REFRACTION_CUBE",
        "REFLECTION_PREFILTERED",
        "IRRADIANCE",
        "BRDF_LUT",
        "POST_FX_DATA",
        "SCENE_NORMALS",
        "NONE"
    };
};

static_assert(std::size(Names::textureUsage) == to_base(TextureUsage::COUNT) + 1);

enum class RefractorType : U8
{
    PLANAR = 0,
    COUNT
};

enum class ReflectorType : U8
{
    PLANAR = 0,
    CUBE,
    COUNT
};

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

enum class FrustumCollision : U8
{
    FRUSTUM_OUT = 0,
    FRUSTUM_IN = 1,
    FRUSTUM_INTERSECT = 2
};

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

enum class ShaderBufferLocation : U8 {
    GPU_BLOCK = 0,
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
    COUNT,
    UNIFORM_BLOCK = COUNT,

    ATOMIC_COUNTER_0 = 0,
    ATOMIC_COUNTER_1 = 1,
    ATOMIC_COUNTER_2 = 2,
    ATOMIC_COUNTER_3 = 3,
    ATOMIC_COUNTER_4 = 4,
};

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


enum class RenderPassType : U8 {
    PRE_PASS = 0,
    MAIN_PASS = 1,
    OIT_PASS = 2,
    COUNT
};

namespace Names {
    static const char* renderPassType[] = {
        "PRE_PASS", "MAIN_PASS", "OIT_PASS", "NONE"
    };
};

enum class PBType : U8 { 
    PB_TEXTURE_1D, 
    PB_TEXTURE_2D, 
    PB_TEXTURE_3D,
    COUNT
};

enum class PrimitiveType : U8 {
    API_POINTS = 0x0000,
    LINES = 0x0001,
    LINE_LOOP = 0x0002,
    LINE_STRIP = 0x0003,
    TRIANGLES = 0x0004,
    TRIANGLE_STRIP = 0x0005,
    TRIANGLE_FAN = 0x0006,
    QUAD_STRIP = 0x0007,
    POLYGON = 0x0008,
    PATCH = 0x0009,
    COUNT
};

namespace Names {
    static const char* primitiveType[] = {
        "POINTS", "LINES", "LINE_LOOP", "LINE_STRIP", "TRIANGLES", "TRIANGLE_STRIP", "TRIANGLE_FAN", "QUAD_STRIP", "POLYGON", "PATCH", "NONE"
    };
};

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

/// Specifies whether front- or back-facing facets are candidates for culling.
enum class CullMode : U8 {
    NONE = 0,
    /// Cull Back facing polygons
    CW,
    BACK = CW,
    /// Cull Front facing polygons
    CCW,
    FRONT = CCW,
    /// Cull All polygons
    ALL,
    /// Place all properties above this.
    COUNT
};

namespace Names {
    static const char* cullModes[] = {
        "None", "CW/BACK", "CCW/FRONT", "ALL", "ERROR!"
    };
};

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
    static const char* fillModes[] = {
        "Point", "Wireframe", "Solid", "ERROR!"
    };
};

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

enum class TextureFilter : U8 {
    LINEAR = 0x0000,
    NEAREST = 0x0001,
    NEAREST_MIPMAP_NEAREST = 0x0002,
    LINEAR_MIPMAP_NEAREST = 0x0003,
    NEAREST_MIPMAP_LINEAR = 0x0004,
    LINEAR_MIPMAP_LINEAR = 0x0005,
    COUNT
};

namespace Names {
    static const char* textureFilter[] = {
        "LINEAR", "NEAREST", "NEAREST_MIPMAP_NEAREST", "LINEAR_MIPMAP_NEAREST", "NEAREST_MIPMAP_LINEAR", "LINEAR_MIPMAP_LINEAR", "NONE"
    };
};

enum class TextureWrap : U8 {
    // Texture coordinates outside [0...1] are clamped to the nearest valid value.
    CLAMP = 0x0,
    CLAMP_TO_EDGE = 0x1,
    CLAMP_TO_BORDER = 0x2,
    // If the texture coordinates for a pixel are outside [0...1] the texture is not applied to that pixel
    DECAL = 0x3,
    REPEAT = 0x4,
    MIRROR_REPEAT = 0x5,
    COUNT
};

namespace Names {
    static const char* textureWrap[] = {
        "CLAMP", "CLAMP_TO_EDGE", "CLAMP_TO_BORDER", "DECAL", "REPEAT", "MIRROR_REPEAT", "NONE"
    };
};

enum class GFXImageFormat : U8 {
    RED = 0,
    RG,
    BGR,
    RGB,
    BGRA,
    RGBA,
    DEPTH_COMPONENT,
    COMPRESSED_RGB_DXT1,
    COMPRESSED_RGBA_DXT1,
    COMPRESSED_RGBA_DXT3,
    COMPRESSED_RGBA_DXT5,
    COUNT
};

enum class GFXDataFormat : U8 {
    UNSIGNED_BYTE = 0x0000,
    UNSIGNED_SHORT = 0x0001,
    UNSIGNED_INT = 0x0002,
    SIGNED_BYTE = 0x0003,
    SIGNED_SHORT = 0x0004,
    SIGNED_INT = 0x0005,
    FLOAT_16 = 0x0006,
    FLOAT_32 = 0x0008,
    COUNT
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

enum class BufferUpdateUsage : U8 {
    CPU_W_GPU_R = 0, //DRAW
    CPU_R_GPU_W = 1, //READ
    GPU_R_GPU_W = 2, //COPY
    GPU_W_CPU_R = GPU_R_GPU_W, //COPY? Again?
    COUNT
};

enum class BufferUpdateFrequency : U8 {
    ONCE = 0,       //STATIC
    OCASSIONAL = 1, //DYNAMIC
    OFTEN = 2,      //STREAM
    COUNT
};

enum class QueryType : U8 {
    VERTICES_SUBMITTED = 0,
    PRIMITIVES_GENERATED,
    TESSELLATION_PATCHES,
    TESSELLATION_CTRL_INVOCATIONS,
    GPU_TIME,
    COUNT
};


};  // namespace Divide

#endif
