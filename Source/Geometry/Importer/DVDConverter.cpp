

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

#include <meshoptimizer.h>

namespace Divide {

namespace {
    // Meshes with fewer than g_minIndexCountForAutoLoD indices will skip auto-LoD generation
    constexpr size_t g_minIndexCountForAutoLoD = 4096;

    struct AssimpStream final : Assimp::LogStream
    {
        void write(const char* message) override
        {
            Console::printfn("{}", message);
        }
    };

    // Select the kinds of messages you want to receive on this log stream
    constexpr U32 g_severity = Config::Build::IS_DEBUG_BUILD ? Assimp::Logger::VERBOSE : Assimp::Logger::NORMAL;

    constexpr bool g_removeLinesAndPoints = true;

    struct vertexWeight 
    {
        U8 _boneID = Bone::INVALID_BONE_IDX;
        F32 _boneWeight = 0.f;
    };

    /// Recursively creates an internal node structure matching the current scene and animation.
    Bone* CreateBoneTree(aiNode* pNode, Bone* parent)
    {
        // set the parent; in case this is the root node, it will be null
        Bone* internalNode = new Bone(pNode->mName.data, parent);

        AnimUtils::TransformMatrix(pNode->mTransformation, internalNode->_localTransform);

        // continue for all child nodes and assign the created internal nodes as our
        // children recursively call this function on all children
        for (U32 i = 0u; i < pNode->mNumChildren; ++i)
        {
            CreateBoneTree(pNode->mChildren[i], internalNode);
        }

        return internalNode;
    }
}; //namespace 

namespace DVDConverter
{
namespace detail
{
    static hashMap<U32, TextureWrap> fillTextureWrapMap()
    {
        hashMap<U32, TextureWrap> wrapMap;
        wrapMap[aiTextureMapMode_Wrap] = TextureWrap::REPEAT;
        wrapMap[aiTextureMapMode_Clamp] = TextureWrap::CLAMP_TO_EDGE;
        wrapMap[aiTextureMapMode_Mirror] = TextureWrap::MIRROR_REPEAT;
        wrapMap[aiTextureMapMode_Decal] = TextureWrap::CLAMP_TO_EDGE; //With transparent border!
        return wrapMap;
    }

