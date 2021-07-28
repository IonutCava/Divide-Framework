/*
   Copyright (c) 2020 DIVIDE-Studio
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
#ifndef _MATERIAL_ENUMS_H_
#define _MATERIAL_ENUMS_H_

namespace Divide {
    enum class MaterialDebugFlag : U8 {
        ALBEDO = 0,
        DEPTH,
        LIGHTING,
        SPECULAR,
        KS,
        UV,
        SSAO,
        EMISSIVE,
        ROUGHNESS,
        METALNESS,
        NORMALS,
        TANGENTS,
        BITANGENTS,
        SHADOW_MAPS,
        CSM_SPLITS,
        LIGHT_HEATMAP,
        DEPTH_CLUSTERS,
        REFLECTIONS,
        REFRACTIONS,
        MATERIAL_IDS,
        SSR,
        COUNT
    };
    namespace Names {
        static const char* materialDebugFlag[] = {
            "ALBEDO",
            "DEPTH",
            "LIGHTING",
            "SPECULAR",
            "KS",
            "UV",
            "SSAO",
            "EMISSIVE",
            "ROUGHNESS",
            "METALNESS",
            "NORMALS",
            "TANGENTS",
            "BITANGENTS",
            "SHADOW_MAPS",
            "CSM_SPLITS",
            "LIGHT_HEATMAP",
            "DEPTH_CLUSTERS",
            "REFLECTIONS",
            "REFRACTIONS",
            "MATERIAL_IDS",
            "SSR",
            "NONE",
        };
    };

    static_assert(ArrayCount(Names::materialDebugFlag) == to_base(MaterialDebugFlag::COUNT) + 1, "MaterialDebugFlag name array out of sync!");

    enum class BumpMethod : U8 {
        NONE = 0,
        NORMAL = 1,
        PARALLAX = 2,
        PARALLAX_OCCLUSION = 3,
        COUNT
    };
    namespace Names {
        static const char* bumpMethod[] = {
            "NONE", "NORMAL", "PARALLAX", "PARALLAX_OCCLUSION", "UNKNOWN"
        };
    };

    static_assert(ArrayCount(Names::bumpMethod) == to_base(BumpMethod::COUNT) + 1, "BumpMethod name array out of sync!");

    /// How should each texture be added
    enum class TextureOperation : U8 {
        NONE = 0,
        MULTIPLY = 1,
        ADD = 2,
        SUBTRACT = 3,
        DIVIDE = 4,
        SMOOTH_ADD = 5,
        SIGNED_ADD = 6,
        DECAL = 7,
        REPLACE = 8,
        COUNT
    };
    namespace Names {
        static const char* textureOperation[] = {
            "NONE", "MULTIPLY", "ADD", "SUBTRACT", "DIVIDE", "SMOOTH_ADD", "SIGNED_ADD", "DECAL", "REPLACE", "UNKNOW",
        };
    };

    static_assert(ArrayCount(Names::textureOperation) == to_base(TextureOperation::COUNT) + 1, "TextureOperation name array out of sync!");

    enum class TranslucencySource : U8 {
        ALBEDO_COLOUR,
        ALBEDO_TEX,
        OPACITY_MAP_R, //single channel
        OPACITY_MAP_A, //rgba texture
        COUNT
    };

    namespace Names {
        static const char* translucencySource[] = {
            "ALBEDO_COLOUR", "ALBEDO_TEX", "OPACITY_MAP_R", "OPACITY_MAP_A", "NONE"
        };
    };

    static_assert(ArrayCount(Names::translucencySource) == to_base(TranslucencySource::COUNT) + 1, "TranslucencySource name array out of sync!");

    /// Not used yet but implemented for shading model selection in shaders
    /// This enum matches the ASSIMP one on a 1-to-1 basis
    enum class ShadingMode : U8 {
        FLAT = 0,
        PHONG,
        BLINN_PHONG,
        TOON,
        // Use PBR for the following
        OREN_NAYAR,
        COOK_TORRANCE,
        COUNT
    };

    namespace Names {
        static const char* shadingMode[] = {
            "FLAT", "PHONG", "BLINN_PHONG", "TOON", "OREN_NAYAR", "COOK_TORRANCE", "NONE"
        };
    };

    static_assert(ArrayCount(Names::shadingMode) == to_base(ShadingMode::COUNT) + 1, "ShadingMode name array out of sync!");

}; //namespace Divide

#endif //_MATERIAL_ENUMS_H_