// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/TaskPool.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Platform/Headers/ConditionalWait.h"

namespace Divide
{
    struct MaterialTestAccess
    {
        [[nodiscard]] static ShaderProgramInfo& Slot( Material& material, const RenderStagePass pass )
        {
            return material.shaderInfo( pass );
        }

    };

    namespace
    {
        class GatedResource final : public CachedResource
        {
        public:
            explicit GatedResource( const ResourceDescriptor<GatedResource>& descriptor )
                : CachedResource( descriptor, "GatedResource" )
                , _fail( descriptor.flag() )
            {
            }

            bool load( [[maybe_unused]] PlatformContext& context ) override
            {
                s_started.store( true );
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
                while ( !s_release.load() && std::chrono::steady_clock::now() < deadline )
                {
                    std::this_thread::yield();
                }
                s_timedOut.store( !s_release.load() );
                return !_fail && !s_timedOut.load();
            }

            static inline std::atomic_bool s_started{ false };
            static inline std::atomic_bool s_release{ false };
            static inline std::atomic_bool s_timedOut{ false };

        private:
            bool _fail;
        };
    }

    TEST_CASE( "Material shader readiness distinguishes pending, ready and failed states", "[materials][resources][startup]" )
    {
        for ( const ResourceState state : { ResourceState::RES_CREATED, ResourceState::RES_LOADING, ResourceState::RES_THREAD_LOADED } )
        {
            CHECK( ClassifyShaderResource( state ) == ShaderResourceStatus::PENDING );
        }
        CHECK( ClassifyShaderResource( ResourceState::RES_LOADED ) == ShaderResourceStatus::READY );
        for ( const ResourceState state : { ResourceState::RES_UNKNOWN, ResourceState::RES_THREAD_LOAD_FAILED,
                                           ResourceState::RES_LOAD_FAILED, ResourceState::RES_UNLOADING, ResourceState::COUNT } )
        {
            CHECK( ClassifyShaderResource( state ) == ShaderResourceStatus::FAILED );
        }

        ShaderProgramInfo info{};
        info._shaderCompStage = ShaderBuildStage::COMPUTED;
        CHECK( info.updateReadiness( ResourceState::RES_LOADING, 23 ) == ShaderResourceStatus::PENDING );
        CHECK( info._shaderCompStage == ShaderBuildStage::COMPUTED );
        CHECK_FALSE( info.takeReadyNotification() );
        CHECK( info.updateReadiness( ResourceState::RES_LOADED, 23 ) == ShaderResourceStatus::READY );
        CHECK( info._shaderCompStage == ShaderBuildStage::READY );
        CHECK( info._shaderKeyCache == 23 );
        CHECK( info.takeReadyNotification() );
        CHECK_FALSE( info.takeReadyNotification() );

        info._shaderCompStage = ShaderBuildStage::COMPUTED;
        CHECK( info.updateReadiness( ResourceState::RES_THREAD_LOAD_FAILED, I64_LOWEST ) == ShaderResourceStatus::FAILED );
        CHECK( info._shaderCompStage == ShaderBuildStage::FAILED );
        CHECK( info._shaderKeyCache == I64_LOWEST );
        CHECK_FALSE( info.takeReadyNotification() );
    }

    TEST_CASE( "Material shader inspection preserves the render-ready notification", "[materials][startup]" )
    {
        Material material{ ResourceDescriptor<Material>{ "readiness-test" } };
        const RenderStagePass pass{ RenderStage::DISPLAY, RenderPassType::MAIN_PASS };
        ShaderProgramInfo& info = MaterialTestAccess::Slot( material, pass );
        CHECK( material.computeAndGetProgramHandle( pass ) == INVALID_HANDLE<ShaderProgram> );
        CHECK( material.shaderPending( pass ) );
        const Handle<ShaderProgram> testHandle{ ._generation = 0u, ._index = 0u };
        info._shaderRef = testHandle;
        SCOPE_EXIT
        {
            info._shaderRef = INVALID_HANDLE<ShaderProgram>;
        };
        info._shaderCompStage = ShaderBuildStage::COMPUTED;
        REQUIRE( info.updateReadiness( ResourceState::RES_LOADED, 17 ) == ShaderResourceStatus::READY );

        CHECK( material.computeAndGetProgramHandle( pass ) == testHandle );
        bool justFinished = false;
        CHECK( material.canDraw( pass, justFinished ) );
        CHECK( justFinished );
        CHECK( material.canDraw( pass, justFinished ) );
        CHECK_FALSE( justFinished );
    }