    static hashMap<U32, ShadingMode> fillShadingModeMap()
    {
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

    static hashMap<U32, TextureOperation> fillTextureOperationMap()
    {
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

    NO_DESTROY static hashMap<U32, TextureWrap> aiTextureMapModeTable = fillTextureWrapMap();
    NO_DESTROY static hashMap<U32, ShadingMode> aiShadingModeInternalTable = fillShadingModeMap();
    NO_DESTROY static hashMap<U32, TextureOperation> aiTextureOperationTable = fillTextureOperationMap();
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

U32 PopulateNodeData(aiNode* node, MeshNodeData& target, const aiMatrix4x4& axisCorrectionBasis)
{
    if (node == nullptr)
    {
        return 0u;
    }

    AnimUtils::TransformMatrix(axisCorrectionBasis * node->mTransformation, target._transform);

    if (!COMPARE(target._transform.m[0][0], target._transform.m[1][1]) && 
        !COMPARE(target._transform.m[1][1], target._transform.m[2][2]))
    {
        // We have either:
        // - Non-uniform scale
        // - Different transform axis (e.g. Z-up)
        // Need to determine which is which.
        aiVector3D pScaling, pPosition;
        aiQuaternion pRotation;
        (axisCorrectionBasis * node->mTransformation).Decompose(pScaling, pRotation, pPosition);
        target._transform = mat4<F32>
        {
            float3(pPosition.x, pPosition.y, pPosition.y),
            float3(pScaling.x, pScaling.y, pScaling.y),
            quatf{ pRotation.x, pRotation.y, pRotation.z, pRotation.w }
        };
    }

    target._name = node->mName.C_Str();
    target._meshIndices.resize(node->mNumMeshes);
    for (U32 i = 0u; i < node->mNumMeshes; ++i)
    {
        target._meshIndices[i] = node->mMeshes[i];
    }

    U32 numChildren = 0u;
    target._children.resize(node->mNumChildren);
    for (U32 i = 0u; i < node->mNumChildren; ++i)
    {
        numChildren += PopulateNodeData(node->mChildren[i], target._children[i], axisCorrectionBasis);
    }

    return numChildren + node->mNumChildren;
}

bool Load(PlatformContext& context, Import::ImportData& target)
{
    const ResourcePath& filePath = target.modelPath();
    const Str<256>& fileName = target.modelName();

    Assimp::Importer importer;

    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, g_removeLinesAndPoints ? aiPrimitiveType_LINE | aiPrimitiveType_POINT : 0);
    //importer.SetPropertyInteger(AI_CONFIG_IMPORT_FBX_SEARCH_EMBEDDED_TEXTURES, 1);
    importer.SetPropertyInteger(AI_CONFIG_PP_FD_REMOVE, 1);
    importer.SetPropertyInteger(AI_CONFIG_IMPORT_TER_MAKE_UVS, 1);
    importer.SetPropertyInteger(AI_CONFIG_GLOB_MEASURE_TIME, 1);
    importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 80.0f);

    constexpr auto ppSteps = aiProcess_GlobalScale |
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
                             (Config::Build::IS_DEBUG_BUILD ? aiProcess_ValidateDataStructure : 0) |
                             aiProcess_OptimizeMeshes |
                             aiProcess_GenBoundingBoxes |
                             aiProcess_TransformUVCoords;// Preprocess UV transformations (scaling, translation ...)

    const string modelPath = (filePath / fileName).string();
    const aiScene* aiScenePointer = importer.ReadFile( modelPath.c_str(), to_U32(ppSteps) );

    if (!aiScenePointer)
    {
        Console::errorfn(LOCALE_STR("ERROR_IMPORTER_FILE"), fileName, importer.GetErrorString());
        return false;
    }

    aiMatrix4x4 axisCorrectionBasis;
    if (aiScenePointer->mMetaData)
    {
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
        upVec[to_U32(UpAxis)]         = to_F32(UpAxisSign    * UnitScaleFactor);
        forwardVec[to_U32(FrontAxis)] = to_F32(FrontAxisSign * UnitScaleFactor);
        rightVec[to_U32(CoordAxis)]   = to_F32(CoordAxisSign * UnitScaleFactor);

        axisCorrectionBasis = aiMatrix4x4(rightVec.x,   rightVec.y,   rightVec.z,   0.f,
                                          upVec.x,      upVec.y,      upVec.z,      0.f,
                                          forwardVec.x, forwardVec.y, forwardVec.z, 0.f,
                                          0.f,          0.f,          0.f,          1.f);
    }

    const GeometryFormat format = GetGeometryFormatForExtension(getExtension(fileName).substr(1).c_str());

    if (format == GeometryFormat::COUNT)
    {
        // unsupported
        return false;
    }

    if ( aiScenePointer->HasAnimations() )
    {
        target._skeleton.reset( CreateBoneTree(aiScenePointer->mRootNode, nullptr) );

        mat4<F32> out;
        U8 boneID = 0u;
        for ( U16 meshPointer = 0u; meshPointer < aiScenePointer->mNumMeshes; ++meshPointer )
        {
            const aiMesh* mesh = aiScenePointer->mMeshes[meshPointer];
            for ( U32 n = 0; n < mesh->mNumBones; ++n )
            {
                const aiBone* bone = mesh->mBones[n];

                Bone* found = target._skeleton->find( _ID(bone->mName.data) );
                if( found == nullptr )
                {
                    Console::warnfn(LOCALE_STR("MISSING_BONE_IN_SKELETON"), bone->mName.C_Str());
                }
                else if ( found->_boneID == Bone::INVALID_BONE_IDX)
                {
                    AnimUtils::TransformMatrix( bone->mOffsetMatrix, found->_offsetMatrix);
                    found->_boneID = boneID++;
                }
                else
                {
                    DIVIDE_UNEXPECTED_CALL_MSG("Duplicate bone transform found in imported mesh data!");
                }
            }
        }

        for (U32 i = 0u; i < aiScenePointer->mNumAnimations; ++i)
        {
            const aiAnimation* animation = aiScenePointer->mAnimations[i];
            if (IS_ZERO(animation->mDuration))
            {
                Console::errorfn(LOCALE_STR("LOADED_0_LENGTH_ANIMATION"), animation->mName.C_Str());
            }
            else
            {
                const auto& evaluator = target._animations.emplace_back(std::make_unique<AnimEvaluator>(animation, i));

                if (evaluator->hasScaling())
                {
                    target._useDualQuatAnimation = false;
                }

                ++target._animationCount;
            }
        }
    }


    const U32 numMeshes = aiScenePointer->mNumMeshes;
    target._subMeshData.reserve(numMeshes);

    const U32 numChildren = PopulateNodeData(aiScenePointer->mRootNode, target._nodeData, axisCorrectionBasis);
    DIVIDE_ASSERT(numChildren > 0u || !target._nodeData._meshIndices.empty(), "DVDConverter::Load: Error: Failed to find child nodes in specified file!");

    constexpr U8 maxModelNameLength = 16;
    constexpr U8 maxMeshNameLength = 64;

    string modelName = stripExtension(fileName);
    if (modelName.length() > maxModelNameLength)
    {
        modelName = modelName.substr(0, maxModelNameLength);
    }

    const ResourcePath& modelFolderName = getTopLevelFolderName(filePath);

    for (U16 n = 0u; n < numMeshes; ++n)
    {
        const aiMesh* currentMesh = aiScenePointer->mMeshes[n];
        // Skip points and lines ... for now -Ionut
        if (currentMesh->mNumVertices == 0)
        {
            continue;
        }

        const string subMeshName = currentMesh->mName.length == 0 ? Util::StringFormat("submesh_{}", n) : currentMesh->mName.C_Str();
        const string fullName = Util::StringFormat("{}_{}_{}", subMeshName, n, modelName);

        Import::SubMeshData& subMeshTemp = target._subMeshData.emplace_back();

        subMeshTemp.name(fullName.length() >= maxMeshNameLength ? fullName.substr(0, maxMeshNameLength - 1u).c_str() : fullName.c_str());

        subMeshTemp._index = n;
        subMeshTemp._boneCount = to_U8(currentMesh->mNumBones);

        detail::LoadSubMeshGeometry(currentMesh, subMeshTemp, target);

        detail::LoadSubMeshMaterial(subMeshTemp._material,
                                    aiScenePointer,
                                    modelFolderName,
                                    to_U16(currentMesh->mMaterialIndex),
                                    Str<128>(subMeshTemp.name().c_str()) + "_material",
                                    format,
                                    true);
    }

    detail::BuildGeometryBuffers(context, target);
    return true;
}

namespace detail
{

void BuildGeometryBuffers(PlatformContext& context, Import::ImportData& target)
{
    size_t indexCount = 0u, vertexCount = 0u;

    for (const Import::SubMeshData& data : target._subMeshData)
    {
        for ( U8 lod = 0u; lod < data._lodCount; ++lod )
        {
            indexCount += data._indices[lod].size();
        }
        vertexCount += data._vertices.size();
    }

    VertexBuffer::Descriptor descriptor
    {
        ._name = target.modelName(),
        ._largeIndices = vertexCount >= U16_MAX,
        ._keepCPUData = true
    };

    target._vertexBuffer = context.gfx().newVB( descriptor );
    VertexBuffer* vb = target._vertexBuffer.get();

    vb->setVertexCount(vertexCount);
    vb->reserveIndexCount(indexCount);

    U32 vertexOffset = 0u;
    for ( Import::SubMeshData& data : target._subMeshData )
    {
        const bool hasTexCoord = data._useAttribute[to_base( AttribLocation::TEXCOORD )];
        const bool hasTangent  = data._useAttribute[to_base( AttribLocation::TANGENT )];
        const bool hasBones    = data._useAttribute[to_base( AttribLocation::BONE_INDICE )] && 
                                 data._useAttribute[to_base( AttribLocation::BONE_WEIGHT )];

        for ( U8 lod = 0u; lod < data._lodCount; ++lod )
        {
            const size_t idxCount = data._indices[lod].size();

            DIVIDE_ASSERT(idxCount != 0u);

            data._triangles[lod].reserve(idxCount / 3);
            const auto& indices = data._indices[lod];
            for (size_t i = 0u; i < idxCount; i += 3u)
            {
                const uint3 triangleTemp = data._triangles[lod].emplace_back
                (
                    indices[i + 0] + vertexOffset,
                    indices[i + 1] + vertexOffset,
                    indices[i + 2] + vertexOffset
                );

                vb->addIndex(triangleTemp[0]);
                vb->addIndex(triangleTemp[1]);
                vb->addIndex(triangleTemp[2]);
            }

            data._partitionIDs[lod] = vb->partitionBuffer();
        }

        const U32 vertCount = to_U32(data._vertices.size());

        for (U32 i = 0u; i < vertCount; ++i)
        {
            const Import::SubMeshData::Vertex& vert = data._vertices[i];

            const U32 idx = i + vertexOffset;

            vb->modifyPositionValue(idx, vert.position);
            vb->modifyNormalValue(idx, vert.normal);

            if (hasTexCoord)
            {
                vb->modifyTexCoordValue(idx, vert.texcoord.xy);
            }
            if (hasTangent)
            {
                vb->modifyTangentValue(idx, vert.tangent.xyz);
            }
            if (hasBones)
            {
                vb->modifyBoneIndices(idx, vert.indices);
                vb->modifyBoneWeights(idx, vert.weights);
            }
        }

        vertexOffset += vertCount;
    } //submesh data
}

void LoadSubMeshGeometry(const aiMesh* source, Import::SubMeshData& subMeshData, Import::ImportData& target)
{
    subMeshData._maxPos = { source->mAABB.mMax.x, source->mAABB.mMax.y, source->mAABB.mMax.z };
    subMeshData._minPos = { source->mAABB.mMin.x, source->mAABB.mMin.y, source->mAABB.mMin.z };

    vector<U32> input_indices(source->mNumFaces * 3u);
    for (U32 j = 0u, k = 0u; k < source->mNumFaces; ++k)
    {
        const U32* indices = source->mFaces[k].mIndices;
        // guaranteed to be 3 thanks to aiProcess_Triangulate 
        input_indices[j++] = indices[0];
        input_indices[j++] = indices[1];
        input_indices[j++] = indices[2];
    }

    vector<Import::SubMeshData::Vertex> vertices(source->mNumVertices);
    for (U32 j = 0u; j < source->mNumVertices; ++j)
    {
        const aiVector3D position = source->mVertices[j];
        const aiVector3D normal = source->mNormals[j];

        vertices[j].position.set( position.x, position.y, position.z );
        vertices[j].normal.set( normal.x, normal.y, normal.z );
    }
    subMeshData._useAttribute[to_base( AttribLocation::POSITION )] = true;
    subMeshData._useAttribute[to_base( AttribLocation::NORMAL )] = true;

    if (source->mTextureCoords[0] != nullptr)
    {
        for (U32 j = 0u; j < source->mNumVertices; ++j)
        {
            const aiVector3D texCoord = source->mTextureCoords[0][j];
            vertices[j].texcoord.set(texCoord.x, texCoord.y, texCoord.z);
        }
        subMeshData._useAttribute[to_base( AttribLocation::TEXCOORD )] = true;
    }

    if (source->mTangents != nullptr)
    {
        for (U32 j = 0u; j < source->mNumVertices; ++j)
        {
            const aiVector3D tangent = source->mTangents[j];
            vertices[j].tangent.set( tangent.x, tangent.y, tangent.z, 1.f);
        }
        subMeshData._useAttribute[to_base( AttribLocation::TANGENT )] = true;
    }
    else
    {
        Console::d_printfn(LOCALE_STR("SUBMESH_NO_TANGENT"), subMeshData.name().c_str());
    }

    if (source->mNumBones > 0u)
    {
        if (source->mNumBones >= Config::MAX_BONE_COUNT_PER_NODE )
        {
            Console::errorfn( LOCALE_STR( "SUBMESH_TOO_MANY_BONES" ), subMeshData.name().c_str(), source->mNumBones, Config::MAX_BONE_COUNT_PER_NODE , Config::MAX_BONE_COUNT_PER_NODE );
        }

        for ( U32 boneIndex = 0u; boneIndex < source->mNumBones; ++boneIndex )
        {

            Bone* bone = target._skeleton->find(_ID(source->mBones[boneIndex]->mName.data));
            if ( bone == nullptr )
            {
                // We may have bad data that should've been caught (or warned about) at load time
                continue;
            }

            DIVIDE_ASSERT( bone->_boneID != Bone::INVALID_BONE_IDX );

            aiVertexWeight* weights = source->mBones[boneIndex]->mWeights;

            const U32 numWeights = source->mBones[boneIndex]->mNumWeights;
            for ( U32 weightIndex = 0u; weightIndex < numWeights; ++weightIndex )
            {
                const U32 vertexId = weights[weightIndex].mVertexId;
                Import::SubMeshData::Vertex& vertex = vertices[vertexId];

                DIVIDE_ASSERT(vertex.weightCount < 4u);
                {
                    vertex.weights[vertex.weightCount] = weights[weightIndex].mWeight;
                    vertex.indices[vertex.weightCount] = bone->_boneID;
                    ++vertex.weightCount;
                }
            }
        }

        subMeshData._useAttribute[to_base( AttribLocation::BONE_INDICE )] = true;
        subMeshData._useAttribute[to_base( AttribLocation::BONE_WEIGHT )] = true;
    }

    constexpr F32 kThreshold = 1.02f;   // allow up to 2% worse ACMR to get more reordering opportunities for overdraw
    constexpr D64 target_factor = 0.75; // index count reduction factor per LoD
    constexpr F32 target_error = 1e-3f; // max allowed error between lod levels

    { // Generate LoD 0 (max detail)
        auto& target_indices = subMeshData._indices[0];

        //Remap VB & IB
        vector<U32> remap(source->mNumVertices);
        const size_t vertex_count = meshopt_generateVertexRemap( remap.data(),
                                                                 input_indices.data(),
                                                                 input_indices.size(),
                                                                 vertices.data(),
                                                                 source->mNumVertices,
                                                                 sizeof( Import::SubMeshData::Vertex ) );
        const size_t index_count = input_indices.size();
        target_indices.resize( index_count );
        meshopt_remapIndexBuffer(target_indices.data(),
                                 input_indices.data(),
                                 input_indices.size(),
                                 remap.data() );

        // Don't need these anymore. Each LoD level will be calculated from the previous level's data
        input_indices.clear();

        subMeshData._vertices.resize( vertex_count );
        meshopt_remapVertexBuffer( subMeshData._vertices.data(),
                                   vertices.data(),
                                   source->mNumVertices,
                                   sizeof( Import::SubMeshData::Vertex ),
                                   remap.data() );
        vertices.clear();
        
        // Optimise VB & IB
        meshopt_optimizeVertexCache( target_indices.data(),
                                     target_indices.data(),
                                     index_count,
                                     subMeshData._vertices.size() );

        meshopt_optimizeOverdraw( target_indices.data(),
                                  target_indices.data(),
                                  index_count,
                                  &subMeshData._vertices[0].position.x,
                                  subMeshData._vertices.size(),
                                  sizeof( Import::SubMeshData::Vertex ),
                                  kThreshold );

        // vertex fetch optimization should go last as it depends on the final index order
        const size_t next_vertices = meshopt_optimizeVertexFetch( subMeshData._vertices.data(),
                                                                  target_indices.data(),
                                                                  index_count,
                                                                  subMeshData._vertices.data(),
                                                                  subMeshData._vertices.size(),
                                                                  sizeof( Import::SubMeshData::Vertex ) );
        subMeshData._vertices.resize( next_vertices );

        subMeshData._lodCount = 1u;
    }
    { // Generate LoDs [1 ... Import::MAX_LOD_LEVELS-1] data and place inside VB & IB with proper offsets
        if (subMeshData._indices[0].size() >= g_minIndexCountForAutoLoD)
        {
            F32 threshold = target_factor;

            for (U8 i = 1u; i < Import::MAX_LOD_LEVELS; ++i)
            {
                auto& target_indices = subMeshData._indices[i];
                auto& source_indices = subMeshData._indices[i - 1u];

                const size_t target_index_count = size_t(source_indices.size() * threshold) / 9;

                target_indices.resize( source_indices.size() );

                const size_t next_indices = meshopt_simplify( target_indices.data(),
                                                              source_indices.data(),
                                                              source_indices.size(),
                                                              &subMeshData._vertices[0].position.x,
                                                              subMeshData._vertices.size(),
                                                              sizeof( Import::SubMeshData::Vertex ),
                                                              target_index_count,
                                                              target_error );

                if (next_indices == source_indices.size() )
                {
                    target_indices.clear();
                    // We failed to simplify further, so no point in adding this as an LoD level
                    break;
                }
                target_indices.resize( next_indices );

                // reorder indices for overdraw, balancing overdraw and vertex cache efficiency
                meshopt_optimizeVertexCache(target_indices.data(),
                                            target_indices.data(),
                                            target_indices.size(),
                                            subMeshData._vertices.size());

                meshopt_optimizeOverdraw( target_indices.data(),
                                          target_indices.data(),
                                          target_indices.size(),
                                          &subMeshData._vertices[0].position.x,
                                          subMeshData._vertices.size(),
                                          sizeof( Import::SubMeshData::Vertex ),
                                          kThreshold );
                                          
                ++subMeshData._lodCount;
                threshold *= threshold;
            }
        }
    }
}

void LoadSubMeshMaterial(Import::MaterialData& material,
                         const aiScene* source,
                         const ResourcePath& modelDirectoryName,
                         const U16 materialIndex,
                         const Str<128>& materialName,
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
            material.shadingMode(detail::aiShadingModeInternalTable[to_U32(shadingModel)]);
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
            Console::d_printfn(LOCALE_STR("MATERIAL_NO_DIFFUSE"), materialName.c_str());
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

    { // Load specular colour
        F32 specShininess = 0.f;
        if (AI_SUCCESS == aiGetMaterialFloat(mat, AI_MATKEY_SHININESS, &specShininess)) {
            // Adjust shininess range here so that it always maps to the range [0,1000]
            if (format == GeometryFormat::_3DS ||
                format == GeometryFormat::ASE ||
                format == GeometryFormat::FBX )
            {
                specShininess *= 10.f; // percentage (0-100%)
            }
            else if (format == GeometryFormat::OBJ)
            {
                NOP(); // 0...1000.f
            }
            else if (format == GeometryFormat::DAE)
            {
                REMAP(specShininess, 0.f, 511.f, 0.f, 1000.f); // 0.f ...511.f
            }
            else
            {
                specShininess = 1.f; // not supported. 0 = gouraud shading. If this ever changes (somehow) we need to handle it.
            }

            CLAMP(specShininess, 0.f, 1000.f);
        }
        // Once the value has been remaped to 0...1000, remap it what we can handle in the engine;
        specShininess = MAP(specShininess, 0.f, 1000.f, 0.f, Material::MAX_SHININESS);

        F32 specStrength = 1.f;
        aiGetMaterialFloat(mat, AI_MATKEY_SHININESS_STRENGTH, &specStrength);

        aiColor4D specular = {1.f, 1.f, 1.f, 1.f};
        aiGetMaterialColor(mat, AI_MATKEY_COLOR_SPECULAR, &specular);

        material.specular({ specular.r * specStrength, specular.g * specStrength, specular.b * specStrength, specShininess });
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

    const auto loadTexture = [&material, &modelDirectoryName](const TextureSlot usage, TextureOperation texOp, const aiString& name, aiTextureMapMode* wrapMode, const bool srgb = false)
    {
        DIVIDE_ASSERT(name.length > 0);
        constexpr const char* g_backupImageExtensions[] =
        {
            "png", "jpg", "jpeg", "tga", "dds"
        };

        ResourcePath filePath = Paths::g_texturesLocation;
        string fileName(name.C_Str());
        const auto originalExtension = getExtension(fileName);

        bool found = fileExists(filePath / fileName);
        if (!found)
        {
            //Try backup extensions
            string fileNameStem = stripExtension(fileName);
            for (const char* ext : g_backupImageExtensions)
            {
                fileName = fileNameStem + "." + ext;

                if (fileExists(filePath / fileName))
                {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            filePath = Paths::g_texturesLocation / modelDirectoryName;
            fileName = stripExtension(fileName).append(originalExtension);

            found = fileExists(filePath / fileName);
            if (!found)
            {
                //Try backup extensions
                string fileNameStem = stripExtension(fileName);
                for (const char* ext : g_backupImageExtensions)
                {
                    fileName = fileNameStem + "." + ext;
                    if (fileExists(filePath / fileName)) 
                    {
                        found = true;
                        break;
                    }
                }
            }
        }

        // if we have a name and an extension
        if (found)
        {
            Import::TextureEntry& texture = material._textures[to_base(usage)];
            // Load the texture resource
            if (IS_IN_RANGE_INCLUSIVE(wrapMode[0], aiTextureMapMode_Wrap, aiTextureMapMode_Decal) &&
                IS_IN_RANGE_INCLUSIVE(wrapMode[1], aiTextureMapMode_Wrap, aiTextureMapMode_Decal) &&
                IS_IN_RANGE_INCLUSIVE(wrapMode[2], aiTextureMapMode_Wrap, aiTextureMapMode_Decal))
            {
                texture.wrapU(detail::aiTextureMapModeTable[wrapMode[0]]);
                texture.wrapV(detail::aiTextureMapModeTable[wrapMode[1]]);
                texture.wrapW(detail::aiTextureMapModeTable[wrapMode[2]]);
            }

            texture.textureName(fileName);
            texture.texturePath(filePath);
            texture.operation(texOp);
            texture.srgb(srgb);
            texture.useDDSCache(true);
            texture.isNormalMap(usage == TextureSlot::NORMALMAP);
            texture.alphaForTransparency(usage == TextureSlot::UNIT0 ||
                                         usage == TextureSlot::UNIT1 ||
                                         usage == TextureSlot::OPACITY);

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
                loadTexture(TextureSlot::UNIT0, detail::aiTextureOperationTable[op], tName, mode, true);
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "0");
            }
        }
    }
    { // Detail map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_BASE_COLOR, 1, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE, 1, &tName, &mapping, &uvInd, &blend, &op, mode)) 
        {
            if (tName.length > 0) {
                // The second operation is how we mix the albedo generated from the diffuse and Tex0 with this texture
                loadTexture(TextureSlot::UNIT1, detail::aiTextureOperationTable[op], tName, mode, true);
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "UNIT1");
            }
        }
    }
    { // Validation
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_BASE_COLOR, 2, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE, 2, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            Console::errorfn(LOCALE_STR("MATERIAL_EXTRA_DIFFUSE"), materialName.c_str());
        }
    }
    bool hasNormalMap = false;
    { // Normal map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_NORMALS, 0, &tName, &mapping, &uvInd, &blend, &op, mode))
        {
            if (tName.length > 0) {
                loadTexture(TextureSlot::NORMALMAP, detail::aiTextureOperationTable[op], tName, mode);
                material.bumpMethod(BumpMethod::NORMAL);
                hasNormalMap = true;
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "NORMALMAP");
            }
        }
    }
    { // Height map or Displacement map. Just one here that acts as a parallax map. Height can act as a backup normalmap as well
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_HEIGHT, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                if (convertHeightToBumpMap && !hasNormalMap) {
                    loadTexture(TextureSlot::NORMALMAP, detail::aiTextureOperationTable[op], tName, mode);
                    material.bumpMethod(BumpMethod::NORMAL);
                    hasNormalMap = true;
                } else {
                    loadTexture(TextureSlot::HEIGHTMAP, detail::aiTextureOperationTable[op], tName, mode);
                    material.bumpMethod(BumpMethod::PARALLAX);
                }
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "NORMALMAP");
            }
        }

