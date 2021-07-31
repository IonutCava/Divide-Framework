#include "stdafx.h"

#include "config.h"

#include "Headers/DVDConverter.h"
#include "Headers/MeshImporter.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Geometry/Animations/Headers/AnimationUtils.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"
#include "Geometry/Shapes/Headers/SkinnedSubMesh.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/Localization.h"

#include <assimp/DefaultLogger.hpp>
#include <assimp/Importer.hpp>
#include <assimp/Logger.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/pbrmaterial.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/types.h>

#include <meshoptimizer/src/meshoptimizer.h>

namespace Divide {

namespace {
        constexpr bool g_useSloppyMeshSimplification = false;
        constexpr U8 g_SloppyTrianglePercentPerLoD = 80;
        constexpr U8 g_PreciseTrianglePercentPerLoD = 70;
        constexpr size_t g_minIndexCountForAutoLoD = 1024;

        std::atomic_bool g_wasSetUp = false;

        struct AssimpStream final : Assimp::LogStream {
            void write(const char* message) override
            {
                Console::printf("%s\n", message);
            }
        };

        // Select the kinds of messages you want to receive on this log stream
        constexpr U32 g_severity = Config::Build::IS_DEBUG_BUILD ? Assimp::Logger::VERBOSE : Assimp::Logger::NORMAL;

        constexpr bool g_removeLinesAndPoints = true;

        struct vertexWeight {
            U8 _boneID = 0;
            F32 _boneWeight = 0.0f;
        };

        /// Recursively creates an internal node structure matching the current scene and animation.
        Bone* CreateBoneTree(aiNode* pNode, Bone* parent) {
            Bone* internalNode = MemoryManager_NEW Bone(pNode->mName.data);
            // set the parent; in case this is the root node, it will be null
            internalNode->_parent = parent;

            AnimUtils::TransformMatrix(pNode->mTransformation, internalNode->_localTransform);
            internalNode->_originalLocalTransform = internalNode->_localTransform;
            CalculateBoneToWorldTransform(internalNode);

            // continue for all child nodes and assign the created internal nodes as our
            // children recursively call this function on all children
            for (U32 i = 0; i < pNode->mNumChildren; ++i) {
                internalNode->_children.push_back(
                    CreateBoneTree(pNode->mChildren[i], internalNode));
            }

            return internalNode;
        }
    };

    hashMap<U32, TextureWrap> DVDConverter::fillTextureWrapMap() {
        hashMap<U32, TextureWrap> wrapMap;
        wrapMap[aiTextureMapMode_Wrap] = TextureWrap::CLAMP;
        wrapMap[aiTextureMapMode_Clamp] = TextureWrap::CLAMP_TO_EDGE;
        wrapMap[aiTextureMapMode_Mirror] = TextureWrap::REPEAT;
        wrapMap[aiTextureMapMode_Decal] = TextureWrap::DECAL;
        return wrapMap;
    }

    hashMap<U32, ShadingMode> DVDConverter::fillShadingModeMap() {
        hashMap<U32, ShadingMode> shadingMap;
        shadingMap[aiShadingMode_Fresnel] = ShadingMode::COOK_TORRANCE;
        shadingMap[aiShadingMode_NoShading] = ShadingMode::FLAT;
        shadingMap[aiShadingMode_CookTorrance] = ShadingMode::COOK_TORRANCE;
        shadingMap[aiShadingMode_Minnaert] = ShadingMode::OREN_NAYAR;
        shadingMap[aiShadingMode_OrenNayar] = ShadingMode::OREN_NAYAR;
        shadingMap[aiShadingMode_Toon] = ShadingMode::TOON;
        shadingMap[aiShadingMode_Blinn] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_Phong] = ShadingMode::PHONG;
        shadingMap[aiShadingMode_Gouraud] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_Flat] = ShadingMode::FLAT;
        return shadingMap;
    }

    hashMap<U32, TextureOperation> DVDConverter::fillTextureOperationMap() {
        hashMap<U32, TextureOperation> operationMap;
        operationMap[aiTextureOp_Multiply] = TextureOperation::MULTIPLY;
        operationMap[aiTextureOp_Add] = TextureOperation::ADD;
        operationMap[aiTextureOp_Subtract] = TextureOperation::SUBTRACT;
        operationMap[aiTextureOp_Divide] = TextureOperation::DIVIDE;
        operationMap[aiTextureOp_SmoothAdd] = TextureOperation::SMOOTH_ADD;
        operationMap[aiTextureOp_SignedAdd] = TextureOperation::SIGNED_ADD;
        operationMap[/*aiTextureOp_Replace*/ 7] = TextureOperation::REPLACE;
        return operationMap;
    }


    hashMap<U32, TextureWrap>
    DVDConverter::aiTextureMapModeTable = fillTextureWrapMap();
    hashMap<U32, ShadingMode>
    DVDConverter::aiShadingModeInternalTable = fillShadingModeMap();
    hashMap<U32, TextureOperation>
    DVDConverter::aiTextureOperationTable = fillTextureOperationMap();

