#include "stdafx.h"

#include "config.h"

#include "Headers/DVDConverter.h"
#include "Headers/MeshImporter.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Geometry/Animations/Headers/AnimationUtils.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"
#include "Geometry/Shapes/Headers/SubMesh.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/Localization.h"

#include <assimp/DefaultLogger.hpp>
#include <assimp/Importer.hpp>
#include <assimp/Logger.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/GltfMaterial.h>
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
        mat4<F32> out;
        AnimUtils::TransformMatrix(pNode->mTransformation, out);
        internalNode->localTransform(out);
        internalNode->originalLocalTransform(internalNode->localTransform());
        CalculateBoneToWorldTransform(internalNode);

        // continue for all child nodes and assign the created internal nodes as our
        // children recursively call this function on all children
        for (U32 i = 0u; i < pNode->mNumChildren; ++i) {
            internalNode->_children.push_back(CreateBoneTree(pNode->mChildren[i], internalNode));
        }

        return internalNode;
    }
}; //namespace 

namespace DVDConverter {
namespace detail {
    hashMap<U32, TextureWrap> fillTextureWrapMap() {
        hashMap<U32, TextureWrap> wrapMap;
        wrapMap[aiTextureMapMode_Wrap] = TextureWrap::REPEAT;
        wrapMap[aiTextureMapMode_Clamp] = TextureWrap::CLAMP_TO_EDGE;
        wrapMap[aiTextureMapMode_Mirror] = TextureWrap::MIRROR_REPEAT;
        wrapMap[aiTextureMapMode_Decal] = TextureWrap::CLAMP_TO_EDGE; //With transparent border!
        return wrapMap;
    }

    hashMap<U32, ShadingMode> fillShadingModeMap() {
        hashMap<U32, ShadingMode> shadingMap;
        shadingMap[aiShadingMode_PBR_BRDF] = ShadingMode::PBR_MR;
        shadingMap[aiShadingMode_Fresnel] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_NoShading] = ShadingMode::FLAT;
        //shadingMap[aiShadingMode_Unlit] = ShadingMode::FLAT; //Alias
        shadingMap[aiShadingMode_CookTorrance] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_Minnaert] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_OrenNayar] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_Toon] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_Blinn] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_Phong] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_Gouraud] = ShadingMode::BLINN_PHONG;
        shadingMap[aiShadingMode_Flat] = ShadingMode::FLAT;
        return shadingMap;
    }

    hashMap<U32, TextureOperation> fillTextureOperationMap() {
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

    hashMap<U32, TextureWrap> aiTextureMapModeTable = fillTextureWrapMap();
    hashMap<U32, ShadingMode> aiShadingModeInternalTable = fillShadingModeMap();
    hashMap<U32, TextureOperation> aiTextureOperationTable = fillTextureOperationMap();
}; //namespace detail

void OnStartup([[maybe_unused]] const PlatformContext& context)
{
    Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);
    Assimp::DefaultLogger::get()->attachStream(new AssimpStream(), g_severity);
}

void OnShutdown()
{
    Assimp::DefaultLogger::kill();
}

U32 PopulateNodeData(aiNode* node, MeshNodeData& target, const aiMatrix4x4& axisCorrectionBasis) {
    if (node == nullptr) {
        return 0u;
    }

    AnimUtils::TransformMatrix(axisCorrectionBasis * node->mTransformation, target._transform);
    if (target._transform.m[0][0] != target._transform.m[1][1] && target._transform.m[1][1] != target._transform.m[2][2]) {
        // We have either:
        // - Non-uniform scale
        // - Different transform axis (e.g. Z-up)
        // Need to determine which is which.
        aiVector3D pScaling, pPosition;
        aiQuaternion pRotation;
        (axisCorrectionBasis * node->mTransformation).Decompose(pScaling, pRotation, pPosition);
        target._transform = mat4<F32>{
            vec3<F32>(pPosition.x, pPosition.y, pPosition.y),
            vec3<F32>(pScaling.x, pScaling.y, pScaling.y),
            GetMatrix(Quaternion<F32>{ pRotation.x, pRotation.y, pRotation.z, pRotation.w })
        };
    }

    target._name = node->mName.C_Str();
    target._meshIndices.resize(node->mNumMeshes);
    for (U32 i = 0u; i < node->mNumMeshes; ++i) {
        target._meshIndices[i] = node->mMeshes[i];
    }

    U32 numChildren = 0u;
    target._children.resize(node->mNumChildren);
    for (U32 i = 0u; i < node->mNumChildren; ++i) {
        numChildren += PopulateNodeData(node->mChildren[i], target._children[i], axisCorrectionBasis);
    }

    return numChildren + node->mNumChildren;
}

