// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Animations/Headers/AnimationUtils.h"
#include "Geometry/Animations/Headers/Bone.h"
#include "Geometry/Importer/Headers/DVDConverter.h"
#include "Geometry/Importer/Headers/MeshImporter.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Graphs/Headers/SceneNode.h"
#include "Graphs/Headers/SceneGraphNode.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

namespace Divide
{
    TEST_CASE( "Character import normalization grounds bind and animated poses at the intended sizes", "[mesh_import][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        for ( const auto& [name, minHeight, maxHeight] : {
            std::tuple{"dwarf.x", 1.7f, 1.9f},
            std::tuple{"Bob.md5mesh", 2.5f, 2.9f},
            std::tuple{"Testwuson.X", 0.8f, 1.2f} } )
        {
            CAPTURE( name );
            XML::detail::SerializerContext settings;
            const string settingsName = Util::StringFormat( "{}.import.xml", name );
            REQUIRE( settings.read( ResourcePath{"Assets"} / "Models", settingsName.c_str(), "import" ) );
            const float3 angles{
                settings.XmlTree.get<F32>( "import.rotation.<xmlattr>.x" ),
                settings.XmlTree.get<F32>( "import.rotation.<xmlattr>.y" ),
                settings.XmlTree.get<F32>( "import.rotation.<xmlattr>.z" )
            };
            const F32 scale = settings.XmlTree.get<F32>( "import.scale" );
            Assimp::Importer importer;
            const std::filesystem::path path = std::filesystem::path{"Assets"} / "Models" / name;
            const aiScene* scene = importer.ReadFile( path.string(), 0u );
            REQUIRE( scene != nullptr );
            REQUIRE( scene->mRootNode != nullptr );
            REQUIRE( DVDConverter::NormalizeScene( *scene, angles, scale ) );
            const auto makeSkeleton = [&]( const auto& self, const aiNode* node, Bone* parent ) -> Bone*
            {
                auto* bone = new Bone{ node->mName.C_Str(), parent };
                AnimUtils::TransformMatrix( node->mTransformation, bone->_localTransform );
                for ( U32 i = 0u; i < node->mNumChildren; ++i )
                {
                    self( self, node->mChildren[i], bone );
                }
                return bone;
            };
            Bone_uptr skeleton{ makeSkeleton( makeSkeleton, scene->mRootNode, nullptr ) };
            if ( scene->mNumAnimations > 0u )
            {
                U32 animationIndex = 0u;
                while ( animationIndex < scene->mNumAnimations && scene->mAnimations[animationIndex]->mDuration == 0. )
                {
                    ++animationIndex;
                }
                REQUIRE( animationIndex < scene->mNumAnimations );
                AnimEvaluator evaluator{ scene->mAnimations[animationIndex], animationIndex };
                evaluator.evaluate( 0.05, *skeleton );
            }
            const auto globalFor = [&]( const auto& self, Bone& bone ) -> mat4<F32>
            {
                return bone.parent() == nullptr ? bone._localTransform : bone._localTransform * self( self, *bone.parent() );
            };
            aiVector3D minimum{F32_MAX, F32_MAX, F32_MAX};
            aiVector3D maximum{-F32_MAX, -F32_MAX, -F32_MAX};
            aiVector3D animatedMinimum{F32_MAX, F32_MAX, F32_MAX};
            aiVector3D animatedMaximum{-F32_MAX, -F32_MAX, -F32_MAX};
            const aiMatrix4x4 inverseRoot = aiMatrix4x4{ scene->mRootNode->mTransformation }.Inverse();
            const auto visit = [&]( const auto& self, const aiNode* node, const aiMatrix4x4& parent ) -> void
            {
                const aiMatrix4x4 global = parent * node->mTransformation;
                for ( U32 i = 0u; i < node->mNumMeshes; ++i )
                {
                    const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
                    vector<aiVector3D> animated( mesh->mNumVertices );
                    for ( U32 b = 0u; b < mesh->mNumBones; ++b )
                    {
                        const aiBone& source = *mesh->mBones[b];
                        Bone* bone = skeleton->find( _ID( source.mName.C_Str() ) );
                        REQUIRE( bone != nullptr );
                        mat4<F32> offset;
                        AnimUtils::TransformMatrix( source.mOffsetMatrix, offset );
                        aiMatrix4x4 skin;
                        AnimUtils::TransformMatrix( offset * globalFor( globalFor, *bone ), skin );
                        for ( U32 w = 0u; w < source.mNumWeights; ++w )
                        {
                            const auto& weight = source.mWeights[w];
                            REQUIRE( weight.mVertexId < animated.size() );
                            animated[weight.mVertexId] += (skin * mesh->mVertices[weight.mVertexId]) * weight.mWeight;
                        }
                    }
                    for ( U32 v = 0u; v < mesh->mNumVertices; ++v )
                    {
                        const aiVector3D position = global * mesh->mVertices[v];
                        // Scene placement replaces the imported root; mesh leaves retain their local identity.
                        const aiVector3D animatedPosition = (inverseRoot * global) *
                            (mesh->mNumBones > 0u ? animated[v] : mesh->mVertices[v]);
                        for ( U8 axis = 0u; axis < 3u; ++axis )
                        {
                            minimum[axis] = std::min( minimum[axis], position[axis] );
                            maximum[axis] = std::max( maximum[axis], position[axis] );
                            animatedMinimum[axis] = std::min( animatedMinimum[axis], animatedPosition[axis] );
                            animatedMaximum[axis] = std::max( animatedMaximum[axis], animatedPosition[axis] );
                        }
                    }
                }
                for ( U32 i = 0u; i < node->mNumChildren; ++i )
                {
                    self( self, node->mChildren[i], global );
                }
            };
            visit( visit, scene->mRootNode, aiMatrix4x4{} );
            INFO( name << " bounds " << minimum.x << "," << minimum.y << "," << minimum.z << " : "
                       << maximum.x << "," << maximum.y << "," << maximum.z );
            CHECK( minimum.y == Catch::Approx( 0.f ).margin( 0.01f ) );
            CHECK( maximum.y > minHeight );
            CHECK( maximum.y < maxHeight );
            INFO( "animated bounds " << animatedMinimum.y << " : " << animatedMaximum.y );
            CHECK( animatedMinimum.y == Catch::Approx( 0.f ).margin( 0.15f ) );
            CHECK( animatedMaximum.y > minHeight );
            CHECK( animatedMaximum.y < maxHeight );
        }
    }

    TEST_CASE( "Import axis correction is applied once and preserves authored node transforms", "[mesh_import][render_smoke]" )
    {
        aiNode root;
        root.mNumChildren = 1u;
        root.mChildren = new aiNode*[1u]{ new aiNode{} };
        root.mChildren[0]->mParent = &root;
        auto& child = root.mChildren[0]->mTransformation;
        child.a1 = 2.f;
        child.b2 = 3.f;
        child.c3 = 4.f;
        child.a4 = 5.f;
        child.b4 = 7.f;
        child.c4 = 11.f;
        aiMatrix4x4 basis;
        aiMatrix4x4::RotationX( AI_MATH_HALF_PI_F, basis );
        MeshNodeData imported;
        CHECK( DVDConverter::PopulateNodeData( &root, imported, basis ) == 1u );
        mat4<F32> expected;
        AnimUtils::TransformMatrix( basis, expected );
        CHECK( imported._transform.compare( expected, 0.0001f ) );
        REQUIRE( imported._children.size() == 1u );
        AnimUtils::TransformMatrix( child, expected );
        CHECK( imported._children[0]._transform.compare( expected, 0.0001f ) );
    }

    TEST_CASE( "Import normalization changes mesh skeleton and animation in the same basis", "[mesh_import][render_smoke]" )
    {
        aiScene scene;
        scene.mRootNode = new aiNode{};
        scene.mRootNode->mTransformation.a4 = 2.f;
        scene.mNumMeshes = 1u;
        scene.mMeshes = new aiMesh*[1u]{ new aiMesh{} };
        aiMesh& mesh = *scene.mMeshes[0];
        mesh.mNumVertices = 1u;
        mesh.mVertices = new aiVector3D[1u]{ {1.f, 2.f, 3.f} };
        mesh.mNormals = new aiVector3D[1u]{ {0.f, 0.f, 1.f} };
        mesh.mNumBones = 1u;
        mesh.mBones = new aiBone*[1u]{ new aiBone{} };
        mesh.mBones[0]->mOffsetMatrix.b4 = -4.f;
        const aiVector3D skinnedBefore = scene.mRootNode->mTransformation *
                                        (mesh.mBones[0]->mOffsetMatrix * mesh.mVertices[0]);
        scene.mNumAnimations = 1u;
        scene.mAnimations = new aiAnimation*[1u]{ new aiAnimation{} };
        aiAnimation& animation = *scene.mAnimations[0];
        animation.mNumChannels = 1u;
        animation.mChannels = new aiNodeAnim*[1u]{ new aiNodeAnim{} };
        aiNodeAnim& channel = *animation.mChannels[0];
        channel.mNumPositionKeys = channel.mNumRotationKeys = channel.mNumScalingKeys = 1u;
        channel.mPositionKeys = new aiVectorKey[1u]{ {0., {2.f, 3.f, 4.f}} };
        channel.mRotationKeys = new aiQuatKey[1u]{ {0., aiQuaternion{aiVector3D{0.f, 0.f, 1.f}, 0.5f}} };
        channel.mScalingKeys = new aiVectorKey[1u]{ {0., {-2.f, 3.f, 4.f}} };
        const aiMatrix4x4 animatedBefore{ channel.mScalingKeys[0].mValue,
                                        channel.mRotationKeys[0].mValue, channel.mPositionKeys[0].mValue };
        aiMatrix4x4 rotation;
        aiMatrix4x4::RotationX( AI_MATH_HALF_PI_F, rotation );
        aiMatrix4x4 basis = rotation;
        basis.a1 *= 2.f; basis.a2 *= 2.f; basis.a3 *= 2.f;
        basis.b1 *= 2.f; basis.b2 *= 2.f; basis.b3 *= 2.f;
        basis.c1 *= 2.f; basis.c2 *= 2.f; basis.c3 *= 2.f;
        REQUIRE( DVDConverter::NormalizeScene( scene, {90.f, 0.f, 0.f}, 2.f ) );
        const auto checkVector = []( const aiVector3D& actual, const aiVector3D& expected )
        {
            CHECK( actual.x == Catch::Approx( expected.x ).margin( 0.0001f ) );
            CHECK( actual.y == Catch::Approx( expected.y ).margin( 0.0001f ) );
            CHECK( actual.z == Catch::Approx( expected.z ).margin( 0.0001f ) );
        };
        checkVector( mesh.mVertices[0], {2.f, -6.f, 4.f} );
        checkVector( mesh.mNormals[0], {0.f, -1.f, 0.f} );
        checkVector( mesh.mAABB.mMin, mesh.mVertices[0] );
        checkVector( mesh.mAABB.mMax, mesh.mVertices[0] );
        checkVector( scene.mRootNode->mTransformation *
                     (mesh.mBones[0]->mOffsetMatrix * mesh.mVertices[0]), basis * skinnedBefore );
        const aiMatrix4x4 animatedAfter{ channel.mScalingKeys[0].mValue,
                                       channel.mRotationKeys[0].mValue, channel.mPositionKeys[0].mValue };
        checkVector( animatedAfter * (basis * aiVector3D{1.f, 2.f, 3.f}),
                     basis * (animatedBefore * aiVector3D{1.f, 2.f, 3.f}) );
    }

    TEST_CASE( "Import normalization rejects invalid settings before changing the scene", "[mesh_import][render_smoke]" )
    {
        aiScene scene;
        scene.mRootNode = new aiNode{};
        const aiMatrix4x4 original = scene.mRootNode->mTransformation;
        CHECK_FALSE( DVDConverter::NormalizeScene( scene, {45.f, 0.f, 0.f}, 1.f ) );
        CHECK_FALSE( DVDConverter::NormalizeScene( scene, {0.f, 0.f, 0.f}, 0.f ) );
        CHECK_FALSE( DVDConverter::NormalizeScene( scene, {0.f, 0.f, 0.f}, -1.f ) );
        CHECK_FALSE( DVDConverter::NormalizeScene( scene, {0.f, 0.f, 0.f}, std::numeric_limits<F32>::infinity() ) );
        CHECK( scene.mRootNode->mTransformation == original );
    }

    TEST_CASE( "Shared scene node handles keep independent resource references", "[mesh_import][vegetation][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        Application app;
        PlatformContext context{ app };
        TaskPool& loader = context.taskPool( TaskPoolType::ASSET_LOADER );
        REQUIRE( loader.init( 1u ) );
        SCOPE_EXIT
        {
            loader.shutdown();
        };
        InitConditionalWait( context );
        ResourceCache::Init( RenderAPI::None, context );
        SCOPE_EXIT
        {
            ResourceCache::Stop();
        };

        ResourceDescriptor<TransformNode> descriptor{ "shared-scene-node-test" };
        descriptor.waitForReady( true );
        Handle<TransformNode> owner = CreateResource( descriptor );
        REQUIRE( owner != INVALID_HANDLE<TransformNode> );
        const Handle<TransformNode> original = owner;
        REQUIRE( Get( original ) != nullptr );
        SceneNodeHandle first = FromSharedHandle( owner );
        SceneNodeHandle second = FromSharedHandle( owner );
        bool firstOwned = true;
        bool secondOwned = true;
        SCOPE_EXIT
        {
            if ( firstOwned )
            {
                first._deleter();
            }
            if ( secondOwned )
            {
                second._deleter();
            }
            DestroyResource( owner );
        };

        first._deleter();
        firstOwned = false;
        ResourceCache::OnFrameStart();
        CHECK( Get( original ) != nullptr );

        DestroyResource( owner );
        ResourceCache::OnFrameStart();
        CHECK( Get( original ) == second._nodePtr );

        second._deleter();
        secondOwned = false;
        ResourceCache::OnFrameStart();
        CHECK( Get( original ) == nullptr );
    }

    TEST_CASE( "Mesh roots leave geometry draws to their submeshes", "[mesh_import][render_smoke]" )
    {
        Mesh mesh{ ResourceDescriptor<Mesh>{ "mesh-root-test" } };
        GenericDrawCommandContainer commands;
        mesh.buildDrawCommands( nullptr, commands );
        CHECK( commands.empty() );

        commands.emplace_back();
        mesh.buildDrawCommands( nullptr, commands );
        CHECK( commands.size() == 1u );
    }

    TEST_CASE( "Shared skeleton bone bindings retain stable indices and reject conflicts", "[mesh_import][render_smoke]" )
    {
        using namespace DVDConverter::detail;

        Bone bone{ "shared", nullptr };
        U16 nextBoneIndex = 0u;
        mat4<F32> offset = MAT4_IDENTITY;
        offset._41 = 23.f;

        CHECK( RegisterBoneBinding( bone, offset, nextBoneIndex ) == BoneBindingStatus::REGISTERED );
        CHECK( bone._boneID == 0u );
        CHECK( nextBoneIndex == 1u );
        CHECK( bone._offsetMatrix.compare( offset, EPSILON_F32 ) );

        CHECK( RegisterBoneBinding( bone, offset, nextBoneIndex ) == BoneBindingStatus::REUSED );
        mat4<F32> roundedOffset = offset;
        roundedOffset._41 += 0.00005f;
        CHECK( RegisterBoneBinding( bone, roundedOffset, nextBoneIndex ) == BoneBindingStatus::REUSED );
        CHECK( nextBoneIndex == 1u );

        mat4<F32> conflictingOffset = offset;
        conflictingOffset._41 += 1.f;
        CHECK( RegisterBoneBinding( bone, conflictingOffset, nextBoneIndex ) == BoneBindingStatus::CONFLICT );
        CHECK( bone._boneID == 0u );
        CHECK( bone._offsetMatrix.compare( offset, EPSILON_F32 ) );
        CHECK( nextBoneIndex == 1u );

        Bone lastBone{ "last", nullptr };
        U16 lastIndex = Config::MAX_BONE_COUNT_PER_NODE - 1u;
        CHECK( RegisterBoneBinding( lastBone, offset, lastIndex ) == BoneBindingStatus::REGISTERED );
        CHECK( lastBone._boneID == Config::MAX_BONE_COUNT_PER_NODE - 1u );

        Bone excessBone{ "excess", nullptr };
        CHECK( RegisterBoneBinding( excessBone, offset, lastIndex ) == BoneBindingStatus::LIMIT_EXCEEDED );
        CHECK( excessBone._boneID == Bone::INVALID_BONE_IDX );
        CHECK( lastIndex == Config::MAX_BONE_COUNT_PER_NODE );
    }

    TEST_CASE( "WarScene MD5 submeshes reuse their shared bone bindings", "[mesh_import][render_smoke]" )
    {
        using namespace DVDConverter::detail;

        const std::filesystem::path model = std::filesystem::path{ "Assets" } / "Models" / "Bob.md5mesh";
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile( model.string(), 0u );
        INFO( importer.GetErrorString() );
        REQUIRE( scene != nullptr );
        REQUIRE( scene->mNumMeshes > 1u );

        std::vector<std::unique_ptr<Bone>> bones;
        U16 nextBoneIndex = 0u;
        U32 sharedBindings = 0u;
        for ( U32 meshIndex = 0u; meshIndex < scene->mNumMeshes; ++meshIndex )
        {
            const aiMesh* mesh = scene->mMeshes[meshIndex];
            for ( U32 boneIndex = 0u; boneIndex < mesh->mNumBones; ++boneIndex )
            {
                const aiBone* sourceBone = mesh->mBones[boneIndex];
                const U64 nameHash = _ID( sourceBone->mName.C_Str() );
                const auto existing = std::find_if( bones.begin(), bones.end(), [nameHash]( const auto& bone )
                {
                    return bone->nameHash() == nameHash;
                } );
                Bone* bone = nullptr;
                if ( existing == bones.end() )
                {
                    bones.emplace_back( std::make_unique<Bone>( sourceBone->mName.C_Str(), nullptr ) );
                    bone = bones.back().get();
                }
                else
                {
                    bone = existing->get();
                }

                mat4<F32> offset;
                AnimUtils::TransformMatrix( sourceBone->mOffsetMatrix, offset );
                const BoneBindingStatus status = RegisterBoneBinding( *bone, offset, nextBoneIndex );
                INFO( "mesh=" << meshIndex << " bone=" << sourceBone->mName.C_Str() );
                REQUIRE( ( status == BoneBindingStatus::REGISTERED || status == BoneBindingStatus::REUSED ) );
                if ( status == BoneBindingStatus::REUSED )
                {
                    ++sharedBindings;
                }
            }
        }

        CHECK( sharedBindings > 0u );
        CHECK( nextBoneIndex == bones.size() );
    }

    namespace
    {
        struct UnweldedGrid
        {
            vector<U32> _indices;
            vector<Import::SubMeshVertex> _vertices;
        };

        // Every triangle gets its own three vertices so the optimiser has to weld them back together.
        UnweldedGrid MakeUnweldedGrid( const U32 cellsPerSide )
        {
            UnweldedGrid grid{};
            const auto corner = [cellsPerSide]( const U32 x, const U32 z )
            {
                Import::SubMeshVertex vertex{};
                vertex.position.set( to_F32( x ), 0.f, to_F32( z ) );
                vertex.normal.set( 0.f, 1.f, 0.f );
                vertex.texcoord.set( to_F32( x ) / to_F32( cellsPerSide ), to_F32( z ) / to_F32( cellsPerSide ), 0.f );
                return vertex;
            };

            for ( U32 z = 0u; z < cellsPerSide; ++z )
            {
                for ( U32 x = 0u; x < cellsPerSide; ++x )
                {
                    for ( const auto& [cx, cz] : { std::pair{x, z}, std::pair{x, z + 1u}, std::pair{x + 1u, z},
                                                   std::pair{x + 1u, z}, std::pair{x, z + 1u}, std::pair{x + 1u, z + 1u} } )
                    {
                        grid._indices.push_back( to_U32( grid._vertices.size() ) );
                        grid._vertices.push_back( corner( cx, cz ) );
                    }
                }
            }

            return grid;
        }

        bool IsGridBorder( const float3& position, const U32 cellsPerSide )
        {
            const F32 edge = to_F32( cellsPerSide );
            return position.x == 0.f || position.z == 0.f || position.x == edge || position.z == edge;
        }
    }

    TEST_CASE( "Submesh optimisation welds vertices and builds a shrinking LoD chain", "[mesh_import]" )
    {
        constexpr U32 cellsPerSide = 64u;
        UnweldedGrid grid = MakeUnweldedGrid( cellsPerSide );
        const size_t sourceIndexCount = grid._indices.size();

        Import::SubMeshData subMesh{};
        DVDConverter::detail::OptimiseSubMeshGeometry( grid._indices, grid._vertices, subMesh );

        CHECK( subMesh._vertices.size() == SQUARED( cellsPerSide + 1u ) );
        REQUIRE( subMesh._lodCount == Import::MAX_LOD_LEVELS );
        CHECK( subMesh._indices[0].size() == sourceIndexCount );

        constexpr std::array<F32, Import::MAX_LOD_LEVELS> expectedRatios{ 1.f, 0.75f, 0.5f, 0.25f };
        for ( U8 lod = 1u; lod < subMesh._lodCount; ++lod )
        {
            CAPTURE( lod );
            const size_t lodIndexCount = subMesh._indices[lod].size();
            CHECK( lodIndexCount % 3u == 0u );
            CHECK( lodIndexCount <= to_size( to_F32( sourceIndexCount ) * expectedRatios[lod] ) );
            CHECK( to_F32( lodIndexCount ) <= to_F32( subMesh._indices[lod - 1u].size() ) * 0.95f );
        }
    }

    TEST_CASE( "Submesh LoDs keep open borders so neighbouring submeshes stay sealed", "[mesh_import]" )
    {
        constexpr U32 cellsPerSide = 64u;
        UnweldedGrid grid = MakeUnweldedGrid( cellsPerSide );

        Import::SubMeshData subMesh{};
        DVDConverter::detail::OptimiseSubMeshGeometry( grid._indices, grid._vertices, subMesh );
        REQUIRE( subMesh._lodCount > 1u );

        size_t borderVertexCount = 0u;
        for ( const Import::SubMeshVertex& vertex : subMesh._vertices )
        {
            borderVertexCount += IsGridBorder( vertex.position, cellsPerSide ) ? 1u : 0u;
        }
        REQUIRE( borderVertexCount == 4u * cellsPerSide );

        for ( U8 lod = 0u; lod < subMesh._lodCount; ++lod )
        {
            CAPTURE( lod );
            vector<bool> referenced( subMesh._vertices.size(), false );
            for ( const U32 index : subMesh._indices[lod] )
            {
                referenced[index] = true;
            }

            size_t referencedBorderCount = 0u;
            for ( size_t i = 0u; i < subMesh._vertices.size(); ++i )
            {
                referencedBorderCount += referenced[i] && IsGridBorder( subMesh._vertices[i].position, cellsPerSide ) ? 1u : 0u;
            }
            CHECK( referencedBorderCount == borderVertexCount );
        }
    }

    TEST_CASE( "Coarser submesh LoDs read a compact prefix of the shared vertex buffer", "[mesh_import]" )
    {
        UnweldedGrid grid = MakeUnweldedGrid( 64u );

        Import::SubMeshData subMesh{};
        DVDConverter::detail::OptimiseSubMeshGeometry( grid._indices, grid._vertices, subMesh );
        REQUIRE( subMesh._lodCount > 1u );

        for ( U8 lod = 0u; lod < subMesh._lodCount; ++lod )
        {
            CAPTURE( lod );
            const vector<U32>& indices = subMesh._indices[lod];
            vector<bool> referenced( subMesh._vertices.size(), false );
            U32 maxIndex = 0u;
            for ( const U32 index : indices )
            {
                referenced[index] = true;
                maxIndex = std::max( maxIndex, index );
            }

            const size_t uniqueCount = to_size( eastl::count( referenced.begin(), referenced.end(), true ) );
            CHECK( maxIndex + 1u == uniqueCount );
        }
    }

    TEST_CASE( "Small submeshes skip automatic LoD generation", "[mesh_import]" )
    {
        UnweldedGrid grid = MakeUnweldedGrid( 8u );

        Import::SubMeshData subMesh{};
        DVDConverter::detail::OptimiseSubMeshGeometry( grid._indices, grid._vertices, subMesh );

        CHECK( subMesh._lodCount == 1u );
        CHECK( subMesh._indices[0].size() == 8u * 8u * 6u );
        CHECK( subMesh._indices[1].empty() );
        CHECK( subMesh._vertices.size() == 9u * 9u );
    }
}