DVDConverter::DVDConverter(PlatformContext& context, Import::ImportData& target, bool& result) {
    bool expected = false;
    if (g_wasSetUp.compare_exchange_strong(expected, true)) {
        Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);
        Assimp::DefaultLogger::get()->attachStream(new AssimpStream(), g_severity);
    }

    result = load(context, target);
}

bool DVDConverter::load(PlatformContext& context, Import::ImportData& target) const {
    const ResourcePath& filePath = target.modelPath();
    const ResourcePath& fileName = target.modelName();

    Assimp::Importer importer;

    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, g_removeLinesAndPoints ? aiPrimitiveType_LINE | aiPrimitiveType_POINT : 0);
    //importer.SetPropertyInteger(AI_CONFIG_IMPORT_FBX_SEARCH_EMBEDDED_TEXTURES, 1);
    importer.SetPropertyInteger(AI_CONFIG_IMPORT_TER_MAKE_UVS, 1);
    importer.SetPropertyInteger(AI_CONFIG_GLOB_MEASURE_TIME, 1);
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 80.0f);

    constexpr U32 ppSteps = aiProcess_CalcTangentSpace |
                            aiProcess_JoinIdenticalVertices |
                            aiProcess_ImproveCacheLocality |
                            aiProcess_GenSmoothNormals |
                            aiProcess_LimitBoneWeights |
                            aiProcess_RemoveRedundantMaterials |
                             //aiProcess_FixInfacingNormals | // Causes issues with backfaces inside the Sponza Atrium model
                            aiProcess_SplitLargeMeshes |
                            aiProcess_FindInstances |
                            aiProcess_Triangulate |
                            aiProcess_GenUVCoords |
                            aiProcess_SortByPType |
                            aiProcess_FindDegenerates |
                            aiProcess_FindInvalidData |
                            aiProcess_ValidateDataStructure |
                            aiProcess_OptimizeMeshes |
                            aiProcess_TransformUVCoords;// Preprocess UV transformations (scaling, translation ...)

    const aiScene* aiScenePointer = importer.ReadFile((filePath.str() + "/" + fileName.str()).c_str(), ppSteps);

    if (!aiScenePointer) {
        Console::errorfn(Locale::Get(_ID("ERROR_IMPORTER_FILE")), fileName.c_str(), importer.GetErrorString());
        return false;
    }

    GeometryFormat format = GeometryFormat::COUNT;
    for (const char* extension : g_geometryExtensions) {
        if (hasExtension(fileName, extension)) {
            format = GetGeometryFormatForExtension(extension);
            break;
        }
    }

    target.hasAnimations(aiScenePointer->HasAnimations());
    if (target.hasAnimations()) {
        target.skeleton(CreateBoneTree(aiScenePointer->mRootNode, nullptr));
        target._bones.reserve(to_I32(target.skeleton()->hierarchyDepth()));

        for (U16 meshPointer = 0; meshPointer < aiScenePointer->mNumMeshes; ++meshPointer) {
            const aiMesh* mesh = aiScenePointer->mMeshes[meshPointer];
            for (U32 n = 0; n < mesh->mNumBones; ++n) {
                const aiBone* bone = mesh->mBones[n];

                Bone* found = target.skeleton()->find(bone->mName.data);
                if (found != nullptr) {
                    AnimUtils::TransformMatrix(bone->mOffsetMatrix, found->_offsetMatrix);
                    target._bones.push_back(found);
                }
            }
        }

        for (size_t i(0); i < aiScenePointer->mNumAnimations; i++) {
            aiAnimation* animation = aiScenePointer->mAnimations[i];
            if (animation->mDuration > 0.0f) {
                target._animations.push_back(std::make_shared<AnimEvaluator>(animation, to_U32(i)));
            }
        }
    }

    const U32 numMeshes = aiScenePointer->mNumMeshes;
    target._subMeshData.reserve(numMeshes);

    Str64 prevName = "";
    for (U16 n = 0; n < numMeshes; ++n) {
        aiMesh* currentMesh = aiScenePointer->mMeshes[n];
        // Skip points and lines ... for now -Ionut
        if (currentMesh->mNumVertices == 0) {
            continue;
        }

        stringImpl fullName = currentMesh->mName.C_Str();
        if (fullName.length() >= 64) {
            fullName = fullName.substr(0, 64);
        }

        Str64 name = fullName.c_str();
        if (Util::CompareIgnoreCase(name, "defaultobject")) {
            name.append("_" + fileName.str());
        }

        Import::SubMeshData subMeshTemp = {};
        subMeshTemp.name(name);
        subMeshTemp.index(to_U32(n));
        if (subMeshTemp.name().empty()) {
            subMeshTemp.name(Util::StringFormat("%s-submesh-%d", fileName.c_str(), n).c_str());
        }
        if (subMeshTemp.name() == prevName) {
            subMeshTemp.name(prevName + "_" + Util::to_string(n).c_str());
        }

        prevName = subMeshTemp.name();
        subMeshTemp.boneCount(to_U8(currentMesh->mNumBones));
        loadSubMeshGeometry(currentMesh, subMeshTemp);

        loadSubMeshMaterial(subMeshTemp._material,
                            aiScenePointer,
                            to_U16(currentMesh->mMaterialIndex),
                            Str128(subMeshTemp.name()) + "_material",
                            format,
                            true);


        target._subMeshData.push_back(subMeshTemp);
    }

    BuildGeometryBuffers(context, target);
    return true;
}

