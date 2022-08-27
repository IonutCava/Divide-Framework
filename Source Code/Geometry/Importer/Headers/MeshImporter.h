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
#ifndef _MESH_IMPORTER_H_
#define _MESH_IMPORTER_H_

#include "Geometry/Material/Headers/Material.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"
#include "Geometry/Shapes/Headers/Mesh.h"

namespace Divide {
    enum class GeometryFormat : U8
    {
        _3DS = 0, //Studio max format
        ASE, //ASCII Scene Export. Old Unreal format
        FBX,
        MD2,
        MD5,
        OBJ,
        X, //DirectX format
        DAE, //Collada
        GLTF,
        DVD_ANIM,
        DVD_GEOM,
        COUNT
    };

    GeometryFormat GetGeometryFormatForExtension(const char* extension) noexcept;

    const char* const g_geometryExtensions[] = {
        "3ds", "ase", "fbx", "md2", "md5mesh", "obj", "x", "dae", "gltf", "glb", "DVDAnim", "DVDGeom"
    };

    class PlatformContext;
    class ByteBuffer;
    class VertexBuffer;
    namespace Import {
        constexpr U8 MAX_LOD_LEVELS = 3;

        struct TextureEntry {
            bool serialize(ByteBuffer& dataOut) const;
            bool deserialize(ByteBuffer& dataIn);

            PROPERTY_RW(ResourcePath, textureName);
            PROPERTY_RW(ResourcePath, texturePath);

            // Only Albedo/Diffuse should be sRGB
            // Normals, specular, etc should be in linear space
            PROPERTY_RW(bool, srgb, false);
            PROPERTY_RW(bool, useDDSCache, false);
            PROPERTY_RW(bool, isNormalMap, false);
            PROPERTY_RW(bool, alphaForTransparency, false);
            PROPERTY_RW(TextureWrap, wrapU, TextureWrap::REPEAT);
            PROPERTY_RW(TextureWrap, wrapV, TextureWrap::REPEAT);
            PROPERTY_RW(TextureWrap, wrapW, TextureWrap::REPEAT);
            PROPERTY_RW(TextureOperation, operation, TextureOperation::NONE);
        };

        struct MaterialData {
            bool serialize(ByteBuffer& dataOut) const;
            bool deserialize(ByteBuffer& dataIn);

            using SpecularGlossiness = vec2<F32>;

            PROPERTY_RW(bool, ignoreTexDiffuseAlpha, false);
            PROPERTY_RW(bool, doubleSided, true);
            PROPERTY_RW(Str128, name);
            PROPERTY_RW(ShadingMode, shadingMode, ShadingMode::FLAT);
            PROPERTY_RW(BumpMethod,  bumpMethod, BumpMethod::NONE);

            PROPERTY_RW(FColour4, baseColour, DefaultColours::WHITE);
            PROPERTY_RW(FColour3, emissive, DefaultColours::BLACK);
            PROPERTY_RW(FColour3, ambient, DefaultColours::BLACK);
            PROPERTY_RW(FColour4, specular, DefaultColours::BLACK);
            PROPERTY_RW(SpecularGlossiness, specGloss);
            PROPERTY_RW(F32, metallic, 0.0f);
            PROPERTY_RW(F32, roughness, 1.0f);
            PROPERTY_RW(F32, parallaxFactor, 1.0f);
            std::array<TextureEntry, to_base(TextureSlot::COUNT)> _textures;
        };

        struct SubMeshData {
            struct Vertex {
                vec3<F32> position = {0.f, 0.f, 0.f };
                vec3<F32> normal = {0.f, 0.f, 0.f };
                vec4<F32> tangent = { 0.f, 0.f, 0.f, 0.f };
                vec3<F32> texcoord = { 0.f, 0.f, 0.f };
                vec4<F32> weights = {};
                P32       indices = {};
            };

            bool serialize(ByteBuffer& dataOut) const;
            bool deserialize(ByteBuffer& dataIn);

            PROPERTY_RW(Str64, name);
            PROPERTY_RW(U32, index, 0u);
            PROPERTY_RW(U8, boneCount, 0u);
            PROPERTY_RW(vec3<F32>, minPos);
            PROPERTY_RW(vec3<F32>, maxPos);
            PROPERTY_RW(vec3<F32>, worldOffset);

            std::array<U16, MAX_LOD_LEVELS> _partitionIDs = {0u, 0u, 0u};
            vector<vec3<U32>> _triangles[MAX_LOD_LEVELS];
            vector<U32> _indices[MAX_LOD_LEVELS];
            vector<Vertex> _vertices[MAX_LOD_LEVELS];

            MaterialData _material;
        };

        struct ImportData {
            ImportData(ResourcePath modelPath, ResourcePath modelName) noexcept
                : _modelName(MOV(modelName)),
                  _modelPath(MOV(modelPath))
            {
            }

            bool saveToFile(PlatformContext& context, const ResourcePath& path, const ResourcePath& fileName);
            bool loadFromFile(PlatformContext& context, const ResourcePath& path, const ResourcePath& fileName);

            Bone* _skeleton = nullptr;

            // Was it loaded from file, or just created?
            PROPERTY_RW(bool, loadedFromFile, false);
            // Geometry
            VertexBuffer_ptr _vertexBuffer = nullptr;
            // Animations
            PROPERTY_RW(bool, hasAnimations, false);

            // Name and path
            PROPERTY_RW(ResourcePath, modelName);
            PROPERTY_RW(ResourcePath, modelPath);
            PROPERTY_RW(bool, fromFile, false);
            vector<Bone*> _bones;
            Divide::MeshNodeData _nodeData;
            vector<SubMeshData> _subMeshData;
            vector<AnimEvaluator*> _animations;
        };
    };

    FWD_DECLARE_MANAGED_CLASS(Material);

    class MeshImporter
    {
        public:
            static bool loadMeshDataFromFile(PlatformContext& context, Import::ImportData& dataOut);
            static bool loadMesh(bool loadedFromCache, Mesh* mesh, PlatformContext& context, ResourceCache* cache, const Import::ImportData& dataIn);

        protected:
            static Material_ptr loadSubMeshMaterial(ResourceCache* cache, const Import::MaterialData& importData, bool loadedFromCache, bool skinned, std::atomic_uint& taskCounter);
    };

};  // namespace Divide

#endif