bool Load(PlatformContext& context, Import::ImportData& target) {
    const ResourcePath& filePath = target.modelPath();
    const ResourcePath& fileName = target.modelName();

    Assimp::Importer importer;

    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, g_removeLinesAndPoints ? aiPrimitiveType_LINE | aiPrimitiveType_POINT : 0);
    //importer.SetPropertyInteger(AI_CONFIG_IMPORT_FBX_SEARCH_EMBEDDED_TEXTURES, 1);
    importer.SetPropertyInteger(AI_CONFIG_PP_FD_REMOVE, 1);
    importer.SetPropertyInteger(AI_CONFIG_IMPORT_TER_MAKE_UVS, 1);
    importer.SetPropertyInteger(AI_CONFIG_GLOB_MEASURE_TIME, 1);
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 80.0f);

    constexpr U32 ppSteps = aiProcess_GlobalScale |
                            aiProcess_CalcTangentSpace |
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
                            (Config::Build::IS_DEBUG_BUILD ? aiProcess_ValidateDataStructure : 0u) |
                            aiProcess_OptimizeMeshes |
                            aiProcess_GenBoundingBoxes |
                            aiProcess_TransformUVCoords;// Preprocess UV transformations (scaling, translation ...)

    const aiScene* aiScenePointer = importer.ReadFile((filePath.str() + "/" + fileName.str()).c_str(), ppSteps);

    if (!aiScenePointer) {
        Console::errorfn(Locale::Get(_ID("ERROR_IMPORTER_FILE")), fileName.c_str(), importer.GetErrorString());
        return false;
    }

    aiMatrix4x4 axisCorrectionBasis;
    if (aiScenePointer->mMetaData) {
        I32 UpAxis = 1, UpAxisSign = 1, FrontAxis = 2, FrontAxisSign = 1, CoordAxis = 0, CoordAxisSign = 1;
        D64 UnitScaleFactor = 1.0;
        aiScenePointer->mMetaData->Get<int>("UpAxis", UpAxis);
        aiScenePointer->mMetaData->Get<int>("UpAxisSign", UpAxisSign);
        aiScenePointer->mMetaData->Get<int>("FrontAxis", FrontAxis);
        aiScenePointer->mMetaData->Get<int>("FrontAxisSign", FrontAxisSign);
        aiScenePointer->mMetaData->Get<int>("CoordAxis", CoordAxis);
        aiScenePointer->mMetaData->Get<int>("CoordAxisSign", CoordAxisSign);
        aiScenePointer->mMetaData->Get<D64>("UnitScaleFactor", UnitScaleFactor);

        aiVector3D upVec, forwardVec, rightVec;
        upVec[UpAxis]         = UpAxisSign    * to_F32(UnitScaleFactor);
        forwardVec[FrontAxis] = FrontAxisSign * to_F32(UnitScaleFactor);
        rightVec[CoordAxis]   = CoordAxisSign * to_F32(UnitScaleFactor);

        axisCorrectionBasis = aiMatrix4x4(rightVec.x,   rightVec.y,   rightVec.z,   0.f,
                                          upVec.x,      upVec.y,      upVec.z,      0.f,
                                          forwardVec.x, forwardVec.y, forwardVec.z, 0.f,
                                          0.f,          0.f,          0.f,          1.f);
    }

    const GeometryFormat format = GetGeometryFormatForExtension(getExtension(fileName).c_str());

    if (format == GeometryFormat::COUNT) {
        // unsupported
        return false;
    }

    target.hasAnimations(aiScenePointer->HasAnimations());
    if (target.hasAnimations()) {
        target._skeleton = CreateBoneTree(aiScenePointer->mRootNode, nullptr);
        target._bones.reserve(to_I32(target._skeleton->hierarchyDepth()));

        for (U16 meshPointer = 0u; meshPointer < aiScenePointer->mNumMeshes; ++meshPointer) {
            const aiMesh* mesh = aiScenePointer->mMeshes[meshPointer];
            for (U32 n = 0; n < mesh->mNumBones; ++n) {
                const aiBone* bone = mesh->mBones[n];

                Bone* found = target._skeleton->find(bone->mName.data);
                if (found != nullptr) {
                    mat4<F32> out;
                    AnimUtils::TransformMatrix(bone->mOffsetMatrix, out);
                    found->offsetMatrix(out);
                    target._bones.push_back(found);
                }
            }
        }

        for (U32 i = 0u; i < aiScenePointer->mNumAnimations; i++) {
            const aiAnimation* animation = aiScenePointer->mAnimations[i];
            if (IS_ZERO(animation->mDuration)) {
                Console::errorfn(Locale::Get(_ID("LOADED_0_LENGTH_ANIMATION")), animation->mName.C_Str());
            } else {
                target._animations.push_back(MemoryManager_NEW AnimEvaluator(animation, i));
            }
        }
    }

    const U32 numMeshes = aiScenePointer->mNumMeshes;
    target._subMeshData.reserve(numMeshes);

    const U32 numChildren = PopulateNodeData(aiScenePointer->mRootNode, target._nodeData, axisCorrectionBasis);
    DIVIDE_ASSERT(numChildren > 0u || !target._nodeData._meshIndices.empty(), "DVDConverter::Load: Error: Failed to find child nodes in specified file!");

    constexpr U8 maxModelNameLength = 16;
    constexpr U8 maxMeshNameLength = 64;

    string modelName{ stripExtension(fileName).c_str() };
    if (modelName.length() > maxModelNameLength) {
        modelName = modelName.substr(0, maxModelNameLength);
    }

    for (U16 n = 0u; n < numMeshes; ++n) {
        const aiMesh* currentMesh = aiScenePointer->mMeshes[n];
        // Skip points and lines ... for now -Ionut
        if (currentMesh->mNumVertices == 0) {
            continue;
        }

        const string subMeshName = currentMesh->mName.length == 0 ? Util::StringFormat("submesh_%d", n) : currentMesh->mName.C_Str();
        const string fullName = Util::StringFormat("%s_%d_%s", subMeshName.c_str(), n, modelName);

        Import::SubMeshData subMeshTemp = {};
        subMeshTemp.name(fullName.length() >= maxMeshNameLength ? fullName.substr(0, maxMeshNameLength - 1u) : fullName);
        subMeshTemp.index(to_U32(n));
        subMeshTemp.boneCount(to_U8(currentMesh->mNumBones));
        detail::LoadSubMeshGeometry(currentMesh, subMeshTemp, target.hasAnimations());

        const string& modelFolderName = getTopLevelFolderName(filePath.c_str());
        detail::LoadSubMeshMaterial(subMeshTemp._material,
                                    aiScenePointer,
                                    modelFolderName,
                                    to_U16(currentMesh->mMaterialIndex),
                                    Str128(subMeshTemp.name()) + "_material",
                                    format,
                                    true);
                        

        target._subMeshData.push_back(subMeshTemp);
    }

    detail::BuildGeometryBuffers(context, target);
    return true;
}