    TEST_CASE( "Material skips a missing shader and reports failure without waiting", "[materials][startup]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        Material material{ ResourceDescriptor<Material>{ "missing-shader-test" } };
        const RenderStagePass pass{ RenderStage::DISPLAY, RenderPassType::MAIN_PASS };
        ShaderProgramInfo& info = MaterialTestAccess::Slot( material, pass );
        info._shaderCompStage = ShaderBuildStage::COMPUTED;

        bool justFinished = true;
        CHECK_FALSE( material.canDraw( pass, justFinished ) );
        CHECK_FALSE( justFinished );
        CHECK( info._shaderCompStage == ShaderBuildStage::FAILED );
        CHECK_FALSE( material.shaderPending( pass ) );
    }

    TEST_CASE( "Cloning a pending material shader requests a fresh slot", "[materials][resources][startup]" )
    {
        for ( const ShaderBuildStage stage : { ShaderBuildStage::REQUESTED, ShaderBuildStage::QUEUED, ShaderBuildStage::COMPUTED } )
        {
            ShaderProgramInfo source{};
            source._shaderCompStage = stage;
            const ShaderProgramInfo clone = source.clone();
            CHECK( clone._shaderCompStage == ShaderBuildStage::REQUESTED );
            CHECK( clone._shaderRef == INVALID_HANDLE<ShaderProgram> );
        }
    }

    TEST_CASE( "Resource snapshots survive asynchronous completion and deallocation", "[resources][threading][startup]" )
    {
        const bool fail = GENERATE( false, true );
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

        GatedResource::s_started = false;
        GatedResource::s_release = false;
        GatedResource::s_timedOut = false;
        ResourceDescriptor<GatedResource> descriptor{ fail ? "snapshot-failure" : "snapshot-success" };
        descriptor.flag( fail );
        descriptor.waitForReady( false );
        std::atomic_uint pending{ 0u };
        Handle<GatedResource> handle = CreateResource( descriptor, pending );
        SCOPE_EXIT
        {
            GatedResource::s_release = true;
            while ( pending.load() != 0u )
            {
                PlatformContextIdleCall();
                std::this_thread::yield();
            }
            DestroyResource( handle, true );
        };
        REQUIRE( handle != INVALID_HANDLE<GatedResource> );
        const Handle<GatedResource> original = handle;

        const auto waitUntil = []( const auto& predicate )
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
            while ( !predicate() && std::chrono::steady_clock::now() < deadline )
            {
                PlatformContextIdleCall();
                std::this_thread::yield();
            }
            return predicate();
        };
        REQUIRE( waitUntil( [] { return GatedResource::s_started.load(); } ) );
        CHECK( pending.load() == 1u );
        const ResourceSnapshot initial = ResourceCache::Inspect( original );
        REQUIRE( initial._state == ResourceState::RES_LOADING );
        CHECK( initial._guid != I64_LOWEST );
        CHECK( ResourceCache::Inspect( INVALID_HANDLE<GatedResource> )._state == ResourceState::RES_UNKNOWN );
        const Handle<GatedResource> outOfBounds{ ._generation = 0u, ._index = 0xFFFFFEu };
        CHECK( ResourceCache::Inspect( outOfBounds )._state == ResourceState::RES_UNKNOWN );

        std::atomic_bool stop{ false };
        std::atomic_bool sawRemoved{ false };
        std::atomic_uint samples{ 0u };
        std::atomic_uint invalidSnapshots{ 0u };
        std::thread reader( [&]
        {
            while ( !stop.load() )
            {
                const ResourceSnapshot snapshot = ResourceCache::Inspect( original );
                if ( snapshot._state == ResourceState::RES_UNKNOWN )
                {
                    sawRemoved = true;
                    if ( snapshot._guid != I64_LOWEST )
                    {
                        ++invalidSnapshots;
                    }
                }
                else if ( snapshot._guid != initial._guid )
                {
                    ++invalidSnapshots;
                }
                ++samples;
            }
        } );
        SCOPE_EXIT
        {
            stop = true;
            reader.join();
        };
        REQUIRE( waitUntil( [&] { return samples.load() > 0u; } ) );
        GatedResource::s_release = true;
        REQUIRE( waitUntil( [&] { return pending.load() == 0u; } ) );
        CHECK_FALSE( GatedResource::s_timedOut.load() );
        if ( fail )
        {
            CHECK( ResourceCache::Inspect( original )._state == ResourceState::RES_UNKNOWN );
        }
        else
        {
            CHECK( ResourceCache::Inspect( original )._state == ResourceState::RES_LOADED );
            DestroyResource( handle, true );
        }
        REQUIRE( waitUntil( [&] { return sawRemoved.load(); } ) );
        CHECK( invalidSnapshots.load() == 0u );
    }
}