void DVDConverter::BuildGeometryBuffers(PlatformContext& context, Import::ImportData& target) {
    target.vertexBuffer(context.gfx().newVB());
    VertexBuffer* vb = target.vertexBuffer();

    size_t indexCount = 0, vertexCount = 0;
    for (U8 lod = 0; lod < Import::MAX_LOD_LEVELS; ++lod) {
        for (const Import::SubMeshData& data : target._subMeshData) {
            indexCount += data._indices[lod].size();
            vertexCount += data._vertices[lod].size();
        }
    }

    vb->useLargeIndices(vertexCount + 1 > U16_MAX);
    vb->setVertexCount(vertexCount);
    vb->reserveIndexCount(indexCount);

    U32 previousOffset = 0;
    for (U8 lod = 0; lod < Import::MAX_LOD_LEVELS; ++lod) {
        U8 subMeshBoneOffset = 0;
        for (Import::SubMeshData& data : target._subMeshData) {
            const size_t idxCount = data._indices[lod].size();

            if (idxCount == 0) {
                assert(lod > 0);
                subMeshBoneOffset += data.boneCount();
                data._partitionIDs[lod] = data._partitionIDs[lod - 1];
                continue;
            }

            data._triangles[lod].reserve(idxCount / 3);
            const auto& indices = data._indices[lod];
            for (size_t i = 0; i < idxCount; i += 3) {
                U32 triangleTemp[3] = {
                    indices[i + 0] + previousOffset,
                    indices[i + 1] + previousOffset,
                    indices[i + 2] + previousOffset
                };

                data._triangles[lod].emplace_back(triangleTemp[0], triangleTemp[1], triangleTemp[2]);

                vb->addIndex(triangleTemp[0]);
                vb->addIndex(triangleTemp[1]);
                vb->addIndex(triangleTemp[2]);
            }

            auto& vertices = data._vertices[lod];
            const U32 vertCount = to_U32(vertices.size());

            const bool hasBones = data.boneCount() > 0;
            const bool hasTexCoord = !IS_ZERO(vertices[0].texcoord.z);
            const bool hasTangent = !IS_ZERO(vertices[0].tangent.w);

            BoundingBox importBB = {};
            for (U32 i = 0; i < vertCount; ++i) {
                const U32 targetIdx = i + previousOffset;

                const Import::SubMeshData::Vertex& vert = vertices[i];

                vb->modifyPositionValue(targetIdx, vert.position);
                vb->modifyNormalValue(targetIdx, vert.normal);

                if (hasTexCoord) {
                    vb->modifyTexCoordValue(targetIdx, vert.texcoord.xy);
                }
                if (hasTangent) {
                    vb->modifyTangentValue(targetIdx, vert.tangent.xyz);
                }
                if (lod == 0) {
                    importBB.add(vert.position);
                }
            }//vertCount

            if (hasBones) {
                for (U32 i = 0; i < vertCount; ++i) {
                    const U32 targetIdx = i + previousOffset;

                    Import::SubMeshData::Vertex& vert = vertices[i];
                    P32& boneIndices = vert.indices;
                    for (U8& idx : boneIndices.b) {
                        idx += subMeshBoneOffset;
                    }

                    vb->modifyBoneIndices(targetIdx, boneIndices);
                    vb->modifyBoneWeights(targetIdx, vert.weights);
                }
            }

            if (lod == 0) {
                data.minPos(importBB.getMin());
                data.maxPos(importBB.getMax());
            }

            subMeshBoneOffset += data.boneCount();
            previousOffset += to_U32(vertices.size());
            data._partitionIDs[lod] = vb->partitionBuffer();
        } //submesh data
    } //lod
}