        if (AI_SUCCESS == mat->GetTexture(aiTextureType_DISPLACEMENT, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureSlot::HEIGHTMAP, detail::aiTextureOperationTable[op], tName, mode);
                material.bumpMethod(BumpMethod::PARALLAX);
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "HEIGHTMAP");
            }
        }
    }
    { // Opacity map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_OPACITY, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureSlot::OPACITY, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "OPACITY");
            }
        }
    }
    { // Specular map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_SPECULAR, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureSlot::SPECULAR, detail::aiTextureOperationTable[op], tName, mode);
                // Undo the spec colour and leave only the strength component in!
                material.specular({ 0.f, 0.f, 0.f, material.specular().a });
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "SPECULAR");
            }
        }
    }
    { // Emissive map
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_EMISSIVE, 0, &tName, &mapping, &uvInd, &blend, &op, mode) ||
            AI_SUCCESS == mat->GetTexture(aiTextureType_EMISSION_COLOR, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureSlot::EMISSIVE, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "EMISSIVE");
            }
        }
    }
    if (AI_SUCCESS == mat->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            loadTexture(TextureSlot::METALNESS, detail::aiTextureOperationTable[op], tName, mode);
        } else {
            Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "METALLIC_ROUGHNESS");
        }
    } else {
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_METALNESS, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureSlot::METALNESS, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "METALNESS");
            }
        }
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureSlot::ROUGHNESS, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "ROUGHNESS");
            }
        }
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
            if (tName.length > 0) {
                loadTexture(TextureSlot::OCCLUSION, detail::aiTextureOperationTable[op], tName, mode);
            } else {
                Console::errorfn(LOCALE_STR("MATERIAL_NO_NAME_TEXTURE"), materialName.c_str(), "OCCLUSION");
            }
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_SHEEN, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            Console::warnfn(LOCALE_STR("MATERIAL_TEXTURE_NOT_SUPPORTED"), materialName.c_str(), "SHEEN");
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_CLEARCOAT, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            Console::warnfn(LOCALE_STR("MATERIAL_TEXTURE_NOT_SUPPORTED"), materialName.c_str(), "CLEARCOAT");
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_TRANSMISSION, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            Console::warnfn(LOCALE_STR("MATERIAL_TEXTURE_NOT_SUPPORTED"), materialName.c_str(), "TRANSMISSION");
        }
    }
    if (AI_SUCCESS == mat->GetTexture(aiTextureType_UNKNOWN, 0, &tName, &mapping, &uvInd, &blend, &op, mode)) {
        if (tName.length > 0) {
            Console::warnfn(LOCALE_STR("MATERIAL_TEXTURE_NOT_SUPPORTED"), materialName.c_str(), "UNKNOWN");
        }
    }
}
}; //namespace detail
}; //namespace DVDConverter
}; //namespace Divide
