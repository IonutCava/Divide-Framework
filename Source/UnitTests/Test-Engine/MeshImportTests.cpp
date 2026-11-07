// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/TaskPool.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Animations/Headers/AnimationUtils.h"
#include "Geometry/Animations/Headers/Bone.h"
#include "Geometry/Importer/Headers/DVDConverter.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Graphs/Headers/SceneNode.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Headers/ConditionalWait.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

namespace Divide
{
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
}