void DVDConverter::loadSubMeshGeometry(const aiMesh* source, Import::SubMeshData& subMeshData) const {
    vectorEASTL<U32> input_indices;
    input_indices.reserve(source->mNumFaces * 3);
    for (U32 k = 0; k < source->mNumFaces; k++) {
        // guaranteed to be 3 thanks to aiProcess_Triangulate 
        for (U32 m = 0; m < 3; ++m) {
            input_indices.push_back(source->mFaces[k].mIndices[m]);
        }
    }

    vectorEASTL<Import::SubMeshData::Vertex> vertices(source->mNumVertices);

    for (U32 j = 0; j < source->mNumVertices; ++j) {
        vertices[j].position.set(source->mVertices[j].x, source->mVertices[j].y, source->mVertices[j].z);
        vertices[j].normal.set(source->mNormals[j].x, source->mNormals[j].y, source->mNormals[j].z);
    }

    if (source->mTextureCoords[0] != nullptr) {
        for (U32 j = 0; j < source->mNumVertices; ++j) {
            vertices[j].texcoord.set(source->mTextureCoords[0][j].x, source->mTextureCoords[0][j].y, 1.0f);
        }
    }

    if (source->mTangents != nullptr) {
        for (U32 j = 0; j < source->mNumVertices; ++j) {
            vertices[j].tangent.set(source->mTangents[j].x, source->mTangents[j].y, source->mTangents[j].z, 1.0f);
        }
    } else {
        Console::d_printfn(Locale::Get(_ID("SUBMESH_NO_TANGENT")), subMeshData.name().c_str());
    }

    if (source->mNumBones > 0) {
        assert(source->mNumBones < U8_MAX);  ///<Fit in U8

        vectorEASTL<vectorEASTL<vertexWeight> > weightsPerVertex(source->mNumVertices);
        for (U8 a = 0; a < source->mNumBones; ++a) {
            const aiBone* bone = source->mBones[a];
            for (U32 b = 0; b < bone->mNumWeights; ++b) {
                weightsPerVertex[bone->mWeights[b].mVertexId].push_back({ a, bone->mWeights[b].mWeight });
            }
        }

        vec4<F32> weights;
        P32       indices;
        for (U32 j = 0; j < source->mNumVertices; ++j) {
            indices.i = 0;
            weights.reset();
            // guaranteed to be max 4 thanks to aiProcess_LimitBoneWeights 
            for (size_t a = 0; a < weightsPerVertex[j].size(); ++a) {
                indices.b[a] = to_U8(weightsPerVertex[j][a]._boneID);
                weights[a] = weightsPerVertex[j][a]._boneWeight;
            }
            vertices[j].indices = indices;
            vertices[j].weights.set(weights);
        }
    }

    auto& target_indices = subMeshData._indices[0];
    auto& target_vertices = subMeshData._vertices[0];

    constexpr F32 kThreshold = 1.01f; // allow up to 1% worse ACMR to get more reordering opportunities for overdraw

    const size_t index_count = input_indices.size();
    target_indices.resize(index_count);

    { //Remap VB & IB
        vectorEASTL<U32> remap(source->mNumVertices);
        const size_t vertex_count = meshopt_generateVertexRemap(&remap[0], input_indices.data(), input_indices.size(), &vertices[0], source->mNumVertices, sizeof(Import::SubMeshData::Vertex));

        meshopt_remapIndexBuffer(&target_indices[0], &input_indices[0], input_indices.size(), &remap[0]);
        input_indices.clear();

        target_vertices.resize(vertex_count);
        meshopt_remapVertexBuffer(&target_vertices[0], &vertices[0], source->mNumVertices, sizeof(Import::SubMeshData::Vertex), &remap[0]);
        vertices.clear();
        remap.clear();
    }
    { // Optimise VB & IB
        meshopt_optimizeVertexCache(&target_indices[0], &target_indices[0], index_count, target_vertices.size());


        meshopt_optimizeOverdraw(&target_indices[0], &target_indices[0], index_count, &target_vertices[0].position.x, target_vertices.size(), sizeof(Import::SubMeshData::Vertex), kThreshold);

        // vertex fetch optimization should go last as it depends on the final index order
        meshopt_optimizeVertexFetch(&target_vertices[0], &target_indices[0], index_count, &target_vertices[0], target_vertices.size(), sizeof(Import::SubMeshData::Vertex));
    }
    { // Generate LoD data and place inside VB & IB with proper offsets
        // We only simplify from the base level. Simplifying from the previous level might be faster,
        // but we cache the geometry anyway at the end
        auto& source_indices = subMeshData._indices[0];
        auto& source_vertices = subMeshData._vertices[0];

        constexpr F32 targetSimplification = (g_useSloppyMeshSimplification ? g_SloppyTrianglePercentPerLoD : g_PreciseTrianglePercentPerLoD) * 0.01f;

        if (source_indices.size() >= g_minIndexCountForAutoLoD) {
            for (U8 i = 1; i < Import::MAX_LOD_LEVELS; ++i) {
                auto& lod_indices = subMeshData._indices[i];

                const F32 threshold = std::pow(targetSimplification, to_F32(i));

                const size_t target_index_count = std::min(source_indices.size(), to_size(index_count * threshold) / 3 * 3);

                lod_indices.resize(source_indices.size());
                constexpr F32 target_error = 1e-2f;
                F32 result_error = 0.f;
                if_constexpr (g_useSloppyMeshSimplification) {
                    lod_indices.resize(meshopt_simplifySloppy(lod_indices.data(),
                                                              source_indices.data(),
                                                              source_indices.size(),
                                                              &source_vertices[0].position.x,
                                                              source_vertices.size(),
                                                              sizeof(Import::SubMeshData::Vertex),
                                                              target_index_count,
                                                              target_error,
                                                              &result_error));
                } else {
                    lod_indices.resize(meshopt_simplify(lod_indices.data(),
                                                        source_indices.data(),
                                                        source_indices.size(),
                                                        &source_vertices[0].position.x,
                                                        source_vertices.size(),
                                                        sizeof(Import::SubMeshData::Vertex),
                                                        target_index_count,
                                                        target_error,
                                                        &result_error));
                }
            }

            // reorder indices for overdraw, balancing overdraw and vertex cache efficiency
            for (U8 i = 1; i < Import::MAX_LOD_LEVELS; ++i) {
                auto& lod_indices = subMeshData._indices[i];
                auto& lod_vertices = subMeshData._vertices[i];

                meshopt_optimizeVertexCache(lod_indices.data(),
                                            lod_indices.data(),
                                            lod_indices.size(),
                                            source_vertices.size());

                meshopt_optimizeOverdraw(lod_indices.data(),
                                         lod_indices.data(),
                                         lod_indices.size(),
                                         &source_vertices[0].position.x,
                                         source_vertices.size(),
                                         sizeof(Import::SubMeshData::Vertex),
                                         1.0f);

                lod_vertices.resize(source_vertices.size());
                lod_vertices.resize(meshopt_optimizeVertexFetch(lod_vertices.data(),
                                                                lod_indices.data(),
                                                                lod_indices.size(),
                                                                source_vertices.data(),
                                                                source_vertices.size(),
                                                                sizeof(Import::SubMeshData::Vertex)));
            }
        }
    }
}