namespace detail {

void BuildGeometryBuffers(PlatformContext& context, Import::ImportData& target) {
    target._vertexBuffer = context.gfx().newVB();

    VertexBuffer* vb = target._vertexBuffer.get();

    size_t indexCount = 0u, vertexCount = 0u;
    for (U8 lod = 0u; lod < Import::MAX_LOD_LEVELS; ++lod) {
        for (const Import::SubMeshData& data : target._subMeshData) {
            indexCount += data._indices[lod].size();
            vertexCount += data._vertices[lod].size();
        }
    }

    vb->useLargeIndices(vertexCount >= U16_MAX);
    vb->setVertexCount(vertexCount);
    vb->reserveIndexCount(indexCount);

    U32 previousOffset = 0u;
    for (U8 lod = 0u; lod < Import::MAX_LOD_LEVELS; ++lod) {
        U8 subMeshBoneOffset = 0;
        for (Import::SubMeshData& data : target._subMeshData) {
            const size_t idxCount = data._indices[lod].size();

            if (idxCount == 0u) {
                assert(lod > 0u);
                subMeshBoneOffset += data.boneCount();
                data._partitionIDs[lod] = data._partitionIDs[lod - 1];
                continue;
            }

            data._triangles[lod].reserve(idxCount / 3);
            const auto& indices = data._indices[lod];
            for (size_t i = 0; i < idxCount; i += 3) {
                const U32 triangleTemp[3] = {
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

            subMeshBoneOffset += data.boneCount();
            previousOffset += to_U32(vertices.size());
            data._partitionIDs[lod] = vb->partitionBuffer();
        } //submesh data
    } //lod
}

void LoadSubMeshGeometry(const aiMesh* source, Import::SubMeshData& subMeshData, const bool isAnimated) {
    vector<U32> input_indices;
    input_indices.reserve(to_size(source->mNumFaces) * 3);
    for (U32 k = 0u; k < source->mNumFaces; k++) {
        // guaranteed to be 3 thanks to aiProcess_Triangulate 
        for (U32 m = 0; m < 3; ++m) {
            input_indices.push_back(source->mFaces[k].mIndices[m]);
        }
    }

    vector<Import::SubMeshData::Vertex> vertices(source->mNumVertices);

    subMeshData.maxPos({ source->mAABB.mMax.x, source->mAABB.mMax.y, source->mAABB.mMax.z });
    subMeshData.minPos({ source->mAABB.mMin.x, source->mAABB.mMin.y, source->mAABB.mMin.z });

    const vec3<F32> worldOffset = isAnimated ? VECTOR3_ZERO : ((subMeshData.maxPos() + subMeshData.minPos()) * 0.5f);
    
    subMeshData.worldOffset(worldOffset);
    subMeshData.maxPos(subMeshData.maxPos() - worldOffset);
    subMeshData.minPos(subMeshData.minPos() - worldOffset);

    for (U32 j = 0; j < source->mNumVertices; ++j) {
        vertices[j].position.set(vec3<F32>{ source->mVertices[j].x, source->mVertices[j].y, source->mVertices[j].z } - worldOffset);
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

        vector<vector<vertexWeight> > weightsPerVertex(source->mNumVertices);
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
        vector<U32> remap(source->mNumVertices);
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

void LoadSubMeshMaterial(Import::MaterialData& material,
                         const aiScene* source,
                         const string& modelDirectoryName,
                         const U16 materialIndex,
                         const Str128& materialName,
                         const GeometryFormat format,
                         bool convertHeightToBumpMap)
{

    const aiMaterial* mat = source->mMaterials[materialIndex];

    // ------------------------------- Part 1: Material properties --------------------------------------------
    { // Load shading mode
        // The default shading model should be set to the classic SpecGloss Phong
        aiString matName;
        if (AI_SUCCESS == mat->Get(AI_MATKEY_NAME, matName)) {
            material.name(matName.C_Str());
        } else {
            material.name(materialName);
        }

        I32 shadingModel = 0, flags = 0;
        if (AI_SUCCESS == aiGetMaterialInteger(mat, AI_MATKEY_SHADING_MODEL, &shadingModel)) {
            material.shadingMode(detail::aiShadingModeInternalTable[shadingModel]);
        } else {
            material.shadingMode(ShadingMode::BLINN_PHONG);
        }
        if (material.shadingMode() == ShadingMode::PBR_MR) {
            //From Assimp:
            /** Physically-Based Rendering (PBR) shading using
            * Bidirectional scattering/reflectance distribution function (BSDF/BRDF)
            * There are multiple methods under this banner, and model files may provide
            * data for more than one PBR-BRDF method.
            * Applications should use the set of provided properties to determine which
            * of their preferred PBR rendering methods are likely to be available
            * eg:
            * - If AI_MATKEY_METALLIC_FACTOR is set, then a Metallic/Roughness is available
            * - If AI_MATKEY_GLOSSINESS_FACTOR is set, then a Specular/Glossiness is available
            * Note that some PBR methods allow layering of techniques
            */
            F32 temp = 0.f;
            if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_GLOSSINESS_FACTOR, &temp)) {
                material.shadingMode(ShadingMode::PBR_SG);
            } else {
                DIVIDE_ASSERT(AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_METALLIC_FACTOR, &temp));
            }
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

    F32 specStrength = 0.f;
    { // Load specular colour
        F32 specShininess = 0.f;
        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_SHININESS, &specShininess)) {
            // Adjust shininess range here so that it always maps to the range [0,1000]
            switch (format) {
                case GeometryFormat::_3DS:
                case GeometryFormat::ASE:
                case GeometryFormat::FBX:  specShininess *= 10.f;                         break; // percentage (0-100%)
                case GeometryFormat::OBJ:  specShininess *= 1.f;                          break; // 0...1000.f
                case GeometryFormat::DAE:  REMAP(specShininess, 0.f, 511.f, 0.f, 1000.f); break; // 511.f
                case GeometryFormat::X:    specShininess = 1000.f;                        break; //no supported. 0 = gouraud shading
            };
            CLAMP(specShininess, 0.f, 1000.f);
        }
        // Once the value has been remaped to 0...1000, remap it what we can handle in the engine;
#if 0
        specShininess = CLAMPED(specShininess, 0.f, Material::MAX_SHININESS);
#else
        specShininess = MAP(specShininess, 0.f, 1000.f, 0.f, Material::MAX_SHININESS);
#endif
        bool hasSpecStrength = false;
        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_SHININESS_STRENGTH, &specStrength)) {
            hasSpecStrength = true;
        }

        material.specular({ specStrength, specStrength, specStrength, specShininess });

        aiColor4D specular;
        if (AI_SUCCESS == aiGetMaterialColor(mat, AI_MATKEY_COLOR_SPECULAR, &specular)) {
            if (!hasSpecStrength) {
                specStrength = 1.f;
            }

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
        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_METALLIC_FACTOR, &metallic)) {
            material.metallic(metallic);
        }
        // Load roughness
        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_ROUGHNESS_FACTOR, &roughness)) {
            material.roughness(roughness);
        }
    }
    { // Load specular & glossiness
        if (material.shadingMode() == ShadingMode::PBR_SG) {
            FColour4 specularTemp;
            specularTemp.rg = {1.f, 1.f};
            aiGetMaterialFloat(mat, AI_MATKEY_SPECULAR_FACTOR, &specularTemp.r);
            aiGetMaterialFloat(mat, AI_MATKEY_GLOSSINESS_FACTOR, &specularTemp.g);
            material.specular(specularTemp);
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

    const auto loadTexture = [&material, &modelDirectoryName](const TextureUsage usage, TextureOperation texOp, const aiString& name, aiTextureMapMode* wrapMode, const bool srgb = false) {
        DIVIDE_ASSERT(name.length > 0);
        constexpr char* const g_backupImageExtensions[] = {
            "png", "jpg", "jpeg", "tga", "dds"
        };

        ResourcePath filePath(Paths::g_assetsLocation + Paths::g_texturesLocation);
        ResourcePath fileName(name.C_Str());
        const Str64 originalExtension = getExtension(fileName);

        bool found = fileExists(filePath + fileName);
        if (!found) {
            //Try backup extensions
            ResourcePath fileNameStem = stripExtension(fileName);
            for (const char* ext : g_backupImageExtensions) {
                fileName = fileNameStem + "." + ext;
                if (fileExists(filePath + fileName)) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            filePath = Paths::g_assetsLocation + Paths::g_texturesLocation + modelDirectoryName + "/";
            fileName = stripExtension(fileName) + originalExtension;
            found = fileExists(filePath + fileName);
            if (!found) {
                //Try backup extensions
                ResourcePath fileNameStem = stripExtension(fileName);
                for (const char* ext : g_backupImageExtensions) {
                    fileName = fileNameStem + "." + ext;
                    if (fileExists(filePath + fileName)) {
                        found = true;
                        break;
                    }
                }
            }
        }

        // if we have a name and an extension
        if (found) {
            Import::TextureEntry& texture = material._textures[to_base(usage)];
            // Load the texture resource
            if (IS_IN_RANGE_INCLUSIVE(wrapMode[0], aiTextureMapMode_Wrap, aiTextureMapMode_Decal) &&
                IS_IN_RANGE_INCLUSIVE(wrapMode[1], aiTextureMapMode_Wrap, aiTextureMapMode_Decal) &&
                IS_IN_RANGE_INCLUSIVE(wrapMode[2], aiTextureMapMode_Wrap, aiTextureMapMode_Decal)) {
                texture.wrapU(detail::aiTextureMapModeTable[wrapMode[0]]);
                texture.wrapV(detail::aiTextureMapModeTable[wrapMode[1]]);
                texture.wrapW(detail::aiTextureMapModeTable[wrapMode[2]]);
            }

            texture.textureName(fileName);
            texture.texturePath(filePath);
            texture.operation(texOp);
            texture.srgb(srgb);
            texture.useDDSCache(true);
            texture.isNormalMap(usage == TextureUsage::NORMALMAP);
            texture.alphaForTransparency(usage == TextureUsage::UNIT0 ||
                                         usage == TextureUsage::UNIT1 ||
                                         usage == TextureUsage::OPACITY);

            material._textures[to_base(usage)] = texture;
        }
    };

    // ------------------------------- Part 2: PBR or legacy material textures -------------------------------------
    { // Albedo map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) 
        {
            if (tName.length > 0) {
                // The first texture operation defines how we should mix the diffuse colour with the texture itself
                loadTexture(TextureUsage::UNIT0, detail::aiTextureOperationTable[op], tName, mode, true);
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "UNIT0");
            }
        }
    }
    { // Detail map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_BASE_COLOR, 1, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE, 1, &tName, &mapping, &uvInd, &blend, &op, mode)) 
        {
            if (tName.length > 0) {
                // The second operation is how we mix the albedo generated from the diffuse and Tex0 with this texture
                loadTexture(TextureUsage::UNIT1, detail::aiTextureOperationTable[op], tName, mode, true);
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "UNIT1");
            }
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
            if (tName.length > 0) {
                loadTexture(TextureUsage::NORMALMAP, detail::aiTextureOperationTable[op], tName, mode);
                material.bumpMethod(BumpMethod::NORMAL);
                hasNormalMap = true;
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "NORMALMAP");
            }
        }
    }
    { // Height map or Displacement map. Just one here that acts as a parallax map. Height can act as a backup normalmap as well
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_HEIGHT, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                if (convertHeightToBumpMap && !hasNormalMap) {
                    loadTexture(TextureUsage::NORMALMAP, detail::aiTextureOperationTable[op], tName, mode);
                    material.bumpMethod(BumpMethod::NORMAL);
                    hasNormalMap = true;
                } else {
                    loadTexture(TextureUsage::HEIGHTMAP, detail::aiTextureOperationTable[op], tName, mode);
                    material.bumpMethod(BumpMethod::PARALLAX);
                }
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "NORMALMAP");
            }
        }

        if (AI_SUCCESS == mat->GetTexture(aiTextureType_DISPLACEMENT, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureUsage::HEIGHTMAP, detail::aiTextureOperationTable[op], tName, mode);
                material.bumpMethod(BumpMethod::PARALLAX);
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "HEIGHTMAP");
            }
        }
    }
    { // Opacity map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_OPACITY, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureUsage::OPACITY, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "OPACITY");
            }
        }
    }
    { // Specular map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_SPECULAR, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureUsage::SPECULAR, detail::aiTextureOperationTable[op], tName, mode);
                // Undo the spec colour and leave only the strength component in!
                material.specular({ specStrength, specStrength, specStrength, material.specular().a });
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "SPECULAR");
            }
        }
    }
    { // Emissive map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_EMISSIVE, 0, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_EMISSION_COLOR, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureUsage::EMISSIVE, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "EMISSIVE");
            }
        }
    }
    if (AI_SUCCESS == mat->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            loadTexture(TextureUsage::METALNESS, detail::aiTextureOperationTable[op], tName, mode);
        } else {
            Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "METALLIC_ROUGHNESS");
        }
    } else {
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_METALNESS, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureUsage::METALNESS, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "METALNESS");
            }
        }
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureUsage::ROUGHNESS, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "ROUGHNESS");
            }
        }
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureUsage::OCCLUSION, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(Locale::Get(_ID("MATERIAL_NO_NAME_TEXTURE")), materialName.c_str(), "OCCLUSION");
            }
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_SHEEN, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            Console::warnfn(Locale::Get(_ID("MATERIAL_TEXTURE_NOT_SUPPORTED")), materialName.c_str(), "SHEEN");
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_CLEARCOAT, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            Console::warnfn(Locale::Get(_ID("MATERIAL_TEXTURE_NOT_SUPPORTED")), materialName.c_str(), "CLEARCOAT");
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_TRANSMISSION, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            Console::warnfn(Locale::Get(_ID("MATERIAL_TEXTURE_NOT_SUPPORTED")), materialName.c_str(), "TRANSMISSION");
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_UNKNOWN, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            Console::warnfn(Locale::Get(_ID("MATERIAL_TEXTURE_NOT_SUPPORTED")), materialName.c_str(), "UNKNOWN");
        }
    }
}
}; //namespace detail
}; //namespace DVDConverter
}; //namespace Divide