void DVDConverter::loadSubMeshMaterial(Import::MaterialData& material,
                                       const aiScene* source,
                                       const U16 materialIndex,
                                       const Str128& materialName,
                                       const GeometryFormat format,
                                       bool convertHeightToBumpMap) const
{

    const aiMaterial* mat = source->mMaterials[materialIndex];

    // ------------------------------- Part 1: Material properties --------------------------------------------
    material.name(materialName);
    // Ignored properties: 
    // - AI_MATKEY_ENABLE_WIREFRAME
    // - AI_MATKEY_BLEND_FUNC
    // - AI_MATKEY_REFLECTIVITY
    // - AI_MATKEY_REFRACTI
    // - AI_MATKEY_COLOR_TRANSPARENT
    // - AI_MATKEY_COLOR_REFLECTIVE
    // - AI_MATKEY_GLOBAL_BACKGROUND_IMAGE
    // - AI_MATKEY_GLOBAL_SHADERLANG
    // - AI_MATKEY_SHADER_VERTEX
    // - AI_MATKEY_SHADER_FRAGMENT
    // - AI_MATKEY_SHADER_GEO
    // - AI_MATKEY_SHADER_TESSELATION
    // - AI_MATKEY_SHADER_PRIMITIVE
    // - AI_MATKEY_SHADER_COMPUTE
    // - AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR
    // - AI_MATKEY_GLTF_ALPHAMODE
    // - AI_MATKEY_GLTF_ALPHACUTOFF
    // - AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS
    // - AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS_GLOSSINESS_FACTOR

    { // Load shading mode
        // The default shading model should be set to the classic SpecGloss Phong
        I32 shadingModel = 0, unlit = 0, flags = 0;
        if (AI_SUCCESS == aiGetMaterialInteger(mat, AI_MATKEY_GLTF_UNLIT, &unlit) && unlit == 1) {
            material.shadingMode(ShadingMode::FLAT);
        } else if (AI_SUCCESS == aiGetMaterialInteger(mat, AI_MATKEY_SHADING_MODEL, &shadingModel)) {
            material.shadingMode(aiShadingModeInternalTable[shadingModel]);
        } else {
            material.shadingMode(ShadingMode::BLINN_PHONG);
        }
        aiGetMaterialInteger(mat, AI_MATKEY_TEXFLAGS_DIFFUSE(0), &flags);
        const bool hasIgnoreAlphaFlag = (flags & aiTextureFlags_IgnoreAlpha) != 0;
        if (hasIgnoreAlphaFlag) {
            material.ignoreTexDiffuseAlpha(true);
        }
        const bool hasUseAlphaFlag = (flags & aiTextureFlags_UseAlpha) != 0;
        if (hasUseAlphaFlag) {
            material.ignoreTexDiffuseAlpha(false);
        }
    }

    {// Load diffuse colour
        material.baseColour(FColour4(0.8f, 0.8f, 0.8f, 1.f));
        aiColor4D diffuse;
        if (AI_SUCCESS == aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &diffuse)) {
            material.baseColour(FColour4(diffuse.r, diffuse.g, diffuse.b, diffuse.a));
        } else {
            Console::d_printfn(Locale::Get(_ID("MATERIAL_NO_DIFFUSE")), materialName.c_str());
        }
        // Load material opacity value. Shouldn't really be used since we can use opacity maps for this
        F32 alpha = 1.0f;
        bool set = false;

        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_OPACITY, &alpha)) {
            set = true;
        } else  if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_TRANSPARENCYFACTOR, &alpha)) {
            alpha = 1.f - alpha;
            set = true;
        }

        if (set && alpha > 0.f && alpha < 1.f) {
            FColour4 base = material.baseColour();
            base.a *= alpha;
            material.baseColour(base);
        }
    }

    F32 specStrength = 1.f;
    { // Load specular colour
        F32 specShininess = 0.f;
        aiGetMaterialFloat(mat, AI_MATKEY_SHININESS_STRENGTH, &specStrength);


        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_SHININESS, &specShininess)) {
            // Adjust shininess range here so that it always maps to the range [0,1000]
            switch (format) {
                case GeometryFormat::_3DS:
                case GeometryFormat::ASE:
                case GeometryFormat::FBX:  specShininess *= 10.f;                         break; // percentage (0-100%)
                case GeometryFormat::OBJ:  specShininess /= 4.f;                          break; // 4000.f
                case GeometryFormat::DAE:  REMAP(specShininess, 0.f, 511.f, 0.f, 1000.f); break; // 511.f
                case GeometryFormat::X:    specShininess = 1000.f;                        break; //no supported. 0 = gouraud shading
            };
            CLAMP(specShininess, 0.f, 1000.f);
        }

        material.specular({ specStrength, specStrength, specStrength, specShininess });

        aiColor4D specular;
        if (AI_SUCCESS == aiGetMaterialColor(mat, AI_MATKEY_COLOR_SPECULAR, &specular)) {
            material.specular({ specular.r * specStrength, specular.g * specStrength, specular.b * specStrength, specShininess });
        }
    }
    { // Load emissive colour
        material.emissive(FColour3(0.f, 0.f, 0.f));
        // Load emissive colour
        aiColor4D emissive;
        if (AI_SUCCESS == aiGetMaterialColor(mat, AI_MATKEY_COLOR_EMISSIVE, &emissive)) {
            material.emissive(FColour3(emissive.r, emissive.g, emissive.b));
        }
    }
    { // Load ambient colour
        material.ambient(FColour3(0.f, 0.f, 0.f));
        aiColor4D ambient;
        if (AI_SUCCESS == aiGetMaterialColor(mat, AI_MATKEY_COLOR_AMBIENT, &ambient)) {
            // Don't use this. Set it manually in the editor!
            //material.ambient(FColour3(ambient.r, ambient.g, ambient.b));
        }
    }
    { // Load metallic & roughness
        material.metallic(0.f);
        material.roughness(1.f);

        F32 roughness = 0.f, metallic = 0.f;
        // Load metallic
        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, &metallic)) {
            material.metallic(metallic);
        }
        // Load roughness
        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, &roughness)) {
            material.roughness(roughness);
        }
    }
    { // Other material properties
        F32 bumpScaling = 0.0f;
        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_BUMPSCALING, &bumpScaling)) {
            material.parallaxFactor(bumpScaling);
        }

        I32 twoSided = 0;
        aiGetMaterialInteger(mat, AI_MATKEY_TWOSIDED, &twoSided);
        material.doubleSided(twoSided != 0);
    }

    aiString tName;
    aiTextureMapping mapping = aiTextureMapping_OTHER;
    U32 uvInd = 0;
    F32 blend = 0.0f;
    aiTextureOp op = aiTextureOp_Multiply;
    aiTextureMapMode mode[3] = { _aiTextureMapMode_Force32Bit,
                                 _aiTextureMapMode_Force32Bit,
                                 _aiTextureMapMode_Force32Bit };

    const auto loadTexture = [&material](const TextureUsage usage, TextureOperation texOp, const aiString& name, aiTextureMapMode* wrapMode, const bool srgb = false) {
        DIVIDE_ASSERT(name.length > 0);

        // it might be an embedded texture
        /*const aiTexture* texture = mat->GetEmbeddedTexture(name.C_Str());

        if (texture != nullptr )
        {
        }*/
        const ResourcePath path(Paths::g_assetsLocation + Paths::g_texturesLocation + name.C_Str());
        auto [img_name, img_path] = splitPathToNameAndLocation(path);

        // if we have a name and an extension
        if (!img_name.str().substr(img_name.str().find_first_of('.'), Str64::npos).empty()) {
            Import::TextureEntry& texture = material._textures[to_base(usage)];
            // Load the texture resource
            if (IS_IN_RANGE_INCLUSIVE(wrapMode[0], aiTextureMapMode_Wrap, aiTextureMapMode_Decal) &&
                IS_IN_RANGE_INCLUSIVE(wrapMode[1], aiTextureMapMode_Wrap, aiTextureMapMode_Decal) &&
                IS_IN_RANGE_INCLUSIVE(wrapMode[2], aiTextureMapMode_Wrap, aiTextureMapMode_Decal)) {
                texture.wrapU(aiTextureMapModeTable[wrapMode[0]]);
                texture.wrapV(aiTextureMapModeTable[wrapMode[1]]);
                texture.wrapW(aiTextureMapModeTable[wrapMode[2]]);
            }

            texture.textureName(img_name);
            texture.texturePath(img_path);
            texture.operation(texOp);
            texture.srgb(srgb);
            material._textures[to_base(usage)] = texture;
        }
    };

    // ------------------------------- Part 2: PBR or legacy material textures -------------------------------------
    { // Albedo map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) 
        {
            // The first texture operation defines how we should mix the diffuse colour with the texture itself
            loadTexture(TextureUsage::UNIT0, aiTextureOperationTable[op], tName, mode, true);
        }
    }
    { // Detail map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_BASE_COLOR, 1, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE, 1, &tName, &mapping, &uvInd, &blend, &op, mode)) 
        {
            // The second operation is how we mix the albedo generated from the diffuse and Tex0 with this texture
            loadTexture(TextureUsage::UNIT1, aiTextureOperationTable[op], tName, mode, true);
        }
    }
    { // Validation
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_BASE_COLOR, 2, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE, 2, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            Console::errorfn(Locale::Get(_ID("MATERIAL_EXTRA_DIFFUSE")), materialName.c_str());
        }
    }
    bool hasNormalMap = false;
    { // Normal map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_NORMALS, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) 
        {
            loadTexture(TextureUsage::NORMALMAP, aiTextureOperationTable[op], tName, mode);
            material.bumpMethod(BumpMethod::NORMAL);
            hasNormalMap = true;
        }
    }
    { // Height map or Displacement map. Just one here that acts as a parallax map. Height can act as a backup normalmap as well
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_HEIGHT, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (convertHeightToBumpMap && !hasNormalMap) {
                loadTexture(TextureUsage::NORMALMAP, aiTextureOperationTable[op], tName, mode);
                material.bumpMethod(BumpMethod::NORMAL);
                hasNormalMap = true;
            } else {
                loadTexture(TextureUsage::HEIGHTMAP, aiTextureOperationTable[op], tName, mode);
                material.bumpMethod(BumpMethod::PARALLAX);
            }
        }

        if (AI_SUCCESS == mat->GetTexture(aiTextureType_DISPLACEMENT, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            loadTexture(TextureUsage::HEIGHTMAP, aiTextureOperationTable[op], tName, mode);
            material.bumpMethod(BumpMethod::PARALLAX);
        }
    }
    { // Opacity map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_OPACITY, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            loadTexture(TextureUsage::OPACITY, aiTextureOperationTable[op], tName, mode);
        }
    }
    { // Specular map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_SPECULAR, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            loadTexture(TextureUsage::SPECULAR, aiTextureOperationTable[op], tName, mode);
            // Undo the spec colour and leave only the strength component in!
            material.specular({ specStrength, specStrength, specStrength, material.specular().a });
        }
    }
    { // Emissive map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_EMISSIVE, 0, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_EMISSION_COLOR, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            loadTexture(TextureUsage::EMISSIVE, aiTextureOperationTable[op], tName, mode);
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_METALNESS, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        loadTexture(TextureUsage::METALNESS, aiTextureOperationTable[op], tName, mode);
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        loadTexture(TextureUsage::ROUGHNESS, aiTextureOperationTable[op], tName, mode);
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        loadTexture(TextureUsage::OCCLUSION, aiTextureOperationTable[op], tName, mode);
    }

}

};