// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/WindowManager.h"
#include "Core/Headers/ErrorCodes.h"
#include "Core/Headers/RingBuffer.h"
#include "GUI/Headers/FontVertexFormat.h"
#include "Dynamics/Entities/Particles/Headers/ParticleEmitter.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glDescriptorUpdate.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkDescriptorState.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkInitializers.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkSwapChain.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkToolProperties.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkBufferImpl.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkRenderTarget.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/GLSLToSPIRV.h"
#include "Platform/Video/Shaders/Headers/SampledImageResolver.h"
#include "Platform/Video/Shaders/Headers/ShaderDataUploader.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Platform/Video/Headers/BlurPass.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXShaderData.h"
#include "Platform/Video/Headers/UniformDataSnapshots.h"
#include "Platform/Video/Headers/OcclusionCullDispatch.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/ResolvedBindingCache.h"
#include "Platform/Video/Headers/fontstash.h"
#include "Rendering/PostFX/CustomOperators/Headers/SSAOState.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/Lighting/Headers/ShadowCasterBounds.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"
#include "Rendering/RenderPass/Headers/RenderPassExecutor.h"
#include "Rendering/RenderPass/Headers/RenderQueue.h"
#include "Rendering/RenderPass/Headers/RenderBin.h"
#include "Environment/Vegetation/Headers/VegetationDescriptor.h"
#include "../../../Assets/Shaders/GLSL/Common/ssaoResampling.cmn"
#include "../../../Assets/Shaders/GLSL/Common/hizConservative.cmn"
#include "../../../Assets/Shaders/GLSL/Common/hizDispatchIndex.cmn"
#include "../../../Assets/Shaders/GLSL/Common/grassInstanceMetadata.cmn"
#include "../../../Assets/Shaders/GLSL/Common/transparentCoverage.cmn"

#include <bit>

namespace Divide
{
    namespace Attorney
    {
        class RenderingCompOITTestAccess
        {
        public:
            static RTBlendStates BlendStates()
            {
                RTBlendStates states{};
                RenderingComponent::ConfigureOITBlendStates( states );
                return states;
            }
        };
    }

    TEST_CASE( "OIT packs multiplicative revealage beside additive normals", "[render_smoke][oit]" )
    {
        const RTBlendStates states = Attorney::RenderingCompOITTestAccess::BlendStates();
        const auto& packed = states._settings[to_base( GFXDevice::ScreenTargets::NORMALS )];
        REQUIRE( packed.enabled() );
        CHECK( packed.blendSrc() == BlendProperty::ONE );
        CHECK( packed.blendDest() == BlendProperty::ONE );
        CHECK( packed.blendOp() == BlendOperation::ADD );
        CHECK( packed.blendSrcAlpha() == BlendProperty::ZERO );
        CHECK( packed.blendDestAlpha() == BlendProperty::INV_SRC_ALPHA );
        CHECK( packed.blendOpAlpha() == BlendOperation::ADD );
        CHECK_FALSE( states._settings[to_base( GFXDevice::ScreenTargets::REVEALAGE )].enabled() );

        const auto& accumulation = states._settings[to_base( GFXDevice::ScreenTargets::ACCUMULATION )];
        CHECK( accumulation.enabled() );
        CHECK( accumulation.blendSrc() == BlendProperty::ONE );
        CHECK( accumulation.blendDest() == BlendProperty::ONE );
        CHECK( accumulation.blendOp() == BlendOperation::ADD );
        const auto& modulate = states._settings[to_base( GFXDevice::ScreenTargets::MODULATE )];
        CHECK( modulate.enabled() );
        CHECK( modulate.blendSrc() == BlendProperty::ZERO );
        CHECK( modulate.blendDest() == BlendProperty::INV_SRC_COLOR );
        CHECK( modulate.blendOp() == BlendOperation::ADD );
        CHECK( modulate.blendSrcAlpha() == BlendProperty::ZERO );
        CHECK( modulate.blendDestAlpha() == BlendProperty::ONE );
        CHECK( modulate.blendOpAlpha() == BlendOperation::ADD );
    }

    TEST_CASE( "Node preparation claims are independent per component and data type", "[renderer][threading][node_preparation]" )
    {
        NodePreparationState first;
        NodePreparationState second;
        for ( const U64 epoch : { U64_ONE, U64{2u}, U64{3u}, U64_MAX } )
        {
            CHECK( first.claim( TRANSFORM_IDX, epoch ) );
            CHECK_FALSE( first.claim( TRANSFORM_IDX, epoch ) );
            CHECK( first.claim( MATERIAL_IDX, epoch ) );
            CHECK_FALSE( first.claim( MATERIAL_IDX, epoch ) );
            CHECK( second.claim( MATERIAL_IDX, epoch ) );
            CHECK( second.claim( TRANSFORM_IDX, epoch ) );
        }
    }

    TEST_CASE( "Node preparation has no visible-node index capacity or recycled-slot state", "[renderer][threading][node_preparation]" )
    {
        constexpr size_t nodeCount = Config::MAX_VISIBLE_NODES + 65u;
        auto nodes = std::make_unique<std::array<NodePreparationState, nodeCount>>();
        for ( size_t i = 0u; i < nodeCount; ++i )
        {
            REQUIRE( (*nodes)[i].claim( TRANSFORM_IDX, 1u ) );
            REQUIRE( (*nodes)[i].claim( MATERIAL_IDX, 1u ) );
        }
        for ( size_t i = 0u; i < nodeCount; ++i )
        {
            REQUIRE_FALSE( (*nodes)[i].claim( TRANSFORM_IDX, 1u ) );
            REQUIRE_FALSE( (*nodes)[i].claim( MATERIAL_IDX, 1u ) );
            REQUIRE( (*nodes)[i].claim( TRANSFORM_IDX, 2u ) );
        }

        NodePreparationState replacement;
        CHECK( replacement.claim( TRANSFORM_IDX, 2u ) );
        CHECK( replacement.claim( MATERIAL_IDX, 2u ) );
    }

    TEST_CASE( "Concurrent render views claim each node preparation exactly once per player pass", "[renderer][threading][node_preparation]" )
    {
        constexpr size_t nodeCount = 129u;
        constexpr size_t threadCount = 8u;
        std::array<NodePreparationState, nodeCount> nodes{};
        std::array<std::array<std::atomic<U32>, 2u>, nodeCount> claims{};
        for ( U64 epoch = 1u; epoch <= 4u; ++epoch )
        {
            std::atomic_size_t ready{0u};
            std::atomic_bool start{false};
            std::array<std::thread, threadCount> workers;
            for ( size_t worker = 0u; worker < threadCount; ++worker )
            {
                workers[worker] = std::thread( [&, worker]
                {
                    ready.fetch_add( 1u );
                    while ( !start.load() )
                    {
                        std::this_thread::yield();
                    }
                    for ( size_t i = 0u; i < nodeCount * 4u; ++i )
                    {
                        const size_t node = (i + worker * 17u) % nodeCount;
                        for ( const U8 dataType : { TRANSFORM_IDX, MATERIAL_IDX } )
                        {
                            if ( nodes[node].claim( dataType, epoch ) )
                            {
                                claims[node][dataType].fetch_add( 1u, std::memory_order_relaxed );
                            }
                        }
                    }
                } );
            }
            while ( ready.load() != threadCount )
            {
                std::this_thread::yield();
            }
            start.store( true );
            for ( std::thread& worker : workers )
            {
                worker.join();
            }
            for ( const auto& nodeClaims : claims )
            {
                CHECK( nodeClaims[TRANSFORM_IDX].load() == epoch );
                CHECK( nodeClaims[MATERIAL_IDX].load() == epoch );
            }
        }
    }

    TEST_CASE( "Resolved binding cache validates exact keys after direct-map collisions", "[renderer][bindings]" )
    {
        ResolvedBindingCache<U32, 4u> cache;
        U32 value = 0u;
        CHECK_FALSE( cache.Find( 0u, 0u, value ) );

        U64 firstRevision = 1u;
        U64 secondRevision = 2u;
        while ( ResolvedBindingCache<U32, 4u>::IndexFor( firstRevision, 3u ) !=
                ResolvedBindingCache<U32, 4u>::IndexFor( secondRevision, 3u ) )
        {
            ++secondRevision;
        }

        cache.Store( firstRevision, 3u, 17u );
        REQUIRE( cache.Find( firstRevision, 3u, value ) );
        CHECK( value == 17u );
        cache.Store( secondRevision, 3u, 29u );
        REQUIRE( cache.Find( secondRevision, 3u, value ) );
        CHECK( value == 29u );
        CHECK_FALSE( cache.Find( firstRevision, 3u, value ) );

        cache.Store( firstRevision, 4u, 41u );
        REQUIRE( cache.Find( firstRevision, 4u, value ) );
        CHECK( value == 41u );
        CHECK( cache.StorageBytes() >= sizeof( U32 ) * 4u );
        cache.Clear();
        CHECK_FALSE( cache.Find( firstRevision, 4u, value ) );
    }

    TEST_CASE( "Immutable resource commands compose overrides without mutating their base", "[renderer][bindings][render_smoke]" )
    {
        DescriptorSet base{};
        ImageView image{};
        image._targetType = TextureType::TEXTURE_2D_ARRAY;
        Set( AddBinding( base, 0u, ShaderStageVisibility::FRAGMENT )._data, image, SamplerDescriptor{} );
        GFX::BindShaderResourcesCommand first{};
        first._usage = DescriptorSetUsage::PER_DRAW;
        first._resources = CreateResourceBindings( base, {} );
        REQUIRE( first._resources != nullptr );
        GFX::BindShaderResourcesCommand second = first;
        image._subRange._mipLevels._offset = 2u;
        Set( AddBinding( second._set, 0u, ShaderStageVisibility::FRAGMENT )._data, image, SamplerDescriptor{} );
        Set( AddBinding( second._set, 10u, ShaderStageVisibility::FRAGMENT )._data, image, SamplerDescriptor{} );
        const DescriptorSet composed = second.effectiveBindings();
        CHECK( composed._bindingCount == 2u );
        CHECK( composed._bindings[0]._data._sampledImage._image == image );
        CHECK( first.effectiveBindings()._bindingCount == 1u );
        CHECK( first._resources->_bindings._bindings[0]._data._sampledImage._image._subRange._mipLevels._offset == 0u );
        CHECK_FALSE( first.sameBindings( second ) );
        GFX::BindShaderResourcesCommand raw{};
        raw._usage = DescriptorSetUsage::PER_DRAW;
        raw._set = composed;
        CHECK( raw.sameBindings( second ) );
        raw._usage = DescriptorSetUsage::PER_PASS;
        CHECK_FALSE( raw.sameBindings( second ) );
    }

    TEST_CASE( "Merged resource slots retain their own immutable revision", "[renderer][bindings][render_smoke]" )
    {
        DescriptorSet bindings{};
        ImageView image{};
        image._targetType = TextureType::TEXTURE_2D_ARRAY;
        Set( AddBinding( bindings, 0u, ShaderStageVisibility::FRAGMENT )._data, image, SamplerDescriptor{} );
        GFXDevice::GFXDescriptorSet merged{};
        std::weak_ptr<const ResourceBindingSet> old;
        {
            GFX::BindShaderResourcesCommand command{};
            command._usage = DescriptorSetUsage::PER_DRAW;
            command._resources = CreateResourceBindings( bindings, {} );
            old = command._resources;
            const U64 materialRevision = command._resources->_revision;
            merged.update( command );
            GFX::BindShaderResourcesCommand partial{};
            partial._usage = DescriptorSetUsage::PER_DRAW;
            image._subRange._mipLevels._offset = 1u;
            Set( AddBinding( partial._set, 10u, ShaderStageVisibility::FRAGMENT )._data, image, SamplerDescriptor{} );
            merged.update( partial );
            CHECK( merged.impl()._bindingCount == 2u );
            CHECK( merged.stableBindingRevisions()[0] == materialRevision );
            CHECK( merged.stableBindingRevisions()[10] == 0u );
            CHECK_FALSE( old.expired() );
        }
        CHECK_FALSE( old.expired() );
        merged.clear();
        CHECK( old.expired() );
    }

    TEST_CASE( "Command pools release immutable bindings while retaining reusable buffers", "[renderer][bindings][render_smoke]" )
    {
        std::weak_ptr<const ResourceBindingSet> revision;
        {
            GFX::CommandBufferPool pool{ 1u };
            const auto populate = [&]
            {
                auto handle = pool.allocateBuffer( "Retained bindings", 32u );
                auto* command = pool.get( handle )->add<GFX::BindShaderResourcesCommand>();
                command->_resources = CreateResourceBindings( DescriptorSet{}, {} );
                revision = command->_resources;
                return handle;
            };
            auto handle = populate();
            auto* buffer = pool.get( handle );
            pool.deallocateBuffer( handle );
            CHECK( revision.expired() );
            handle = populate();
            CHECK( pool.get( handle ) == buffer );
            pool.reset();
            CHECK( revision.expired() );
            handle = populate();
            CHECK( pool.get( handle ) == buffer );
            CHECK_FALSE( revision.expired() );
        }
        CHECK( revision.expired() );
    }

    TEST_CASE( "Binding diagnostics canonicalize effective tuples and stay bounded", "[renderer][bindings][render_smoke]" )
    {
        ResourceBindingMetrics metrics;
        DescriptorSet first{}, reordered{};
        ImageView image{};
        image._targetType = TextureType::TEXTURE_2D_ARRAY;
        for ( const U8 slot : { U8{ 0u }, U8{ 1u } } )
        {
            Set( AddBinding( first, slot, ShaderStageVisibility::FRAGMENT )._data, image, SamplerDescriptor{} );
        }
        reordered._bindingCount = 2u;
        reordered._bindings[0] = first._bindings[1];
        reordered._bindings[1] = first._bindings[0];
        metrics.beginFrame( false );
        metrics.observe( 1, 1u, first );
        CHECK( metrics.current()._drawSets == 0u );
        metrics.beginFrame( true );
        metrics.observe( 1, 1u, first );
        metrics.observe( 1, 1u, reordered );
        CHECK( metrics.current()._uniqueEffectiveSets == 1u );
        CHECK( metrics.current()._uniqueMaterialLayouts == 1u );
        metrics.observe( 2, 1u, first );
        CHECK( metrics.current()._uniqueEffectiveSets == 2u );
        CHECK( metrics.current()._uniqueMaterialLayouts == 2u );
        DescriptorSet collision = first;
        collision._bindings[0]._data._sampledImage._image._subRange._mipLevels._offset = 3u;
        metrics.observe( 1, 1u, collision );
        CHECK( metrics.current()._uniqueEffectiveSets == 3u );
        metrics.endFrame();
        metrics.beginFrame( true );
        CHECK( metrics.current()._storageGrowths == 0u );
        CHECK( metrics.previous()._drawSets == 4u );
        for ( size_t i = 0u; i < ResourceBindingMetrics::CAPACITY + 1u; ++i )
        {
            metrics.observe( to_I64( i ), 1u, first );
        }
        CHECK( metrics.current()._uniqueEffectiveSets == ResourceBindingMetrics::CAPACITY );
        CHECK( metrics.current()._effectiveOverflow == 1u );
        CHECK( metrics.current()._storageGrowths == 0u );
    }

    TEST_CASE( "Cube and paraboloid recording cameras are isolated across render stages", "[renderer][camera][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        Camera::InitPool();
        SCOPE_EXIT { Camera::DestroyPool(); };

        for ( const Camera::UtilityCamera type : { Camera::UtilityCamera::CUBE, Camera::UtilityCamera::DUAL_PARABOLOID } )
        {
            std::array<Camera*, to_base( RenderStage::COUNT )> cameras{};
            for ( size_t stage = 0u; stage < cameras.size(); ++stage )
            {
                cameras[stage] = Camera::GetUtilityCamera( type, static_cast<RenderStage>(stage) );
                REQUIRE( cameras[stage] != nullptr );
                CHECK( cameras[stage] != Camera::GetUtilityCamera( type ) );
                for ( size_t other = 0u; other < stage; ++other )
                {
                    CHECK( cameras[stage] != cameras[other] );
                }
                const float3 eye{ to_F32( stage ) * 10.f, 2.f, 3.f };
                cameras[stage]->setProjection( 1.f, Angle::DEGREES_F( 90.f ), float2{ 0.1f, 100.f + to_F32( stage ) } );
                cameras[stage]->lookAt( eye, eye + WORLD_Z_NEG_AXIS );
                (void)cameras[stage]->updateLookAt();
            }
            for ( size_t stage = 0u; stage < cameras.size(); ++stage )
            {
                CHECK( cameras[stage]->snapshot()._eye == float3{ to_F32( stage ) * 10.f, 2.f, 3.f } );
                CHECK( cameras[stage]->snapshot()._zPlanes.max == Catch::Approx( 100.f + to_F32( stage ) ) );
                CHECK( Camera::GetUtilityCamera( type, static_cast<RenderStage>(stage) ) == cameras[stage] );
            }
        }
    }

    TEST_CASE( "Transparent coverage rejects invisible texels without clipping soft edges", "[renderer][vegetation][oit]" )
    {
        CHECK_FALSE( TRANSPARENT_HAS_COVERAGE( 0.f ) );
        CHECK_FALSE( TRANSPARENT_HAS_COVERAGE( -0.f ) );
        CHECK_FALSE( TRANSPARENT_HAS_COVERAGE( -0.1f ) );
        for ( const F32 alpha : { 0.000001f, 0.01f, 0.05f, 0.5f, 1.f } )
        {
            CHECK( TRANSPARENT_HAS_COVERAGE( alpha ) );
            CHECK( TRANSPARENT_HAS_COVERAGE( alpha * 0.05f ) );
            CHECK_FALSE( TRANSPARENT_HAS_COVERAGE( alpha * 0.f ) );
        }
    }

    TEST_CASE( "Grass placement uses selected terrain material channels", "[renderer][vegetation]" )
    {
        const UColour4 grass = GrassPlacementColour( UColour4{ 180u, 90u, 255u, 255u }, 3u,
                                                     UColour4{ 255u, 255u, 255u, 120u }, 8u );
        CHECK( grass.r == 180u );
        CHECK( grass.g == 90u );
        CHECK( grass.b == 0u );
        CHECK( grass.a == 120u );
        const UColour4 bare = GrassPlacementColour( UColour4{ 0u, 0u, 255u, 255u }, 3u,
                                                    UColour4{ 255u, 255u, 255u, 0u }, 8u );
        CHECK( bare == UColour4{ 0u, 0u, 0u, 0u } );
        const UColour4 legacy{ 20u, 30u, 40u, 50u };
        CHECK( GrassPlacementColour( legacy, 15u, UColour4{ 0u }, 0u ) == legacy );
        CHECK( GrassPlacementColour( legacy, 0u, UColour4{ 0u, 0u, 0u, 200u }, 8u ).a == 200u );
    }

    TEST_CASE( "Compacted grass preserves LOD and original colour variation", "[renderer][vegetation]" )
    {
        for ( U32 index = 0u; index < 1000u; ++index )
        {
            for ( U32 lod = 0u; lod < 4u; ++lod )
            {
                const F32 packed = to_F32( GRASS_PACK_METADATA( lod, index ) );
                const U32 metadata = to_U32( packed );
                CHECK( GRASS_METADATA_LOD( metadata ) == lod );
                const U32 tint = GRASS_METADATA_TINT( metadata );
                CHECK( tint % 3u == index % 3u );
                CHECK( tint % 4u == index % 4u );
            }
        }
    }

    namespace Attorney
    {
        struct LightPoolTestAccess
        {
            using PointShadowProperties = LightPool::PointShadowProperties;
            using SpotShadowProperties = LightPool::SpotShadowProperties;
            using CSMShadowProperties = LightPool::CSMShadowProperties;
            using ShadowProperties = LightPool::ShadowProperties;
            using ViewLightDataList = LightPool::ViewLightDataList;

            static auto& AcquireViewLightData( ViewLightDataList& views, size_t& usedCount )
            {
                return LightPool::AcquireViewLightData( views, usedCount );
            }

            static U32 LightBufferCapacity( const size_t count )
            {
                return LightPool::LightBufferCapacity( count );
            }
        };
    }

    using Attorney::LightPoolTestAccess;

    TEST_CASE( "View light buffer capacity grows geometrically within the upload limit", "[renderer][lighting][render_smoke]" )
    {
        constexpr U32 limit = Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME;
        CHECK( LightPoolTestAccess::LightBufferCapacity( 0u ) == 32u );
        CHECK( LightPoolTestAccess::LightBufferCapacity( 32u ) == 32u );
        CHECK( LightPoolTestAccess::LightBufferCapacity( 33u ) == 64u );
        for ( size_t count = 0u; count <= limit + 1u; ++count )
        {
            const U32 capacity = LightPoolTestAccess::LightBufferCapacity( count );
            CHECK( capacity >= std::min( count, size_t{ limit } ) );
            CHECK( capacity <= limit );
            CHECK( (capacity & (capacity - 1u)) == 0u );
        }
    }

    TEST_CASE( "Light uploads retain every view in preallocated contiguous storage", "[renderer][lighting][render_smoke]" )
    {
        LightPoolTestAccess::ViewLightDataList views;
        views.resize( 66u );
        const auto* storage = views.data();
        const size_t capacity = views.capacity();
        size_t usedCount = 0u;
        auto& first = LightPoolTestAccess::AcquireViewLightData( views, usedCount );
        first._properties.emplace_back()._position = float4{ 1.f, 2.f, 3.f, 4.f };
        first._lights.push_back( nullptr );
        first._scene._globalData = uint4{ 0u, 1u, 0u, 0u };
        auto& second = LightPoolTestAccess::AcquireViewLightData( views, usedCount );
        second._properties.emplace_back()._position = float4{ -3.f, -2.f, -1.f, 4.f };
        second._lights.push_back( nullptr );
        second._scene._globalData = uint4{ 1u, 0u, 0u, 0u };
        CHECK( &first != &second );
        CHECK( usedCount == 2u );
        CHECK( &second == &first + 1u );

        for ( size_t i = 0u; i < 64u; ++i )
        {
            auto& empty = LightPoolTestAccess::AcquireViewLightData( views, usedCount );
            CHECK( empty._properties.empty() );
            CHECK( empty._scene._globalData == uint4{ 0u } );
        }
        CHECK( first._properties.front()._position == float4{ 1.f, 2.f, 3.f, 4.f } );
        CHECK( second._properties.front()._position == float4{ -3.f, -2.f, -1.f, 4.f } );
        CHECK( first._scene._globalData == uint4{ 0u, 1u, 0u, 0u } );
        CHECK( second._scene._globalData == uint4{ 1u, 0u, 0u, 0u } );
        CHECK( views.data() == storage );
        CHECK( views.capacity() == capacity );

        const size_t highWater = views.size();
        const size_t firstCapacity = first._properties.capacity();
        usedCount = 0u;
        auto& reused = LightPoolTestAccess::AcquireViewLightData( views, usedCount );
        CHECK( &reused == &first );
        reused._properties.clear();
        reused._lights.clear();
        reused._scene._globalData = uint4{ 0u };
        CHECK( reused._properties.capacity() == firstCapacity );
        CHECK( views.size() == highWater );
        CHECK( usedCount == 1u );
        CHECK( second._properties.size() == 1u );
    }

    TEST_CASE( "Shadow indices are patched independently for every view light order", "[renderer][lighting][render_smoke]" )
    {
        struct TestLight
        {
            bool _castsShadows;
            I32 _index;
            [[nodiscard]] bool castsShadows() const noexcept { return _castsShadows; }
            [[nodiscard]] I32 shadowPropertyIndex() const noexcept { return _index; }
        };
        LightPoolTestAccess::ViewLightDataList views;
        views.resize( 2u );
        size_t usedCount = 0u;
        auto& first = LightPoolTestAccess::AcquireViewLightData( views, usedCount );
        auto& second = LightPoolTestAccess::AcquireViewLightData( views, usedCount );
        first._properties.resize( 2u );
        second._properties.resize( 2u );
        TestLight caster{ true, -1 };
        TestLight unshadowed{ false, 9 };
        const std::array firstLights{ &caster, &unshadowed };
        const std::array secondLights{ &unshadowed, &caster };
        caster._index = 3;
        CHECK( PatchLightShadowIndices( first._properties, firstLights ) );
        CHECK( PatchLightShadowIndices( second._properties, secondLights ) );
        CHECK( first._properties[0]._options.y == 3 );
        CHECK( first._properties[1]._options.y == -1 );
        CHECK( second._properties[0]._options.y == -1 );
        CHECK( second._properties[1]._options.y == 3 );
        caster._index = -1;
        CHECK_FALSE( PatchLightShadowIndices( first._properties, firstLights ) );
        CHECK( first._properties[0]._options.y == -1 );
        CHECK( second._properties[1]._options.y == 3 );
        const std::array<TestLight*, 0u> noLights{};
        first._properties.clear();
        CHECK_FALSE( PatchLightShadowIndices( first._properties, noLights ) );
    }

    namespace
    {
        template<typename T>
        struct PrefixedGPURecords
        {
            U8 _prefix = 0u;
            std::array<T, 2u> _records{};
        };

        [[nodiscard]] bool IsSimdAligned( const void* ptr )
        {
            return reinterpret_cast<uintptr_t>( ptr ) % alignof(float4) == 0u;
        }

        void SetTestSampledImageFallback( DescriptorSetBindingData& data, const Reflection::ImageEntry& image )
        {
            ImageView view{};
            view._targetType = image._textureType;
            SamplerDescriptor sampler{};
            if ( image._isDepth )
            {
                sampler._depthCompareFunc = ComparisonFunction::LEQUAL;
            }
            Set( data, view, sampler );
        }
    }

    TEST_CASE( "GPU records preserve SIMD alignment and shader layout", "[renderer][gpu_data][render_smoke]" )
    {
        STATIC_CHECK( alignof(TransformData) >= alignof(float4) );
        STATIC_CHECK( alignof(NodeMaterialData) >= alignof(float4) );
        STATIC_CHECK( alignof(NodeTransformData) >= alignof(float4) );
        STATIC_CHECK( alignof(GFXShaderData::CamData) >= alignof(mat4<F32>) );
        STATIC_CHECK( sizeof(TransformData) == 3u * sizeof(float4) );
        STATIC_CHECK( sizeof(NodeMaterialData) == 5u * sizeof(float4) );
        STATIC_CHECK( sizeof(NodeMaterialData) == 80u );
        STATIC_CHECK( sizeof(NodeTransformData) == 2u * sizeof(mat4<F32>) );
        STATIC_CHECK( sizeof(NodeTransformData) == 128u );
        STATIC_CHECK( sizeof(NodeIndirectionData) == 4u * sizeof(U32) );
        STATIC_CHECK( sizeof(NodeIndirectionData) == 16u );
        STATIC_CHECK( sizeof(GFXShaderData::CamData) == 8u * sizeof(mat4<F32>) );
        STATIC_CHECK( sizeof(GFXShaderData::CamData) == 512u );
        STATIC_CHECK( offsetof(TransformData, _rotation) == 2u * sizeof(float4) );
        STATIC_CHECK( offsetof(NodeTransformData, _prevTransform) == sizeof(TransformData) );
        STATIC_CHECK( offsetof(NodeTransformData, _boundingSphere) == 2u * sizeof(TransformData) );
        STATIC_CHECK( offsetof(NodeTransformData, _data) == 2u * sizeof(TransformData) + sizeof(float4) );
        STATIC_CHECK( offsetof(NodeMaterialData, _emissiveAndParallax) == sizeof(float4) );
        STATIC_CHECK( offsetof(NodeMaterialData, _colourData) == 2u * sizeof(float4) );
        STATIC_CHECK( offsetof(NodeMaterialData, _data) == 3u * sizeof(float4) );
        STATIC_CHECK( offsetof(NodeMaterialData, _textureOperations) == 4u * sizeof(float4) );
        STATIC_CHECK( offsetof(NodeMaterialData, _textureOperations) == 64u );
        STATIC_CHECK( offsetof(NodeTransformData, _boundingSphere) == 96u );
        STATIC_CHECK( offsetof(GFXShaderData::CamData, dvd_ViewPort) == 6u * sizeof(mat4<F32>) );
        STATIC_CHECK( offsetof(GFXShaderData::CamData, dvd_ViewPort) == 384u );
        STATIC_CHECK( offsetof(GFXShaderData::CamData, dvd_clipPlanes) == 6u * sizeof(mat4<F32>) + 4u * sizeof(float4) );

        SECTION( "Prefixed node records and their arrays" )
        {
            alignas(float4) PrefixedGPURecords<NodeMaterialData> materials{};
            alignas(float4) PrefixedGPURecords<NodeTransformData> transforms{};
            for ( size_t i = 0u; i < materials._records.size(); ++i )
            {
                NodeMaterialData& material = materials._records[i];
                NodeTransformData& transform = transforms._records[i];
                REQUIRE( IsSimdAligned( &material._albedo ) );
                CHECK( IsSimdAligned( &material._emissiveAndParallax ) );
                CHECK( IsSimdAligned( &material._colourData ) );
                REQUIRE( IsSimdAligned( &transform._transform._position ) );
                CHECK( IsSimdAligned( &transform._prevTransform._rotation ) );
                CHECK( IsSimdAligned( &transform._boundingSphere ) );

                material._albedo.set( 1.f, 2.f, 3.f, 4.f );
                transform._transform._position.set( 5.f, 6.f, 7.f, 8.f );
                CHECK( material._albedo.w == 4.f );
                CHECK( transform._transform._position.w == 8.f );
            }
        }

        SECTION( "Fixed-vector overflow retains record alignment" )
        {
            fixed_vector<NodeMaterialData, 1u, true> materials{};
            fixed_vector<NodeTransformData, 1u, true> transforms{};
            materials.resize( 1u );
            transforms.resize( 1u );
            CHECK( IsSimdAligned( &materials[0]._albedo ) );
            CHECK( IsSimdAligned( &transforms[0]._transform._position ) );
            materials.resize( 2u );
            transforms.resize( 2u );
            for ( size_t i = 0u; i < materials.size(); ++i )
            {
                REQUIRE( IsSimdAligned( &materials[i]._albedo ) );
                REQUIRE( IsSimdAligned( &transforms[i]._transform._position ) );
                materials[i]._albedo.set( 1.f, 2.f, 3.f, 4.f );
                transforms[i]._transform._position.set( 5.f, 6.f, 7.f, 8.f );
                CHECK( materials[i]._albedo.w == 4.f );
                CHECK( transforms[i]._transform._position.w == 8.f );
            }
        }

        SECTION( "Prefixed camera records and their arrays" )
        {
            alignas(float4) PrefixedGPURecords<GFXShaderData::CamData> cameras{};
            for ( auto& camera : cameras._records )
            {
                CHECK( IsSimdAligned( &camera.dvd_ViewMatrix ) );
                REQUIRE( IsSimdAligned( &camera.dvd_ViewPort ) );
                CHECK( IsSimdAligned( &camera.dvd_clipPlanes[0] ) );
                camera.dvd_ViewPort.set( 1.f, 2.f, 3.f, 4.f );
                CHECK( camera.dvd_ViewPort.w == 4.f );
            }
        }
    }

    TEST_CASE( "Shadow GPU records preserve SIMD alignment and shader layout", "[renderer][gpu_data][shadows]" )
    {
        using Point = LightPoolTestAccess::PointShadowProperties;
        using Spot = LightPoolTestAccess::SpotShadowProperties;
        using CSM = LightPoolTestAccess::CSMShadowProperties;
        using Shadow = LightPoolTestAccess::ShadowProperties;

        STATIC_CHECK( sizeof(float4) == 16u );
        STATIC_CHECK( sizeof(mat4<F32>) == 64u );
        STATIC_CHECK( alignof(Point) >= alignof(float4) );
        STATIC_CHECK( alignof(Spot) >= alignof(mat4<F32>) );
        STATIC_CHECK( alignof(CSM) >= alignof(mat4<F32>) );
        STATIC_CHECK( alignof(Shadow) >= alignof(mat4<F32>) );
        STATIC_CHECK( sizeof(Point) == 2u * sizeof(float4) );
        STATIC_CHECK( offsetof(Point, _position) == sizeof(float4) );
        STATIC_CHECK( sizeof(Spot) == 2u * sizeof(float4) + sizeof(mat4<F32>) );
        STATIC_CHECK( offsetof(Spot, _vpMatrix) == 2u * sizeof(float4) );
        STATIC_CHECK( sizeof(CSM) == (1u + Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT) * sizeof(float4) +
                                     Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT * sizeof(mat4<F32>) );
        STATIC_CHECK( offsetof(CSM, _position) == sizeof(float4) );
        STATIC_CHECK( offsetof(CSM, _vpMatrix) == (1u + Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT) * sizeof(float4) );
        STATIC_CHECK( offsetof(Shadow, _cameraViewDepthRow) ==
                      Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS * sizeof(Point) +
                      Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS * sizeof(Spot) +
                      Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS * sizeof(CSM) );
        STATIC_CHECK( offsetof(Shadow, _worldAOVPMatrix) == offsetof(Shadow, _cameraViewDepthRow) + sizeof(float4) );
        STATIC_CHECK( sizeof(Shadow) == offsetof(Shadow, _worldAOVPMatrix) + sizeof(mat4<F32>) );

        alignas(float4) PrefixedGPURecords<Shadow> shadows{};
        for ( auto& shadow : shadows._records )
        {
            CHECK( IsSimdAligned( &shadow._cameraViewDepthRow ) );
            CHECK( IsSimdAligned( &shadow._worldAOVPMatrix ) );
            for ( auto& point : shadow._pointLights )
            {
                CHECK( IsSimdAligned( &point._details ) );
                REQUIRE( IsSimdAligned( &point._position ) );
                point._position.set( 1.f, 2.f, 3.f, 4.f );
                CHECK( point._position.w == 4.f );
            }
            for ( const auto& spot : shadow._spotLights )
            {
                CHECK( IsSimdAligned( &spot._details ) );
                CHECK( IsSimdAligned( &spot._position ) );
                CHECK( IsSimdAligned( &spot._vpMatrix ) );
            }
            for ( const auto& csm : shadow._dirLights )
            {
                CHECK( IsSimdAligned( &csm._details ) );
                for ( const auto& position : csm._position )
                {
                    CHECK( IsSimdAligned( &position ) );
                }
                for ( const auto& matrix : csm._vpMatrix )
                {
                    CHECK( IsSimdAligned( &matrix ) );
                }
            }
        }
    }

    TEST_CASE( "Draw command record sizes match their expected layout", "[renderer][gpu_data]" )
    {
        STATIC_CHECK( sizeof(IndirectIndexedDrawCommand) == 20u );
        STATIC_CHECK( sizeof(IndirectNonIndexedDrawCommand) == 16u );
        STATIC_CHECK( sizeof(GenericDrawCommand) == 40u );
    }

    TEST_CASE( "Hi-Z dispatch covers indirect commands within device limits", "[renderer][occlusion][render_smoke]" )
    {
        struct TestCase
        {
            U32 _count;
            U32 _maxX;
            U32 _maxY;
            bool _valid;
            U32 _x;
            U32 _y;
        };
        constexpr std::array cases{
            TestCase{ 0u, 0u, 0u, true, 0u, 0u },
            TestCase{ 1u, 0u, 1u, false, 0u, 0u },
            TestCase{ 1u, 1u, 0u, false, 0u, 0u },
            TestCase{ 1u, 1u, 1u, true, 1u, 1u },
            TestCase{ 64u, 1u, 1u, true, 1u, 1u },
            TestCase{ 65u, 1u, 1u, false, 0u, 0u },
            TestCase{ 65u, 1u, 2u, true, 1u, 2u },
            TestCase{ 269u, 3u, 2u, true, 3u, 2u },
            TestCase{ 4194240u, 65535u, 1u, true, 65535u, 1u },
            TestCase{ 4194241u, 65535u, 1u, false, 0u, 0u },
            TestCase{ 4194241u, 65535u, 2u, true, 65535u, 2u },
            TestCase{ U32_MAX, 65535u, 1024u, false, 0u, 0u },
            TestCase{ U32_MAX, 65535u, 1025u, true, 65535u, 1025u },
            TestCase{ U32_MAX, 1u, 67108864u, true, 1u, 67108864u }
        };

        for ( const TestCase& test : cases )
        {
            CAPTURE( test._count, test._maxX, test._maxY );
            OcclusionCullDispatch dispatch{ 99u, 99u };
            REQUIRE( TryPlanOcclusionCullDispatch( test._count, test._maxX, test._maxY, dispatch ) == test._valid );
            CHECK( dispatch._groupsX == test._x );
            CHECK( dispatch._groupsY == test._y );
            if ( !test._valid || test._count == 0u )
            {
                continue;
            }

            const U64 required = (to_U64( test._count ) + OCCLUSION_CULL_GROUP_SIZE - 1u) / OCCLUSION_CULL_GROUP_SIZE;
            CHECK( HIZ_CULL_GROUP_COUNT( test._count, OCCLUSION_CULL_GROUP_SIZE ) == required );
            const U64 dispatched = to_U64( dispatch._groupsX ) * dispatch._groupsY;
            CHECK( dispatched >= required );
            CHECK( dispatched - required < dispatch._groupsX );
            CHECK( dispatched <= U32_MAX );

            const std::array<U64, 3u> entities{
                0u, std::min( to_U64( dispatch._groupsX ) * OCCLUSION_CULL_GROUP_SIZE, to_U64( test._count ) - 1u ),
                to_U64( test._count ) - 1u
            };
            for ( const U64 entity : entities )
            {
                const U32 group = to_U32( entity / OCCLUSION_CULL_GROUP_SIZE );
                const U32 lane = to_U32( entity % OCCLUSION_CULL_GROUP_SIZE );
                const U32 x = group % dispatch._groupsX;
                const U32 y = group / dispatch._groupsX;
                REQUIRE( y < dispatch._groupsY );
                const U32 shaderGroup = HIZ_CULL_GROUP_INDEX( x, y, dispatch._groupsX );
                CHECK( shaderGroup == group );
                CHECK( HIZ_CULL_ENTITY_INDEX( shaderGroup, lane, OCCLUSION_CULL_GROUP_SIZE ) == entity );
            }
            const U32 lastGroup = HIZ_CULL_GROUP_INDEX( dispatch._groupsX - 1u, dispatch._groupsY - 1u, dispatch._groupsX );
            CHECK( lastGroup == dispatched - 1u );
        }

        constexpr U32 count = 269u;
        OcclusionCullDispatch dispatch{};
        REQUIRE( TryPlanOcclusionCullDispatch( count, 3u, 2u, dispatch ) );
        std::array<U8, count> visits{};
        U32 padding = 0u;
        for ( U32 y = 0u; y < dispatch._groupsY; ++y )
        {
            for ( U32 x = 0u; x < dispatch._groupsX; ++x )
            {
                const U32 group = HIZ_CULL_GROUP_INDEX( x, y, dispatch._groupsX );
                if ( group >= HIZ_CULL_GROUP_COUNT( count, OCCLUSION_CULL_GROUP_SIZE ) )
                {
                    padding += OCCLUSION_CULL_GROUP_SIZE;
                    continue;
                }
                for ( U32 lane = 0u; lane < OCCLUSION_CULL_GROUP_SIZE; ++lane )
                {
                    const U32 entity = HIZ_CULL_ENTITY_INDEX( group, lane, OCCLUSION_CULL_GROUP_SIZE );
                    if ( entity >= count )
                    {
                        ++padding;
                    }
                    else
                    {
                        ++visits[entity];
                    }
                }
            }
        }
        CHECK( padding == dispatch._groupsX * dispatch._groupsY * OCCLUSION_CULL_GROUP_SIZE - count );
        CHECK( std::ranges::all_of( visits, []( const U8 visitCount ) { return visitCount == 1u; } ) );
    }

    TEST_CASE("Sampled image fallback resolution copies only when needed", "[renderer][descriptors][sampled-images]")
    {
        DescriptorSet source{};
        DescriptorSetBinding& buffer = AddBinding(source, 1u, ShaderStageVisibility::VERTEX);
        buffer._data._type = DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC;
        buffer._data._buffer._range = { 3u, 5u };
        DescriptorSetBinding& missing = AddBinding(source, 3u, ShaderStageVisibility::FRAGMENT);
        Set(missing._data, ImageView{}, SamplerDescriptor{});

        DescriptorSet scratch{};
        Set(AddBinding(scratch, 15u, ShaderStageVisibility::FRAGMENT)._data, ImageView{}, ImageUsage::SHADER_READ);
        const DescriptorSet originalScratch = scratch;
        const DescriptorSetEntry entry{ &source, DescriptorSetUsage::PER_DRAW, false };
        vector<Reflection::ImageEntry> images{};
        Reflection::ImageEntry otherSet{};
        otherSet._bindingSet = to_base(DescriptorSetUsage::PER_FRAME);
        otherSet._bindingSlot = 3u;
        images.push_back(otherSet);
        CHECK_FALSE(ResolveSampledImages(entry, images, scratch, SetTestSampledImageFallback));
        CHECK_FALSE(scratch != originalScratch);
        CHECK(source._bindingCount == 2u);

        Reflection::ImageEntry existing{};
        existing._bindingSet = to_base(DescriptorSetUsage::PER_DRAW);
        existing._bindingSlot = 3u;
        existing._stageVisibility = to_U16(ShaderStageVisibility::FRAGMENT);
        existing._textureType = TextureType::TEXTURE_2D;
        images.push_back(existing);
        Reflection::ImageEntry omitted = existing;
        omitted._bindingSlot = 7u;
        omitted._textureType = TextureType::TEXTURE_2D_ARRAY;
        omitted._isDepth = true;
        images.push_back(omitted);

        REQUIRE(ResolveSampledImages(entry, images, scratch, SetTestSampledImageFallback));
        CHECK(source._bindingCount == 2u);
        CHECK(source._bindings[1]._data._sampledImage._image._targetType == TextureType::COUNT);
        REQUIRE(scratch._bindingCount == 3u);
        CHECK(scratch._bindings[0]._data._buffer._range == BufferRange<>{3u, 5u});
        CHECK(scratch._bindings[1]._slot == 3u);
        CHECK(scratch._bindings[1]._data._sampledImage._image._targetType == TextureType::TEXTURE_2D);
        CHECK(scratch._bindings[2]._slot == 7u);
        CHECK(scratch._bindings[2]._shaderStageVisibility == to_U16(ShaderStageVisibility::FRAGMENT));
        CHECK(scratch._bindings[2]._data._sampledImage._image._targetType == TextureType::TEXTURE_2D_ARRAY);
        CHECK(scratch._bindings[2]._data._sampledImage._sampler._depthCompareFunc == ComparisonFunction::LEQUAL);

        DescriptorSet nextSource{};
        const DescriptorSetEntry nextEntry{ &nextSource, DescriptorSetUsage::PER_DRAW, false };
        REQUIRE(ResolveSampledImages(nextEntry, images, scratch, SetTestSampledImageFallback));
        CHECK(nextSource._bindingCount == 0u);
        REQUIRE(scratch._bindingCount == 2u);
        CHECK(scratch._bindings[0]._slot == 3u);
        CHECK(scratch._bindings[1]._slot == 7u);
        CHECK(scratch._bindings[2]._data._type == DescriptorSetBindingType::COUNT);
    }

    TEST_CASE("Sampled image fallback cache tracks transitions and dirty snapshots", "[renderer][descriptors][sampled-images]")
    {
        SampledImageResolutionCache cache{};
        AddBinding(cache._resolvedSet, 2u, ShaderStageVisibility::FRAGMENT)._data._type =
            DescriptorSetBindingType::UNIFORM_BUFFER_STATIC;

        CHECK_FALSE(cache.update(false, false));
        CHECK(cache.update(true, false));
        CHECK_FALSE(cache.update(true, false));
        cache._resolvedSet._bindings[0]._slot = 3u;
        CHECK(cache.update(true, false));
        CHECK_FALSE(cache.update(true, false));

        cache._resolvedSet._bindings[15]._slot = 9u;
        CHECK(cache.update(true, false));
        cache._resolvedSet._bindings[0]._slot = 4u;
        CHECK_FALSE(cache.update(true, true));
        CHECK_FALSE(cache.update(true, false));

        CHECK_FALSE(cache.update(false, false));
        CHECK(cache.update(true, false));
        CHECK_FALSE(cache.update(true, false));
        CHECK_FALSE(cache.update(false, true));
        CHECK(cache.update(true, true));
        CHECK_FALSE(cache.update(true, false));
    }

    TEST_CASE("Render bin geometry keys order both buffer handles", "[renderer][render-bin]")
    {
        const RenderBinGeometrySortKey first{
            PoolHandle{1u, 1u, 0u},
            PoolHandle{3u, 2u, 0u}
        };
        const RenderBinGeometrySortKey same{
            PoolHandle{1u, 1u, 0u},
            PoolHandle{3u, 2u, 0u}
        };
        const RenderBinGeometrySortKey laterVertex{
            PoolHandle{2u, 1u, 0u},
            PoolHandle{1u, 1u, 0u}
        };
        const RenderBinGeometrySortKey laterIndex{
            PoolHandle{1u, 1u, 0u},
            PoolHandle{4u, 2u, 0u}
        };
        const RenderBinGeometrySortKey laterGeneration{
            PoolHandle{1u, 2u, 0u},
            PoolHandle{1u, 1u, 0u}
        };

        CHECK_FALSE(RenderBinGeometrySortKeyLess(first, same));
        CHECK_FALSE(RenderBinGeometrySortKeyLess(same, first));
        CHECK(RenderBinGeometrySortKeyLess(first, laterVertex));
        CHECK(RenderBinGeometrySortKeyLess(first, laterIndex));
        CHECK(RenderBinGeometrySortKeyLess(first, laterGeneration));
        CHECK_FALSE(RenderBinGeometrySortKeyLess(laterVertex, first));
    }

    TEST_CASE("Shadow invalidation selects layers without hiding full cache changes", "[renderer][shadow]")
    {
        CHECK(ShadowLayerUpdateMask(4u, 1u, true, false) == 1u);
        CHECK(ShadowLayerUpdateMask(4u, 0b1010u, true, false) == 0b1010u);
        CHECK(ShadowLayerUpdateMask(4u, 1u | 4u, true, false) == 5u);
        CHECK(ShadowLayerUpdateMask(4u, 0u, true, false) == 0u);
        CHECK(ShadowLayerUpdateMask(4u, 1u, false, false) == 15u);
        CHECK(ShadowLayerUpdateMask(4u, 1u, true, true) == 15u);
        CHECK(ShadowLayerUpdateMask(4u, 0u, false, false) == 15u);
        CHECK(ShadowLayerUpdateMask(4u, 0b10000u, true, false) == 0u);
        CHECK(ShadowLayerUpdateMask(1u, 1u, true, false) == 1u);
        CHECK(ShadowLayerUpdateMask(0u, U32_MAX, false, true) == 0u);
        CHECK(ShadowLayerMask(32u) == U32_MAX);
        CHECK(ShadowLayerSelected(U32_MAX, 31u));
        CHECK_FALSE(ShadowLayerSelected(U32_MAX, 32u));
    }

    TEST_CASE("Shadow refresh plans separate static rebuilds from dynamic recomposition", "[renderer][shadow][render_smoke]")
    {
        const auto check = [](const U8 layers, const U32 dirtyLayers, const bool cacheValid,
                              const bool staticDirty, const bool dynamicDirty,
                              const U32 expectedStatic, const U32 expectedDynamic)
        {
            const ShadowRefreshPlan plan = BuildShadowRefreshPlan(layers, dirtyLayers, cacheValid, staticDirty, dynamicDirty);
            CHECK(plan._staticMask == expectedStatic);
            CHECK(plan._dynamicMask == expectedDynamic);
        };
        check(3u, 0u, true, false, false, 0u, 0u);
        check(3u, 0u, true, false, true, 0u, 7u);
        check(3u, 0u, true, true, false, 7u, 7u);
        check(3u, 0u, true, true, true, 7u, 7u);
        check(3u, 0u, false, false, false, 7u, 7u);
        check(3u, 1u, true, false, false, 1u, 1u);
        check(3u, 1u, true, false, true, 1u, 7u);
        check(3u, 8u, true, false, false, 0u, 0u);
        check(1u, 0u, true, false, true, 0u, 1u);
        check(1u, 0u, false, false, false, 1u, 1u);
        check(0u, U32_MAX, false, true, true, 0u, 0u);
        check(32u, 1u << 31u, true, false, false, 1u << 31u, 1u << 31u);
        check(32u, 0u, false, false, false, U32_MAX, U32_MAX);
    }

    TEST_CASE("Shadow static caches require matching ownership projection and coverage", "[renderer][shadow][render_smoke]")
    {
        const ShadowStaticCacheKey key
        {
            ._owner = 42,
            ._coverageRevision = 3u,
            ._renderMask = 7u,
            ._resolution = {2048u, 2048u},
            ._visibilityDistances = {1000.f, 500.f},
            ._samples = 2u,
            ._splitCount = 3u
        };
        auto projectionKey = key;
        projectionKey._viewProjection[0] = 1.f;
        ShadowStaticCacheState projectionCache{};
        projectionCache.complete(projectionKey, false);
        projectionKey._viewProjection[0] = std::nextafter(1.f, 2.f);
        CHECK(projectionCache.needsRefresh(projectionKey, false));

        ShadowStaticCacheState cache{};
        CHECK(cache.needsRefresh(key, false));
        cache.complete(key, false);
        CHECK_FALSE(cache.needsRefresh(key, false));
        CHECK(cache.needsRefresh(key, true));

        for (size_t field = 0u; field < 10u; ++field)
        {
            ShadowStaticCacheKey changed = key;
            switch (field)
            {
                case 0u: ++changed._owner; break;
                case 1u: changed._viewProjection[12] = 1.f; break;
                case 2u: ++changed._coverageRevision; break;
                case 3u: changed._renderMask ^= 1u; break;
                case 4u: ++changed._resolution[0]; break;
                case 5u: ++changed._resolution[1]; break;
                case 6u: changed._samples = 0u; break;
                case 7u: ++changed._splitCount; break;
                case 8u: changed._visibilityDistances[1] += 1.f; break;
                case 9u: changed._rangeCameraEye[0] += 1.f; break;
            }
            CAPTURE(field);
            CHECK(cache.needsRefresh(changed, false));
        }
        cache.complete(key, true);
        CHECK(cache.needsRefresh(key, false));
        cache.complete(key, false);
        CHECK_FALSE(cache.needsRefresh(key, false));
        cache._valid = false;
        CHECK(cache.needsRefresh(key, false));
    }

    TEST_CASE("CSM cached bounds preserve static and dynamic scene-fit coverage", "[renderer][shadow][render_smoke]")
    {
        ShadowCasterBounds staticBounds{};
        staticBounds.add(BoundingBox{float3{-10.f, -5.f, -7.f}, float3{1.f, 2.f, 3.f}});
        ShadowCasterBounds feedback{};
        feedback.add(ShadowCasterBounds{});
        CHECK_FALSE(feedback._valid);
        feedback.add(staticBounds);
        CHECK(feedback._valid);
        CHECK(feedback._bounds._min == staticBounds._bounds._min);
        feedback.reset();
        feedback.add(BoundingBox{float3{-2.f}, float3{20.f, 30.f, 40.f}});
        feedback.add(staticBounds);
        CHECK(feedback._bounds._min == float3{-10.f, -5.f, -7.f});
        CHECK(feedback._bounds._max == float3{20.f, 30.f, 40.f});
    }

    TEST_CASE("Shadow cache restore commands retain separate colour and depth layers", "[renderer][shadow][render_smoke]")
    {
        const auto floatMoments = GLUtil::InternalFormatAndDataType(
            GFXImageFormat::RG, GFXDataFormat::FLOAT_32, GFXImagePacking::UNNORMALIZED);
        const auto defaultPacking = GLUtil::InternalFormatAndDataType(
            GFXImageFormat::RG, GFXDataFormat::FLOAT_32, TextureDescriptor{}._packing);
        CHECK(floatMoments._format == defaultPacking._format);
        CHECK_FALSE(RTBlitEntry{}._copySamples);
        GFX::CommandBuffer source;
        ShadowMap::restoreStaticBase(INVALID_RENDER_TARGET_ID, INVALID_RENDER_TARGET_ID, 3u, source);
        GFX::CommandBuffer commands;
        commands.add(source, 0u, 1u);
        source.clear();
        REQUIRE(commands.commands().size() == 1u);
        const auto& recorded = *commands.commands().front()->As<GFX::BlitRenderTargetCommand>();
        REQUIRE(recorded._params.size() == 2u);
        CHECK(IsValid(recorded._params));
        for (const auto& entry : recorded._params)
        {
            CHECK(entry._copySamples);
            CHECK(entry._input._layerOffset == 3u);
            CHECK(entry._output._layerOffset == 0u);
            CHECK(entry._input._index == entry._output._index);
            CHECK(entry._layerCount == 1u);
        }
        CHECK(recorded._params[1]._input._index == RT_DEPTH_ATTACHMENT_IDX);
    }

    TEST_CASE("Spotlight static bases survive dynamic refresh and retry pending shaders", "[renderer][shadow][spot-cache][render_smoke]")
    {
        std::array<ShadowStaticCacheState, 2> layers{};
        const ShadowStaticCacheKey key{._owner = 42, ._samples = 0u, ._splitCount = 1u};
        const auto initial = BuildShadowRefreshPlan(1u, 0u, false, false, false);
        CHECK(layers[0].needsRefresh(key, ShadowLayerSelected(initial._staticMask, 0u)));
        layers[0].complete(key, false);
        const auto animated = BuildShadowRefreshPlan(1u, 0u, true, false, true);
        CHECK(animated._dynamicMask == 1u);
        CHECK_FALSE(layers[0].needsRefresh(key, ShadowLayerSelected(animated._staticMask, 0u)));
        CHECK(layers[1].needsRefresh(key, false));

        const auto wind = BuildShadowRefreshPlan(1u, 1u, true, false, true);
        CHECK(layers[0].needsRefresh(key, ShadowLayerSelected(wind._staticMask, 0u)));
        layers[0].complete(key, true);
        CHECK(layers[0].needsRefresh(key, false));
        layers[0].complete(key, false);
        CHECK_FALSE(layers[0].needsRefresh(key, false));

        auto reassigned = key;
        ++reassigned._owner;
        CHECK(layers[0].needsRefresh(reassigned, false));
        auto movedLight = key;
        movedLight._viewProjection[12] = 2.f;
        CHECK(layers[0].needsRefresh(movedLight, false));
        auto movedCamera = key;
        movedCamera._rangeCameraEye[2] = 50.f;
        CHECK(layers[0].needsRefresh(movedCamera, false));
        auto multisampled = key;
        multisampled._samples = 2u;
        CHECK(layers[0].needsRefresh(multisampled, false));
    }

    TEST_CASE("Shadow refresh fallback preserves full-render layer selection", "[renderer][shadow][render_smoke]")
    {
        for (const U8 layers : {U8{0u}, U8{1u}, U8{3u}, U8{4u}, U8{32u}})
        {
            for (const U32 dirtyLayers : {0u, 1u, 5u, 8u, U32_MAX})
            {
                for (const bool cacheValid : {false, true})
                {
                    for (const bool staticDirty : {false, true})
                    {
                        for (const bool dynamicDirty : {false, true})
                        {
                            CAPTURE(layers, dirtyLayers, cacheValid, staticDirty, dynamicDirty);
                            const ShadowRefreshPlan plan = BuildShadowRefreshPlan(
                                layers, dirtyLayers, cacheValid, staticDirty, dynamicDirty);
                            CHECK(plan.updateMask() == ShadowLayerUpdateMask(
                                layers, dirtyLayers, cacheValid, staticDirty || dynamicDirty));
                            CHECK((plan._staticMask & ~plan._dynamicMask) == 0u);
                        }
                    }
                }
            }
        }
    }

    TEST_CASE("Shadow blur and mip ranges cover only selected layers", "[renderer][shadow]")
    {
        for (U32 mask = 0u; mask < 16u; ++mask)
        {
            CAPTURE(mask);
            U32 visited = 0u;
            U8 previousEnd = 0u;
            bool firstRange = true;
            ForEachShadowLayerRange(mask, 4u, [&](const U8 first, const U8 count)
            {
                CHECK(count > 0u);
                CHECK(first + count <= 4u);
                CHECK((firstRange || first > previousEnd));
                firstRange = false;
                previousEnd = first + count;
                for (U8 layer = first; layer < first + count; ++layer)
                {
                    CHECK(ShadowLayerSelected(mask, layer));
                    CHECK_FALSE(ShadowLayerSelected(visited, layer));
                    visited |= U32{1u} << layer;
                }
            });
            CHECK(visited == mask);
        }
        for (U8 first = 0u; first < 4u; ++first)
        {
            for (U8 relative = 0u; relative < 4u - first; ++relative)
            {
                CHECK(ShadowBlurTexelScale(first) * ShadowBlurTexelScale(relative) ==
                      ShadowBlurTexelScale(to_U8(first + relative)));
            }
        }
        CHECK(ShadowBlurTexelScale(0u) == 1.f);
        CHECK(ShadowBlurTexelScale(3u) == 0.125f);
        U32 calls = 0u;
        ForEachShadowLayerRange(U32_MAX, 32u, [&](const U8 first, const U8 count)
        {
            CHECK(first == 0u);
            CHECK(count == 32u);
            ++calls;
        });
        CHECK(calls == 1u);
        ForEachShadowLayerRange(U32_MAX, 0u, [&](U8, U8) { FAIL("Empty layer sets must not schedule work"); });
    }

    TEST_CASE("Shadow invalidation filters light type and caller supplied bounds", "[renderer][shadow]")
    {
        const BoundingSphere source{float3{0.f}, 500.f};
        const BoundingSphere influence{float3{0.f}, 100.f};
        LightPool::MovingVolume request{source, false, 1u, LightType::POINT, influence};
        CHECK(request.affectsLight(LightType::POINT, BoundingSphere{float3{109.f, 0.f, 0.f}, 10.f}));
        CHECK_FALSE(request.affectsLight(LightType::POINT, BoundingSphere{float3{111.f, 0.f, 0.f}, 10.f}));
        CHECK_FALSE(request.affectsLight(LightType::SPOT, BoundingSphere{float3{0.f}, 10.f}));
        CHECK_FALSE(request.affectsLight(LightType::DIRECTIONAL, BoundingSphere{float3{0.f}, 10.f}));
        request._volume = BoundingSphere{float3{1000.f}, 10.f};
        CHECK_FALSE(request.affectsLight(LightType::POINT, BoundingSphere{float3{0.f}, 10.f}));
        request._lightType = LightType::DIRECTIONAL;
        request._influenceBounds.reset();
        CHECK(request.affectsLight(LightType::DIRECTIONAL, BoundingSphere{float3{0.f}, 10.f}));
        const LightPool::MovingVolume geometryChange{source, true};
        CHECK(geometryChange._layerMask == 0u);
        CHECK(geometryChange.affectsLight(LightType::POINT, BoundingSphere{float3{0.f}, 10.f}));
        CHECK(geometryChange.affectsLight(LightType::SPOT, BoundingSphere{float3{0.f}, 10.f}));
    }

    struct ParticleEmitterTestAccess
    {
        static auto& Upload(ParticleEmitter& emitter, const U64 frame, const float3& eye)
        {
            return emitter.getUpload(frame, eye);
        }
    };

    TEST_CASE("Particle uploads retain every view until its frame slot is recycled", "[renderer][particles]")
    {
        platformInitRunListener::PlatformInit();
        ParticleEmitter emitter{ ResourceDescriptor<ParticleEmitter>{ "particle_upload_lifetime" } };
        auto& first = ParticleEmitterTestAccess::Upload(emitter, 0u, float3{0.f});
        first._uploaded = true;
        first._handles[1] = { 7u, 1u };
        GenericDrawCommand& command = first._commands.emplace_back();
        command._sourceBuffers = first._handles.data();
        command._sourceBuffersCount = to_U16(first._handles.size());
        command._cmd.instanceCount = 17u;
        const GenericDrawCommand queued = command;

        // More views than the old three-slot ring and four-player modulo.
        for (U32 i = 1u; i < 32u; ++i)
        {
            auto& other = ParticleEmitterTestAccess::Upload(emitter, 0u, float3{to_F32(i), 0.f, 0.f});
            CHECK(&other != &first);
            other._handles[1] = { to_U16(i + 8u), 1u };
            CHECK(queued._sourceBuffers[1] == GPUBuffer::Handle{7u, 1u});
        }
        CHECK(&ParticleEmitterTestAccess::Upload(emitter, 0u, float3{0.f}) == &first);
        CHECK(first._uploaded);
        CHECK(queued._cmd.instanceCount == 17u);

        for (U64 frame = 1u; frame < Config::MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            auto& inFlight = ParticleEmitterTestAccess::Upload(emitter, frame, float3{0.f});
            CHECK(&inFlight != &first);
            CHECK(first._uploaded);
        }
        auto& recycled = ParticleEmitterTestAccess::Upload(emitter, Config::MAX_FRAMES_IN_FLIGHT, float3{0.f});
        CHECK(&recycled == &first);
        CHECK_FALSE(recycled._uploaded);
    }

    TEST_CASE("Render packages retain immutable per-view particle commands", "[renderer][particles]")
    {
        GenericDrawCommandContainer first;
        first.emplace_back()._cmd.instanceCount = 2u;
        GenericDrawCommandContainer second;
        second.emplace_back()._cmd.instanceCount = 9u;
        RenderPackage firstPackage;
        RenderPackage secondPackage;
        firstPackage.drawCommandsOverride(&first);
        secondPackage.drawCommandsOverride(&second);
        GFX::DrawCommand queued;
        queued._drawCommands = *firstPackage.drawCommandsOverride();
        firstPackage.drawCommandsOverride(&second);
        CHECK(queued._drawCommands.front()._cmd.instanceCount == 2u);
        CHECK(secondPackage.drawCommandsOverride()->front()._cmd.instanceCount == 9u);
        Clear(firstPackage);
        CHECK(firstPackage.drawCommandsOverride() == nullptr);
    }

    template<typename T>
    concept UniformScalarCanBeSet = requires( UniformData& uniforms, const T& value )
    {
        uniforms.set( 1u, PushConstantType::VEC4, value );
    };

    static_assert( UniformScalarCanBeSet<float4> );
    static_assert( UniformScalarCanBeSet<std::array<float4, 2>> );
    static_assert( !UniformScalarCanBeSet<vector<float4>> );
    static_assert( !UniformScalarCanBeSet<std::vector<float4>> );
    static_assert( sizeof(float4) == 4u * sizeof(F32) );

    TEST_CASE( "SSAO preserves each configured blur kernel size", "[renderer][ssao]" )
    {
        Configuration::SSAOSettings fullRes;
        Configuration::SSAOSettings halfRes;
        fullRes.BlurKernelSize = 0;
        halfRes.BlurKernelSize = 4;

        const std::array<I32, 2> sizes = SSAO::ConfiguredBlurKernelSizes( fullRes, halfRes );
        CHECK( sizes[0] == 0 );
        CHECK( sizes[1] == 4 );
    }

    TEST_CASE( "SSAO resizes all full- and half-resolution targets together", "[renderer][ssao]" )
    {
        struct TestTarget
        {
            vec2<U16> _resolution{};
            U32 _resizeCount = 0u;

            void resize( const U16 width, const U16 height )
            {
                _resolution.set( width, height );
                ++_resizeCount;
            }
        };

        TestTarget output;
        TestTarget blur;
        TestTarget halfOutput;
        TestTarget halfDepthAndNormals;

        const auto checkTargets = [&]( const vec2<U16> resolution, const vec2<U16> halfResolution, const U32 count )
        {
            CHECK( output._resolution.width == resolution.width );
            CHECK( output._resolution.height == resolution.height );
            CHECK( blur._resolution.width == resolution.width );
            CHECK( blur._resolution.height == resolution.height );
            CHECK( halfOutput._resolution.width == halfResolution.width );
            CHECK( halfOutput._resolution.height == halfResolution.height );
            CHECK( halfDepthAndNormals._resolution.width == halfResolution.width );
            CHECK( halfDepthAndNormals._resolution.height == halfResolution.height );
            CHECK( output._resizeCount == count );
            CHECK( blur._resizeCount == count );
            CHECK( halfOutput._resizeCount == count );
            CHECK( halfDepthAndNormals._resizeCount == count );
        };

        SSAO::ResizeTargets( { 1920u, 1080u }, output, blur, halfOutput, halfDepthAndNormals );
        checkTargets( { 1920u, 1080u }, { 960u, 540u }, 1u );

        SSAO::ResizeTargets( { 1279u, 719u }, output, blur, halfOutput, halfDepthAndNormals );
        checkTargets( { 1279u, 719u }, { 640u, 360u }, 2u );

        SSAO::ResizeTargets( { 1u, 1u }, output, blur, halfOutput, halfDepthAndNormals );
        checkTargets( { 1u, 1u }, { 1u, 1u }, 3u );
    }

    TEST_CASE( "Half-resolution SSAO downsampling covers each source texel", "[renderer][shader][ssao]" )
    {
        using std::min;

        for ( const U16 size : std::array<U16, 6>{ { 1u, 2u, 3u, 5u, 1279u, 65535u } } )
        {
            CAPTURE( size );
            const I32 sourceSize = size;
            const I32 targetSize = SSAO::HalfResolution( { size, size } ).width;
            std::vector<bool> covered( size, false );
            for ( I32 pixel = 0; pixel < targetSize; ++pixel )
            {
                for ( const I32 offset : { 0, 1 } )
                {
                    const I32 source = SSAO_DOWNSAMPLE_SOURCE_PIXEL( pixel, offset, sourceSize - 1 );
                    REQUIRE( source >= 0 );
                    REQUIRE( source < sourceSize );
                    CHECK( source == min( pixel * 2 + offset, sourceSize - 1 ) );
                    covered[to_size( source )] = true;
                }
            }
            CHECK( std::ranges::all_of( covered, []( const bool value ) { return value; } ) );
        }
    }

    TEST_CASE( "Half-resolution SSAO upsampling follows the bilinear footprint", "[renderer][shader][ssao]" )
    {
        using std::clamp;
        using std::floor;
        using std::max;

        const auto sourcePixels = []( const I32 fullPixel, const I32 fullSize, const I32 halfSize )
        {
            const F32 uv = ( to_F32( fullPixel ) + 0.5f ) / to_F32( fullSize );
            const F32 halfPosition = SSAO_HALF_GRID_POSITION( uv * to_F32( fullSize ), to_F32( fullSize ), to_F32( fullSize & 1 ) );
            const I32 base = to_I32( floor( halfPosition ) );
            return std::array<I32, 2>{ {
                SSAO_CLAMP_SOURCE_PIXEL( base, 0, 0, halfSize - 1 ),
                SSAO_CLAMP_SOURCE_PIXEL( base, 1, 0, halfSize - 1 )
            } };
        };

        CHECK( ( sourcePixels( 0, 1, 1 ) == std::array<I32, 2>{ { 0, 0 } } ) );
        CHECK( ( sourcePixels( 0, 4, 2 ) == std::array<I32, 2>{ { 0, 0 } } ) );
        CHECK( ( sourcePixels( 1, 4, 2 ) == std::array<I32, 2>{ { 0, 1 } } ) );
        CHECK( ( sourcePixels( 2, 4, 2 ) == std::array<I32, 2>{ { 0, 1 } } ) );
        CHECK( ( sourcePixels( 3, 4, 2 ) == std::array<I32, 2>{ { 1, 1 } } ) );
        CHECK( ( sourcePixels( 0, 5, 3 ) == std::array<I32, 2>{ { 0, 0 } } ) );
        CHECK( ( sourcePixels( 1, 5, 3 ) == std::array<I32, 2>{ { 0, 1 } } ) );
        CHECK( ( sourcePixels( 2, 5, 3 ) == std::array<I32, 2>{ { 0, 1 } } ) );
        CHECK( ( sourcePixels( 3, 5, 3 ) == std::array<I32, 2>{ { 1, 2 } } ) );
        CHECK( ( sourcePixels( 4, 5, 3 ) == std::array<I32, 2>{ { 2, 2 } } ) );
    }

    TEST_CASE( "Half-resolution SSAO reconstructs the selected source pixel without stretching odd edges", "[renderer][shader][ssao]" )
    {
        using std::floor;
        using std::max;
        using std::min;

        for ( const I32 fullSize : { 1, 2, 3, 4, 5, 719, 1279, 1920, 65535 } )
        {
            CAPTURE( fullSize );
            const I32 halfSize = (fullSize + 1) / 2;
            for ( I32 halfPixel = 0; halfPixel < halfSize; ++halfPixel )
            {
                const F32 blockCenter = min( to_F32( halfPixel * 2 + 1 ), to_F32( fullSize ) - 0.5f );
                const F32 halfPosition = SSAO_HALF_GRID_POSITION( blockCenter, to_F32( fullSize ), to_F32( fullSize & 1 ) );
                REQUIRE( halfPosition == Catch::Approx( to_F32( halfPixel ) ).margin( 0.00001f ) );

                for ( const I32 offset : { 0, 1 } )
                {
                    const I32 sourcePixel = SSAO_DOWNSAMPLE_SOURCE_PIXEL( halfPixel, offset, fullSize - 1 );
                    const F32 uv = SSAO_SOURCE_UV( to_F32( sourcePixel ), to_F32( fullSize ) );
                    const F32 reconstructedPixel = uv * to_F32( fullSize ) - 0.5f;
                    REQUIRE( reconstructedPixel == Catch::Approx( to_F32( sourcePixel ) ).margin( 0.004f ) );
                    REQUIRE( to_I32( SSAO_HALF_SAMPLE_POSITION( uv, to_F32( fullSize ) ) ) == halfPixel );
                }
            }
        }
    }

    TEST_CASE( "SSAO uploads kernel elements independently for both quality modes", "[renderer][ssao]" )
    {
        std::array<UniformData, 2> modes;
        vector<float4> fullKernel( 32u );
        vector<float4> halfKernel( 24u );
        for ( size_t i = 0u; i < fullKernel.size(); ++i )
        {
            fullKernel[i].set( to_F32(i), -to_F32(i), to_F32(i) * 0.1f, 1.f );
        }
        for ( size_t i = 0u; i < halfKernel.size(); ++i )
        {
            halfKernel[i].set( -to_F32(i), to_F32(i), to_F32(i) * 0.2f, 0.f );
        }

        const auto checkKernel = []( const UniformData& uniforms, const vector<float4>& expected )
        {
            REQUIRE( uniforms.entries().size() == 1u );
            const UniformData::Entry& entry = uniforms.entries().front();
            REQUIRE( entry._bindingHash == _ID( "sampleKernel" ) );
            REQUIRE( entry._type == PushConstantType::VEC4 );
            REQUIRE( entry._range._length == expected.size() * 4u * sizeof(F32) );
            CHECK( std::memcmp( uniforms.data( entry._range._startOffset ), expected.data(), entry._range._length ) == 0 );
        };

        SSAO::UploadSampleKernel( modes[1], halfKernel );
        SSAO::UploadSampleKernel( modes[0], fullKernel );
        checkKernel( modes[1], halfKernel );
        checkKernel( modes[0], fullKernel );

        halfKernel[0].set( 0.5f, 0.25f, 0.125f, 0.f );
        SSAO::UploadSampleKernel( modes[1], halfKernel );
        SSAO::UploadSampleKernel( modes[0], fullKernel );
        checkKernel( modes[1], halfKernel );
        checkKernel( modes[0], fullKernel );
    }

    TEST_CASE( "OpenGL object labels respect the device length limit", "[renderer][opengl]" )
    {
        std::array<char, 500> storage;
        storage.fill( 'x' );
        const std::string_view label{ storage.data(), storage.size() };
        CHECK( GLUtil::ObjectLabelLength( label.substr( 0u, 12u ), 256u ) == 12u );
        CHECK( GLUtil::ObjectLabelLength( label.substr( 0u, 255u ), 256u ) == 255u );
        CHECK( GLUtil::ObjectLabelLength( label.substr( 0u, 256u ), 256u ) == 255u );
        CHECK( GLUtil::ObjectLabelLength( label, 256u ) == 255u );
        CHECK( GLUtil::ObjectLabelLength( label, 1u ) == 0u );
        CHECK( GLUtil::ObjectLabelLength( label, 0u ) == 0u );

        const std::string_view substring{ storage.data() + 1u, 3u };
        CHECK( GLUtil::ObjectLabelLength( substring, 256u ) == 3u );
        CHECK( GLUtil::ObjectLabelLength( substring, 3u ) == 2u );
    }

    TEST_CASE( "Vulkan tool enumeration handles changing counts and errors", "[render_smoke][vulkan_tools]" )
    {
        std::vector<VkPhysicalDeviceToolProperties> tools( 1u );

        SECTION( "No active tools" )
        {
            const VkResult result = EnumeratePhysicalDeviceTools(
                []( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    CHECK( properties == nullptr );
                    *count = 0u;
                    return VK_SUCCESS;
                },
                tools );
            CHECK( result == VK_SUCCESS );
            CHECK( tools.empty() );
        }

        SECTION( "An incomplete result is retried using a fresh count" )
        {
            U32 countCalls = 0u;
            U32 dataCalls = 0u;
            const VkResult result = EnumeratePhysicalDeviceTools(
                [&]( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    if ( properties == nullptr )
                    {
                        *count = countCalls++ == 0u ? 1u : 2u;
                        return VK_SUCCESS;
                    }

                    for ( U32 i = 0u; i < *count; ++i )
                    {
                        CHECK( properties[i].sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES );
                        CHECK( properties[i].pNext == nullptr );
                    }
                    if ( dataCalls++ == 0u )
                    {
                        CHECK( *count == 1u );
                        return VK_INCOMPLETE;
                    }
                    CHECK( *count == 2u );
                    properties[0].name[0] = 'A';
                    properties[1].name[0] = 'B';
                    return VK_SUCCESS;
                },
                tools );
            REQUIRE( result == VK_SUCCESS );
            REQUIRE( tools.size() == 2u );
            CHECK( tools[0].name[0] == 'A' );
            CHECK( tools[1].name[0] == 'B' );
            CHECK( countCalls == 2u );
            CHECK( dataCalls == 2u );
        }

        SECTION( "A shrinking count keeps only the returned properties" )
        {
            const VkResult result = EnumeratePhysicalDeviceTools(
                []( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    if ( properties == nullptr )
                    {
                        *count = 2u;
                    }
                    else
                    {
                        *count = 1u;
                    }
                    return VK_SUCCESS;
                },
                tools );
            CHECK( result == VK_SUCCESS );
            CHECK( tools.size() == 1u );
        }

        SECTION( "Repeated incomplete results are bounded" )
        {
            U32 dataCalls = 0u;
            const VkResult result = EnumeratePhysicalDeviceTools(
                [&]( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    *count = 1u;
                    if ( properties == nullptr )
                    {
                        return VK_SUCCESS;
                    }
                    ++dataCalls;
                    return VK_INCOMPLETE;
                },
                tools );
            CHECK( result == VK_INCOMPLETE );
            CHECK( dataCalls == MAX_VULKAN_TOOL_ENUMERATION_ATTEMPTS );
            CHECK( tools.empty() );
        }

        SECTION( "Query failures and invalid returned counts do not publish partial results" )
        {
            for ( const bool invalidCount : { false, true } )
            {
                const VkResult result = EnumeratePhysicalDeviceTools(
                    [invalidCount]( U32* count, VkPhysicalDeviceToolProperties* properties )
                    {
                        if ( properties == nullptr )
                        {
                            *count = 1u;
                            return VK_SUCCESS;
                        }
                        if ( invalidCount )
                        {
                            *count = 2u;
                            return VK_SUCCESS;
                        }
                        return VK_ERROR_OUT_OF_HOST_MEMORY;
                    },
                    tools );
                CHECK( result == ( invalidCount ? VK_ERROR_UNKNOWN : VK_ERROR_OUT_OF_HOST_MEMORY ) );
                CHECK( tools.empty() );
            }
        }
    }

    TEST_CASE( "Blur passes record their constants before every draw", "[render_smoke][blur]" )
    {
        for ( const bool gaussian : { false, true } )
        {
            for ( const U8 layers : { U8{ 1u }, U8{ 3u } } )
            {
                for ( const F32 direction : { 0.f, 1.f } )
                {
                    CAPTURE( gaussian, layers, direction );
                    PushConstantsStruct pushData{};
                    pushData.data[0]._vec[0].w = 7.f;
                    pushData.data[0]._vec[1].x = direction;
                    pushData.data[0]._vec[1].y = to_F32( layers );

                    GFX::CommandBuffer commands;
                    GFX::RecordBlurDrawCommands( commands, pushData, gaussian, layers );
                    const size_t drawCount = gaussian ? 1u : to_size( layers );
                    REQUIRE( commands.commands().size() == drawCount * 2u );
                    for ( size_t draw = 0u; draw < drawCount; ++draw )
                    {
                        auto* constants = commands.commands()[draw * 2u];
                        const auto* command = commands.commands()[draw * 2u + 1u];
                        REQUIRE( constants->type() == GFX::CommandType::SEND_PUSH_CONSTANTS );
                        REQUIRE( command->type() == GFX::CommandType::DRAW_COMMANDS );
                        const PushConstantsStruct& recorded = constants->As<GFX::SendPushConstantsCommand>()->_fastData;
                        CHECK( recorded.data[0]._vec[0].x == ( gaussian ? 0.f : to_F32( draw ) ) );
                        CHECK( recorded.data[0]._vec[0].w == 7.f );
                        CHECK( recorded.data[0]._vec[1].x == direction );
                        CHECK( recorded.data[0]._vec[1].y == to_F32( layers ) );
                    }
                }
            }
        }
    }

    TEST_CASE( "View depth and colour passes exclude translucent bins without changing shadow casters", "[renderer][render_queue]" )
    {
        using Params = RenderQueue::PopulateQueueParams;
        constexpr std::array<RenderBinType, 7u> bins{{
            RenderBinType::OPAQUE, RenderBinType::WATER, RenderBinType::TERRAIN, RenderBinType::TERRAIN_AUX,
            RenderBinType::SKY, RenderBinType::TRANSLUCENT, RenderBinType::IMPOSTOR
        }};
        constexpr std::array<RenderStage, 4u> stages{{
            RenderStage::DISPLAY, RenderStage::REFLECTION, RenderStage::REFRACTION, RenderStage::NODE_PREVIEW
        }};

        for ( const RenderStage stage : stages )
        {
            for ( const RenderPassType pass : { RenderPassType::PRE_PASS, RenderPassType::MAIN_PASS } )
            {
                const Params params = Params::ForPass( { stage, pass }, false );
                for ( const RenderBinType bin : bins )
                {
                    CAPTURE( stage, pass, bin );
                    CHECK( params.includes( bin ) == ( bin != RenderBinType::TRANSLUCENT ) );
                }
                CHECK_FALSE( params.includes( RenderBinType::COUNT ) );
            }

            for ( const RenderPassType pass : { RenderPassType::OIT_PASS, RenderPassType::TRANSPARENCY_PASS } )
            {
                const Params params = Params::ForPass( { stage, pass }, true );
                for ( const RenderBinType bin : bins )
                {
                    CAPTURE( stage, pass, bin );
                    CHECK( params.includes( bin ) == ( bin == RenderBinType::TRANSLUCENT ) );
                }
            }
        }

        for ( const RenderPassType pass : { RenderPassType::PRE_PASS, RenderPassType::MAIN_PASS } )
        {
            const Params params = Params::ForPass( { RenderStage::SHADOW, pass }, false );
            for ( const RenderBinType bin : bins )
            {
                CAPTURE( pass, bin );
                CHECK( params.includes( bin ) );
            }
        }

        const Params all{ {}, RenderBinType::COUNT, false };
        const Params none{ {}, RenderBinType::COUNT, true };
        for ( const RenderBinType bin : bins )
        {
            CHECK( all.includes( bin ) );
            CHECK_FALSE( none.includes( bin ) );
        }
    }

    TEST_CASE( "Material readiness polls every required render pass", "[renderer][render_queue][startup]" )
    {
        VisibleNode node{};
        U32 checks = 0u;
        const auto check = [&]( const bool ready )
        {
            return node.checkMaterialReadiness( [&]
            {
                ++checks;
                return ready;
            } );
        };

        CHECK_FALSE( check( false ) );
        CHECK( check( true ) );
        CHECK_FALSE( check( false ) );
        CHECK( checks == 3u );
        CHECK_FALSE( node._materialReady );

        node = {};
        CHECK( check( true ) );
        CHECK( check( true ) );
        CHECK( node._materialReady );
    }

    TEST_CASE( "Material readiness follows the bins used to prepare each pass", "[renderer][render_queue][startup]" )
    {
        using Params = RenderQueue::PopulateQueueParams;
        for ( const RenderStage stage : { RenderStage::DISPLAY, RenderStage::REFLECTION, RenderStage::REFRACTION, RenderStage::NODE_PREVIEW } )
        {
            for ( const RenderBinType bin : { RenderBinType::OPAQUE, RenderBinType::SKY, RenderBinType::WATER, RenderBinType::TRANSLUCENT } )
            {
                CAPTURE( stage, bin );
                const bool translucent = bin == RenderBinType::TRANSLUCENT;
                CHECK( Params::RequiresMaterialForPreparation( { stage, RenderPassType::MAIN_PASS }, bin, false ) );
                CHECK( Params::RequiresMaterialForPreparation( { stage, RenderPassType::PRE_PASS }, bin, true ) == !translucent );
                CHECK_FALSE( Params::RequiresMaterialForPreparation( { stage, RenderPassType::PRE_PASS }, bin, false ) );
                for ( const RenderPassType pass : { RenderPassType::OIT_PASS, RenderPassType::TRANSPARENCY_PASS } )
                {
                    CHECK( Params::RequiresMaterialForPreparation( { stage, pass }, bin, true ) == translucent );
                    CHECK_FALSE( Params::RequiresMaterialForPreparation( { stage, pass }, bin, false ) );
                }
            }
        }

        CHECK( Params::RequiresMaterialForPreparation( { RenderStage::SHADOW, RenderPassType::MAIN_PASS },
                                                        RenderBinType::TRANSLUCENT, true ) );
        CHECK( Params::RequiresMaterialForPreparation( { RenderStage::SHADOW, RenderPassType::PRE_PASS },
                                                        RenderBinType::TRANSLUCENT, true ) );
    }

    TEST_CASE( "Only nodes with prepared main-pass draws reach later passes", "[renderer][render_queue][startup]" )
    {
        VisibleNodeList<VisibleNode, 4u> nodes{};
        for ( U32 i = 0u; i < 4u; ++i )
        {
            VisibleNode node{};
            node._distanceToCameraSq = to_F32( i );
            nodes.append( node );
        }

        CHECK( nodes.node( 0u ).prepareDraw( [] { return true; } ) );
        CHECK_FALSE( nodes.node( 1u ).prepareDraw( [] { return false; } ) );
        CHECK( nodes.node( 2u ).prepareDraw( [] { return true; } ) );
        CHECK( nodes.node( 3u ).prepareDraw( [] { return true; } ) );
        CHECK_FALSE( nodes.node( 3u ).prepareDraw( [] { return false; } ) );

        nodes.removeIf( []( const VisibleNode& node ) { return !node._drawPrepared; } );
        REQUIRE( nodes.size() == 2u );
        CHECK( nodes.node( 0u )._distanceToCameraSq == 0.f );
        CHECK( nodes.node( 1u )._distanceToCameraSq == 2.f );
        CHECK( nodes.node( 0u )._drawPrepared );
        CHECK( nodes.node( 1u )._drawPrepared );

        nodes.removeIf( []( const VisibleNode& ) { return true; } );
        CHECK( nodes.size() == 0u );
        nodes.removeIf( []( const VisibleNode& ) { return true; } );
        CHECK( nodes.size() == 0u );
    }

    TEST_CASE( "OIT composition selects the destination and sample-count variant", "[render_smoke][oit]" )
    {
        using Variant = RenderPassExecutor::OITCompositionVariant;
        CHECK( RenderPassExecutor::SelectOITCompositionVariant( false, false ) == Variant::COLOUR );
        CHECK( RenderPassExecutor::SelectOITCompositionVariant( false, true ) == Variant::COLOUR_MSAA );
        CHECK( RenderPassExecutor::SelectOITCompositionVariant( true, false ) == Variant::COLOUR_NORMALS );
        CHECK( RenderPassExecutor::SelectOITCompositionVariant( true, true ) == Variant::COLOUR_NORMALS_MSAA );
    }

    TEST_CASE( "Vulkan MSAA state defaults to per-pixel shading", "[render_smoke][pipeline][msaa]" )
    {
        constexpr std::array<VkSampleCountFlagBits, 4> sampleCounts{{ VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT,
                                                                    VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT }};
        for ( const VkSampleCountFlagBits sampleCount : sampleCounts )
        {
            const VkPipelineMultisampleStateCreateInfo state = vk::pipelineMultisampleStateCreateInfo( sampleCount );
            CHECK( state.sType == VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO );
            CHECK( state.rasterizationSamples == sampleCount );
            CHECK( state.sampleShadingEnable == VK_FALSE );
            CHECK( state.minSampleShading == 0.f );
        }
    }

    TEST_CASE( "Vulkan pipeline variants retain the complete rendering signature", "[render_smoke][pipeline]" )
    {
        std::array<VkFormat, 2> colourFormats{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM };
        VkPipelineRenderingCreateInfo info{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        info.colorAttachmentCount = to_U32( colourFormats.size() );
        info.pColorAttachmentFormats = colourFormats.data();

        const VKRenderingSignature base = VKRenderingSignature::From( info, 1u );
        CHECK( base._colourAttachmentCount == 2u );
        CHECK( base._colourFormats[0] == colourFormats[0] );
        CHECK( base._colourFormats[1] == colourFormats[1] );
        CHECK( base._depthFormat == VK_FORMAT_UNDEFINED );
        CHECK( base._stencilFormat == VK_FORMAT_UNDEFINED );

        info.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        const VKRenderingSignature depth = VKRenderingSignature::From( info, 1u );
        CHECK( depth != base );
        info.stencilAttachmentFormat = VK_FORMAT_S8_UINT;
        CHECK( VKRenderingSignature::From( info, 1u ) != depth );
        info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
        info.viewMask = 3u;
        CHECK( VKRenderingSignature::From( info, 1u ) != depth );
        info.viewMask = 0u;
        CHECK( VKRenderingSignature::From( info, 4u ) != depth );
        info.colorAttachmentCount = 1u;
        CHECK( VKRenderingSignature::From( info, 1u ) != depth );
        info.colorAttachmentCount = 2u;
        colourFormats[0] = VK_FORMAT_R8G8B8A8_UNORM;
        CHECK( VKRenderingSignature::From( info, 1u ) != depth );
        CHECK( depth._colourFormats[0] == VK_FORMAT_R16G16B16A16_SFLOAT );

        struct CollidingHash
        {
            size_t operator()( const VKPipelineCacheKey& ) const noexcept { return 0u; }
        };
        hashMap<VKPipelineCacheKey, U32, CollidingHash> variants;
        const VKPipelineCacheKey colourOnly{ ._pipelineStateHash = 42u, ._rendering = base };
        const VKPipelineCacheKey withDepth{ ._pipelineStateHash = 42u, ._rendering = depth };
        variants[colourOnly] = 1u;
        variants[withDepth] = 2u;
        REQUIRE( variants.size() == 2u );
        CHECK( variants[colourOnly] == 1u );
        CHECK( variants[withDepth] == 2u );
        CHECK( VKPipelineCacheKeyHash{}( colourOnly ) == VKPipelineCacheKeyHash{}( colourOnly ) );
    }

    TEST_CASE( "Vulkan pipeline keys share dynamic state without merging baked state", "[render_smoke][pipeline]" )
    {
        PipelineDescriptor descriptor{};
        descriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
        const Pipeline baseline{ descriptor };
        VKRenderingSignature rendering{};
        rendering._sampleCount = 1u;
        const VKPipelineCacheKey dynamicKey = VKPipelineCacheKey::From( baseline, rendering, true );
        const VKPipelineCacheKey staticKey = VKPipelineCacheKey::From( baseline, rendering, false );

        std::array<PipelineDescriptor, 18u> dynamicStates;
        dynamicStates.fill( descriptor );
        dynamicStates[0]._stateBlock._cullMode = CullMode::NONE;
        dynamicStates[1]._stateBlock._frontFaceCCW = false;
        dynamicStates[2]._stateBlock._scissorTestEnabled = true;
        dynamicStates[3]._stateBlock._depthTestEnabled = false;
        dynamicStates[4]._stateBlock._depthWriteEnabled = false;
        dynamicStates[5]._stateBlock._zFunc = ComparisonFunction::LESS;
        dynamicStates[6]._stateBlock._zBias = 1.f;
        dynamicStates[7]._stateBlock._zUnits = 1.f;
        dynamicStates[8]._stateBlock._stencilEnabled = true;
        dynamicStates[9]._stateBlock._stencilRef = 7u;
        dynamicStates[10]._stateBlock._stencilMask = 7u;
        dynamicStates[11]._stateBlock._stencilWriteMask = 7u;
        dynamicStates[12]._stateBlock._stencilFailOp = StencilOperation::REPLACE;
        dynamicStates[13]._stateBlock._stencilPassOp = StencilOperation::REPLACE;
        dynamicStates[14]._stateBlock._stencilZFailOp = StencilOperation::REPLACE;
        dynamicStates[15]._stateBlock._stencilFunc = ComparisonFunction::ALWAYS;
        dynamicStates[16]._stateBlock._primitiveRestartEnabled = true;
        dynamicStates[17]._stateBlock._rasterizationEnabled = false;
        for ( size_t i = 0u; i < dynamicStates.size(); ++i )
        {
            CAPTURE( i );
            const Pipeline changed{ dynamicStates[i] };
            CHECK( changed.stateHash() != baseline.stateHash() );
            CHECK( VKPipelineCacheKey::From( changed, rendering, true ) == dynamicKey );
            CHECK( VKPipelineCacheKey::From( changed, rendering, false ) == staticKey );
        }

        PipelineDescriptor changedColour = descriptor;
        changedColour._stateBlock._colourWrite.b[0] = false;
        const Pipeline colourPipeline{ changedColour };
        CHECK( colourPipeline.stateHash() != baseline.stateHash() );
        CHECK( VKPipelineCacheKey::From( colourPipeline, rendering, true ) == dynamicKey );
        CHECK( VKPipelineCacheKey::From( colourPipeline, rendering, false ) != staticKey );

        PipelineDescriptor changedBlend = descriptor;
        changedBlend._blendStates._settings[0].enabled( true );
        const Pipeline blendPipeline{ changedBlend };
        CHECK( blendPipeline.stateHash() != baseline.stateHash() );
        CHECK( VKPipelineCacheKey::From( blendPipeline, rendering, true ) == dynamicKey );
        CHECK( VKPipelineCacheKey::From( blendPipeline, rendering, false ) != staticKey );

        std::array<PipelineDescriptor, 7u> bakedStates;
        bakedStates.fill( descriptor );
        bakedStates[0]._primitiveTopology = PrimitiveTopology::LINES;
        bakedStates[1]._stateBlock._fillMode = FillMode::WIREFRAME;
        bakedStates[2]._stateBlock._tessControlPoints = 3u;
        bakedStates[3]._multiSampleCount = 4u;
        bakedStates[4]._alphaToCoverage = true;
        bakedStates[5]._shaderProgramHandle = Handle<ShaderProgram>{ ._generation = 1u, ._index = 1u };
        bakedStates[6]._vertexFormat._attributes[0]._strideInBytes = 4u;
        bakedStates[6]._vertexFormat._attributes[0]._dataType = GFXDataFormat::FLOAT_32;
        for ( size_t i = 0u; i < bakedStates.size(); ++i )
        {
            CAPTURE( i );
            const Pipeline changed{ bakedStates[i] };
            CHECK( VKPipelineCacheKey::From( changed, rendering, true ) != dynamicKey );
            CHECK( VKPipelineCacheKey::From( changed, rendering, false ) != staticKey );
        }
        CHECK( descriptor != bakedStates[4] );
        CHECK_FALSE( descriptor == bakedStates[4] );

        rendering._depthFormat = VK_FORMAT_D32_SFLOAT;
        CHECK( VKPipelineCacheKey::From( baseline, rendering, true ) != dynamicKey );
    }

    TEST_CASE( "Vulkan stencil state is reissued across disable and enable transitions", "[render_smoke][pipeline]" )
    {
        VKPerWindowState::VKDynamicState state{};
        CHECK( state.needsStencilTestUpdate( false ) );
        CHECK( state.needsStencilTestUpdate( true ) );
        CHECK( state.needsStencilValuesReset() );

        state._isSet = true;
        CHECK_FALSE( state.needsStencilTestUpdate( false ) );
        CHECK( state.needsStencilTestUpdate( true ) );
        CHECK( state.needsStencilValuesReset() );

        state._block._stencilEnabled = true;
        CHECK( state.needsStencilTestUpdate( false ) );
        CHECK_FALSE( state.needsStencilTestUpdate( true ) );
        CHECK_FALSE( state.needsStencilValuesReset() );

        state._block._stencilEnabled = false;
        state._block._stencilMask = 7u;
        CHECK( state.needsStencilTestUpdate( true ) );
        CHECK( state.needsStencilValuesReset() );
    }

    TEST_CASE( "Vulkan rebinds retained graphics pipelines when pass compatibility changes", "[render_smoke][pipeline]" )
    {
        PipelineDescriptor descriptor{};
        descriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
        const Pipeline logicalPipeline{ descriptor };
        VKStateTracker tracker{};
        tracker._logicalPipeline = &logicalPipeline;
        tracker._pipeline._bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        tracker._renderingSignature._sampleCount = 1u;
        tracker._boundRenderingSignature = tracker._renderingSignature;
        CHECK_FALSE( tracker.needsGraphicsPipelineRebind() );

        tracker._renderingSignature._depthFormat = VK_FORMAT_D32_SFLOAT;
        CHECK( tracker.needsGraphicsPipelineRebind() );
        tracker._boundRenderingSignature = tracker._renderingSignature;
        CHECK_FALSE( tracker.needsGraphicsPipelineRebind() );
        tracker._renderingSignature._sampleCount = 4u;
        CHECK( tracker.needsGraphicsPipelineRebind() );
        tracker._pipeline._bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        CHECK_FALSE( tracker.needsGraphicsPipelineRebind() );
        tracker._pipeline._bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        tracker._logicalPipeline = nullptr;
        CHECK_FALSE( tracker.needsGraphicsPipelineRebind() );
    }

    TEST_CASE( "Vulkan buffer bindings track each recording and window independently", "[render_smoke][vulkan_bindings]" )
    {
        const VkBuffer bufferA = std::bit_cast<VkBuffer>( U64{ 1u } );
        const VkBuffer bufferB = std::bit_cast<VkBuffer>( U64{ 2u } );
        VKPerWindowState mainWindow{};
        VKPerWindowState nestedWindow{};
        VKBufferBindingState& main = mainWindow._bufferBindings;
        VKBufferBindingState& nested = nestedWindow._bufferBindings;

        CHECK( main.updateVertexBinding( 0u, bufferA, 0u ) );
        CHECK_FALSE( main.updateVertexBinding( 0u, bufferA, 0u ) );
        CHECK( main.updateVertexBinding( 1u, bufferA, 0u ) );
        CHECK_FALSE( main.updateVertexBinding( 0u, bufferA, 0u ) );
        CHECK( main.updateVertexBinding( 0u, bufferA, 64u ) );
        CHECK_FALSE( main.updateVertexBinding( 0u, bufferA, 64u ) );
        CHECK( main.updateVertexBinding( 0u, bufferB, 64u ) );
        CHECK_FALSE( main.updateVertexBinding( 0u, bufferB, 64u ) );

        CHECK( main.updateIndexBinding( bufferA, 0u, VK_INDEX_TYPE_UINT16 ) );
        CHECK_FALSE( main.updateIndexBinding( bufferA, 0u, VK_INDEX_TYPE_UINT16 ) );
        CHECK( main.updateIndexBinding( bufferA, 64u, VK_INDEX_TYPE_UINT16 ) );
        CHECK( main.updateIndexBinding( bufferA, 64u, VK_INDEX_TYPE_UINT32 ) );
        CHECK( main.updateIndexBinding( bufferB, 64u, VK_INDEX_TYPE_UINT32 ) );
        CHECK_FALSE( main.updateIndexBinding( bufferB, 64u, VK_INDEX_TYPE_UINT32 ) );

        CHECK( nested.updateVertexBinding( 0u, bufferB, 64u ) );
        CHECK( nested.updateIndexBinding( bufferB, 64u, VK_INDEX_TYPE_UINT32 ) );
        CHECK_FALSE( main.updateVertexBinding( 0u, bufferB, 64u ) );
        CHECK_FALSE( main.updateIndexBinding( bufferB, 64u, VK_INDEX_TYPE_UINT32 ) );

        const U16 untrackedBinding = to_U16( to_base( AttribLocation::COUNT ) );
        CHECK( main.updateVertexBinding( untrackedBinding, bufferA, 0u ) );
        CHECK( main.updateVertexBinding( untrackedBinding, bufferA, 0u ) );
        CHECK_FALSE( main.updateVertexBinding( 0u, bufferB, 64u ) );

        main.reset();
        CHECK( main.updateVertexBinding( 0u, bufferB, 64u ) );
        CHECK( main.updateIndexBinding( bufferB, 64u, VK_INDEX_TYPE_UINT32 ) );
        CHECK_FALSE( nested.updateVertexBinding( 0u, bufferB, 64u ) );
        CHECK_FALSE( nested.updateIndexBinding( bufferB, 64u, VK_INDEX_TYPE_UINT32 ) );
    }

    TEST_CASE( "OpenGL uniform-only descriptor updates preserve other bindings", "[render_smoke][opengl_bindings]" )
    {
        using Scope = GLDescriptorUpdateScope;
        for ( U8 flags = 0u; flags < 16u; ++flags )
        {
            const bool descriptorDirty = ( flags & 1u ) != 0u;
            const bool fallbackChanged = ( flags & 2u ) != 0u;
            const bool perDraw = ( flags & 4u ) != 0u;
            const bool uniformsChanged = ( flags & 8u ) != 0u;
            const Scope expected = descriptorDirty || fallbackChanged ? Scope::ALL :
                                   perDraw && uniformsChanged ? Scope::UNIFORM_BUFFERS : Scope::NONE;
            CAPTURE( flags );
            CHECK( SelectGLDescriptorUpdateScope( descriptorDirty, fallbackChanged, perDraw, uniformsChanged ) == expected );
        }

        for ( const DescriptorSetBindingType type : {
                  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, DescriptorSetBindingType::IMAGE,
                  DescriptorSetBindingType::UNIFORM_BUFFER_STATIC, DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC,
                  DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC, DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC,
                  DescriptorSetBindingType::INDIRECT_BUFFER, DescriptorSetBindingType::COUNT } )
        {
            CAPTURE( to_base( type ) );
            CHECK_FALSE( ShouldUpdateGLDescriptor( Scope::NONE, type ) );
            CHECK( ShouldUpdateGLDescriptor( Scope::ALL, type ) );
            const bool expected = type == DescriptorSetBindingType::UNIFORM_BUFFER_STATIC ||
                                  type == DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC ||
                                  type == DescriptorSetBindingType::COUNT;
            CHECK( ShouldUpdateGLDescriptor( Scope::UNIFORM_BUFFERS, type ) == expected );
        }
    }

    TEST_CASE( "Vulkan push descriptors update only changed slots within a recording and layout", "[render_smoke][vulkan_bindings]" )
    {
        const VkPipelineLayout layoutA = std::bit_cast<VkPipelineLayout>( U64{ 1u } );
        const VkPipelineLayout layoutB = std::bit_cast<VkPipelineLayout>( U64{ 2u } );
        const VkBuffer bufferA = std::bit_cast<VkBuffer>( U64{ 3u } );
        const VkBuffer bufferB = std::bit_cast<VkBuffer>( U64{ 4u } );
        const VkImageView viewA = std::bit_cast<VkImageView>( U64{ 5u } );
        const VkImageView viewB = std::bit_cast<VkImageView>( U64{ 6u } );
        const VkSampler samplerA = std::bit_cast<VkSampler>( U64{ 7u } );
        const VkSampler samplerB = std::bit_cast<VkSampler>( U64{ 8u } );

        VKDescriptorSlotState buffer{};
        buffer._type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        buffer._bufferInfo = { bufferA, 16u, 32u };
        VKDescriptorSlotState image{};
        image._type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        image._imageInfo = { samplerA, viewA, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };

        VKPushDescriptorState graphics{};
        VKPushDescriptorState compute{};
        REQUIRE( graphics.needsUpdate( layoutA, 1u ) );
        graphics.beginUpdate( layoutA );
        CHECK( graphics.updateSlot( 0u, buffer ) );
        CHECK( graphics.updateSlot( 1u, image ) );
        graphics.finishUpdate( 1u );
        CHECK_FALSE( graphics.needsUpdate( layoutA, 1u ) );
        CHECK( graphics.needsUpdate( layoutA, 2u ) );

        graphics.beginUpdate( layoutA );
        CHECK_FALSE( graphics.updateSlot( 0u, buffer ) );
        CHECK_FALSE( graphics.updateSlot( 1u, image ) );
        graphics.finishUpdate( 2u );

        VKDescriptorSlotState changed = buffer;
        changed._bufferInfo.buffer = bufferB;
        CHECK( graphics.updateSlot( 0u, changed ) );
        changed._bufferInfo.offset += 1u;
        CHECK( graphics.updateSlot( 0u, changed ) );
        changed._bufferInfo.range += 1u;
        CHECK( graphics.updateSlot( 0u, changed ) );
        changed._type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        CHECK( graphics.updateSlot( 0u, changed ) );
        CHECK_FALSE( graphics.updateSlot( 0u, changed ) );
        CHECK_FALSE( graphics.updateSlot( 1u, image ) );

        changed = image;
        changed._imageInfo.sampler = samplerB;
        CHECK( graphics.updateSlot( 1u, changed ) );
        changed._imageInfo.imageView = viewB;
        CHECK( graphics.updateSlot( 1u, changed ) );
        changed._imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        CHECK( graphics.updateSlot( 1u, changed ) );
        changed._type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        CHECK( graphics.updateSlot( 1u, changed ) );
        CHECK_FALSE( graphics.updateSlot( 1u, changed ) );

        compute.beginUpdate( layoutA );
        CHECK( compute.updateSlot( 0u, buffer ) );
        CHECK( compute.updateSlot( 1u, image ) );
        compute.finishUpdate( 2u );
        CHECK_FALSE( graphics.updateSlot( 1u, changed ) );

        CHECK( graphics.needsUpdate( layoutB, 2u ) );
        graphics.beginUpdate( layoutB );
        CHECK( graphics.updateSlot( 0u, buffer ) );
        CHECK( graphics.updateSlot( 1u, image ) );
        graphics.finishUpdate( 2u );
        CHECK_FALSE( graphics.needsUpdate( layoutB, 2u ) );
        CHECK_FALSE( compute.updateSlot( 0u, buffer ) );
        CHECK_FALSE( compute.updateSlot( 1u, image ) );

        graphics.beginUpdate( layoutA );
        CHECK( graphics.updateSlot( 0u, buffer ) );
        CHECK( graphics.updateSlot( 1u, image ) );
        graphics.finishUpdate( 2u );
        graphics = {};
        CHECK( graphics.needsUpdate( layoutA, 2u ) );
        graphics.beginUpdate( layoutA );
        CHECK( graphics.updateSlot( 0u, buffer ) );
        CHECK( graphics.updateSlot( 1u, image ) );
    }

    TEST_CASE( "FontStash vertex format matches its uploaded vertices", "[render_smoke][font]" )
    {
        const AttributeMap format = CreateFontVertexFormat();
        REQUIRE( format._vertexBindings.size() == 1u );
        CHECK( format._vertexBindings[0]._bufferBindIndex == 0u );
        CHECK( format._vertexBindings[0]._strideInBytes == sizeof( FONSvert ) );

        AttributeFlags shaderInputs{};
        shaderInputs[to_base( AttribLocation::POSITION )] = true;
        shaderInputs[to_base( AttribLocation::TEXCOORD )] = true;
        shaderInputs[to_base( AttribLocation::COLOR )] = true;
        const VertexInputDescription input = getVertexDescription( format, shaderInputs );
        REQUIRE( input.bindings.size() == 1u );
        CHECK( input.bindings[0].binding == 0u );
        CHECK( input.bindings[0].stride == sizeof( FONSvert ) );
        CHECK( input.bindings[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX );
        REQUIRE( input.attributes.size() == 3u );

        const auto checkAttribute = []( const VkVertexInputAttributeDescription& attribute,
                                        const AttribLocation location, const VkFormat type, const size_t offset )
        {
            CHECK( attribute.binding == 0u );
            CHECK( attribute.location == to_base( location ) );
            CHECK( attribute.format == type );
            CHECK( attribute.offset == offset );
        };
        checkAttribute( input.attributes[0], AttribLocation::POSITION, VK_FORMAT_R32G32_SFLOAT, offsetof( FONSvert, pos ) );
        checkAttribute( input.attributes[1], AttribLocation::TEXCOORD, VK_FORMAT_R32G32_SFLOAT, offsetof( FONSvert, tcoords ) );
        checkAttribute( input.attributes[2], AttribLocation::COLOR, VK_FORMAT_R8G8B8A8_UNORM, offsetof( FONSvert, colours ) );
    }

    TEST_CASE( "FontStash keeps the atlas only after successful creation", "[render_smoke][font]" )
    {
        struct AtlasState
        {
            I32 _createResult = 1;
            I32 _resizeResult = 1;
            U32 _creates = 0u;
            U32 _resizes = 0u;
            U32 _deletes = 0u;
        } state;

        FONSparams params{};
        params.width = 32;
        params.height = 32;
        params.userPtr = &state;
        params.renderCreate = []( void* userPtr, int, int )
        {
            AtlasState& atlas = *static_cast<AtlasState*>( userPtr );
            ++atlas._creates;
            return atlas._createResult;
        };
        params.renderResize = []( void* userPtr, int, int )
        {
            AtlasState& atlas = *static_cast<AtlasState*>( userPtr );
            ++atlas._resizes;
            return atlas._resizeResult;
        };
        params.renderDelete = []( void* userPtr )
        {
            ++static_cast<AtlasState*>( userPtr )->_deletes;
        };

        SECTION( "A successfully created atlas remains usable until deletion" )
        {
            FONScontext* context = fonsCreateInternal( &params );
            REQUIRE( context != nullptr );
            CHECK( state._creates == 1u );
            CHECK( state._deletes == 0u );
            CHECK( fonsExpandAtlas( context, 64, 64 ) == 1 );
            CHECK( state._resizes == 1u );

            state._resizeResult = 0;
            CHECK( fonsExpandAtlas( context, 128, 128 ) == 0 );
            int width = 0;
            int height = 0;
            fonsGetAtlasSize( context, &width, &height );
            CHECK( width == 64 );
            CHECK( height == 64 );
            fonsDeleteInternal( context );
            CHECK( state._deletes == 1u );
        }

        SECTION( "A failed atlas creation returns no context and releases its resources" )
        {
            state._createResult = 0;
            CHECK( fonsCreateInternal( &params ) == nullptr );
            CHECK( state._creates == 1u );
            CHECK( state._deletes == 1u );
        }
    }

    TEST_CASE( "Immediate draws retain independent uniforms until their frame is flushed", "[render_smoke][uniforms][editor]" )
    {
        UniformDataSnapshots snapshots{ 2u };
        UniformData defaults;
        GFX::CommandBuffer commands;
        constexpr U64 customBinding = _ID( "customImmediateValue" );
        constexpr U64 matrixBinding = _ID( "dvd_WorldMatrix" );
        constexpr U32 drawCount = 24u;
        std::array<mat4<F32>, drawCount> expectedMatrices;
        std::array<const UniformData*, drawCount> expectedAddresses{};

        for (U32 i = 0u; i < drawCount; ++i)
        {
            defaults.set( customBinding, PushConstantType::UINT, i );
            expectedMatrices[i] = MAT4_IDENTITY;
            expectedMatrices[i]._vec[3].x = to_F32(i + 1u);
            snapshots.record( 42u, defaults, [&](UniformData& uniforms)
            {
                uniforms.set( matrixBinding, PushConstantType::MAT4, expectedMatrices[i] );
                expectedAddresses[i] = &uniforms;
                GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( commands )->_uniformData = &uniforms;
            } );
        }

        REQUIRE( commands.commands().size() == drawCount );
        for (U32 i = 0u; i < drawCount; ++i)
        {
            const auto* command = commands.commands()[i]->As<GFX::SendPushConstantsCommand>();
            REQUIRE( command->_uniformData == expectedAddresses[i] );
            const auto& entries = command->_uniformData->entries();
            REQUIRE( entries.size() == 2u );
            REQUIRE( entries[0]._bindingHash == customBinding );
            REQUIRE( entries[1]._bindingHash == matrixBinding );
            U32 customValue = U32_MAX;
            std::memcpy( &customValue, command->_uniformData->data( entries[0]._range._startOffset ), sizeof(customValue) );
            CHECK( customValue == i );
            CHECK( std::memcmp( command->_uniformData->data( entries[1]._range._startOffset ),
                                &expectedMatrices[i], sizeof(expectedMatrices[i]) ) == 0 );
            for (U32 j = 0u; j < i; ++j)
            {
                CHECK( expectedAddresses[i] != expectedAddresses[j] );
            }
        }

        const Byte* firstStorage = expectedAddresses[0]->data( 0u );
        commands.clear();
        snapshots.record( 43u, defaults, [&](UniformData& uniforms)
        {
            CHECK( &uniforms == expectedAddresses[0] );
            uniforms.set( matrixBinding, PushConstantType::MAT4, expectedMatrices[0] );
            CHECK( uniforms.data( 0u ) == firstStorage );
        } );
    }

    TEST_CASE( "Immediate draws isolate concurrent view uniforms", "[render_smoke][uniforms][editor][threading]" )
    {
        UniformDataSnapshots snapshots{ 2u };
        const UniformData defaults;
        constexpr size_t viewCount = 8u;
        constexpr U64 viewBinding = _ID( "viewIndex" );
        std::array<UniformData*, viewCount> recorded{};
        std::array<std::thread, viewCount> workers;
        for (size_t i = 0u; i < viewCount; ++i)
        {
            workers[i] = std::thread( [&, i]
            {
                snapshots.record( 100u, defaults, [&](UniformData& uniforms)
                {
                    uniforms.set( viewBinding, PushConstantType::UINT, to_U32(i) );
                    recorded[i] = &uniforms;
                } );
            } );
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }

        for (size_t i = 0u; i < viewCount; ++i)
        {
            REQUIRE( recorded[i] != nullptr );
            REQUIRE( recorded[i]->entries().size() == 1u );
            const auto& entry = recorded[i]->entries().front();
            CHECK( entry._bindingHash == viewBinding );
            U32 view = U32_MAX;
            std::memcpy( &view, recorded[i]->data( entry._range._startOffset ), sizeof(view) );
            CHECK( view == i );
            for (size_t j = 0u; j < i; ++j)
            {
                CHECK( recorded[i] != recorded[j] );
            }
        }
    }

    TEST_CASE( "HiZ view uniforms retain their entity counts and frusta until translation", "[renderer][hiz][uniforms][render_smoke]" )
    {
        UniformDataSnapshots snapshots;
        const UniformData defaults;
        GFX::CommandBuffer commands;
        constexpr U64 countBinding = _ID( "dvd_numEntities" );
        constexpr U64 planesBinding = _ID( "dvd_frustumPlanes" );
        constexpr size_t viewCount = 8u;
        std::array<std::array<float4, 6u>, viewCount> planes{};
        for ( size_t view = 0u; view < viewCount; ++view )
        {
            planes[view].fill( float4{ to_F32( view ), 2.f, 3.f, 4.f } );
            snapshots.record( 17u, defaults, [&]( UniformData& uniforms )
            {
                uniforms.set( countBinding, PushConstantType::UINT, to_U32( view + 1u ) );
                uniforms.set( planesBinding, PushConstantType::VEC4, planes[view] );
                GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( commands )->_uniformData = &uniforms;
            } );
        }
        REQUIRE( commands.commands().size() == viewCount );
        for ( size_t view = 0u; view < viewCount; ++view )
        {
            const auto* command = commands.commands()[view]->As<GFX::SendPushConstantsCommand>();
            const auto& entries = command->_uniformData->entries();
            REQUIRE( entries.size() == 2u );
            REQUIRE( entries[0]._bindingHash == countBinding );
            REQUIRE( entries[1]._bindingHash == planesBinding );
            U32 count = 0u;
            std::memcpy( &count, command->_uniformData->data( entries[0]._range._startOffset ), sizeof(count) );
            CHECK( count == view + 1u );
            CHECK( std::memcmp( command->_uniformData->data( entries[1]._range._startOffset ),
                                planes[view].data(), sizeof(planes[view]) ) == 0 );
        }
        CHECK( defaults.entries().empty() );
    }

    TEST_CASE( "Additional render commands retain their package's uniform storage", "[render_smoke][uniforms]" )
    {
        RenderPackage package;
        RenderPackage otherPackage;
        GFX::CommandBuffer commands;
        constexpr U64 chunkBinding = _ID( "dvd_terrainChunkOffset" );

        {
            GFX::SendPushConstantsCommand cull = package.additionalPushConstantsCmd();
            REQUIRE( cull._uniformData != nullptr );
            CHECK( cull._uniformData == package.pushConstantsCmd()._uniformData );
            CHECK( cull._uniformData != otherPackage.additionalPushConstantsCmd()._uniformData );
            cull._uniformData->set( chunkBinding, PushConstantType::UINT, 7u );
            GFX::EnqueueCommand( commands, cull );
        }

        REQUIRE( commands.commands().size() == 1u );
        const auto* queued = commands.commands().front()->As<GFX::SendPushConstantsCommand>();
        REQUIRE( queued->_uniformData != nullptr );
        REQUIRE( queued->_uniformData->entries().size() == 1u );
        const UniformData::Entry& entry = queued->_uniformData->entries().front();
        CHECK( entry._bindingHash == chunkBinding );
        U32 chunkIndex = 0u;
        std::memcpy( &chunkIndex, queued->_uniformData->data( entry._range._startOffset ), sizeof( chunkIndex ) );
        CHECK( chunkIndex == 7u );

        Clear( package );
        CHECK( package.additionalPushConstantsCmd()._uniformData == package.pushConstantsCmd()._uniformData );
    }

    TEST_CASE( "Render package preparation is copied separately from draw commands", "[render_smoke][vegetation][commands]" )
    {
        GFX::InitPools( 4u );
        SCOPE_EXIT
        {
            GFX::DestroyPools();
        };

        RenderPackage package;
        GFX::CommandBuffer pass;
        AppendPreparationCommands( package, pass );
        CHECK( pass.commands().empty() );

        const Handle<GFX::CommandBuffer> prepHandle = GetPreparationCommandBuffer( package );
        REQUIRE( prepHandle == GetPreparationCommandBuffer( package ) );
        GFX::CommandBuffer* preparation = GFX::Get( prepHandle );
        REQUIRE( preparation != nullptr );
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>( *preparation )->_workGroupSize.set( 1u, 1u, 1u );
        AppendPreparationCommands( package, pass );

        GFX::CommandBuffer* draw = GFX::Get( GetCommandBuffer( package ) );
        REQUIRE( draw != nullptr );
        GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( *draw );
        preparation->clear();

        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( pass );
        pass.add( *draw );
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( pass );
        REQUIRE( pass.commands().size() == 4u );
        CHECK( pass.commands()[0]->type() == GFX::CommandType::DISPATCH_SHADER_TASK );
        CHECK( pass.commands()[1]->type() == GFX::CommandType::BEGIN_RENDER_PASS );
        CHECK( pass.commands()[2]->type() == GFX::CommandType::BIND_SHADER_RESOURCES );
        CHECK( pass.commands()[3]->type() == GFX::CommandType::END_RENDER_PASS );

        Clear( package );
        GFX::CommandBuffer afterClear;
        AppendPreparationCommands( package, afterClear );
        CHECK( afterClear.commands().empty() );
    }

    TEST_CASE("Vegetation occlusion commands remain outside depth and colour passes", "[renderer][vegetation][commands]")
    {
        GFX::InitPools(4u);
        SCOPE_EXIT { GFX::DestroyPools(); };
        RenderPackage package;
        GFX::CommandBuffer view;
        AppendOcclusionCommands(package, view);
        CHECK(view.commands().empty());
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(*GFX::Get(GetPreparationCommandBuffer(package)))->_workGroupSize.x = 1u;
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(*GFX::Get(GetOcclusionCommandBuffer(package)))->_workGroupSize.x = 2u;
        AppendPreparationCommands(package, view);
        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(view);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(view);
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(view)->_workGroupSize.x = 3u; // Current-view Hi-Z.
        AppendOcclusionCommands(package, view);
        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(view);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(view);
        GFX::Get(GetOcclusionCommandBuffer(package))->clear();
        REQUIRE(view.commands().size() == 7u);
        CHECK(view.commands()[0]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 1u);
        CHECK(view.commands()[3]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 3u);
        CHECK(view.commands()[4]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 2u);
        Clear(package);
        GFX::CommandBuffer afterClear;
        AppendOcclusionCommands(package, afterClear);
        CHECK(afterClear.commands().empty());
    }

    namespace
    {
        void RecordIndependentCompute( RenderPackage& package, LockableBuffer& buffer,
                                       const BufferRange<> range, const U32 dispatch, const bool occlusion = false )
        {
            package.independentCompute( true );
            GFX::CommandBuffer& commands = *GFX::Get( occlusion ? GetOcclusionCommandBuffer( package )
                                                               : GetPreparationCommandBuffer( package ) );
            auto* before = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( commands );
            before->_bufferLocks.push_back( { range, BufferSyncUsage::GPU_READ_TO_GPU_WRITE, &buffer } );
            before->_bufferLocks.push_back( { range, BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE, &buffer } );
            GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>( commands )->_workGroupSize.set( dispatch, 1u, 1u );
            auto* after = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( commands );
            after->_bufferLocks.push_back( { range, BufferSyncUsage::GPU_WRITE_TO_GPU_READ, &buffer } );
        }
    }

    TEST_CASE( "Independent compute batches retain every disjoint range and dispatch", "[render_smoke][vegetation][compute_batch]" )
    {
        GFX::InitPools( 4u );
        SCOPE_EXIT { GFX::DestroyPools(); };
        LockableBuffer grass;
        LockableBuffer trees;
        RenderPackage first;
        RenderPackage second;
        RenderPackage gap;
        RenderPackage otherBuffer;
        RenderPackage empty;
        RecordIndependentCompute( first, grass, { 0u, 16u }, 1u );
        RecordIndependentCompute( second, grass, { 24u, 8u }, 2u );
        RecordIndependentCompute( gap, grass, { 16u, 8u }, 3u );
        RecordIndependentCompute( otherBuffer, trees, { 0u, 16u }, 4u );
        const std::array packages{ &first, &empty, &second, &gap, &otherBuffer };
        BufferLocks scratch;
        GFX::CommandBuffer view;
        AppendPreparationCommands( packages, view, scratch );

        REQUIRE( view.commands().size() == 6u );
        REQUIRE( view.commands().front()->type() == GFX::CommandType::MEMORY_BARRIER );
        REQUIRE( view.commands().back()->type() == GFX::CommandType::MEMORY_BARRIER );
        for ( U32 i = 1u; i <= 4u; ++i )
        {
            REQUIRE( view.commands()[i]->type() == GFX::CommandType::DISPATCH_SHADER_TASK );
            CHECK( view.commands()[i]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == i );
        }
        const auto* before = view.commands().front()->As<GFX::MemoryBarrierCommand>();
        const auto* after = view.commands().back()->As<GFX::MemoryBarrierCommand>();
        REQUIRE( before->_bufferLocks.size() == 4u );
        REQUIRE( after->_bufferLocks.size() == 2u );
        for ( const BufferLock& lock : before->_bufferLocks )
        {
            CHECK( (lock._type == BufferSyncUsage::GPU_READ_TO_GPU_WRITE ||
                    lock._type == BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE) );
            CHECK( lock._range == (lock._buffer == &grass ? BufferRange<>{ 0u, 32u } : BufferRange<>{ 0u, 16u }) );
        }
        for ( const BufferLock& lock : after->_bufferLocks )
        {
            CHECK( lock._type == BufferSyncUsage::GPU_WRITE_TO_GPU_READ );
            CHECK( lock._range == (lock._buffer == &grass ? BufferRange<>{ 0u, 32u } : BufferRange<>{ 0u, 16u }) );
        }
        CHECK( scratch.empty() );
        GFX::Get( GetPreparationCommandBuffer( first ) )->clear();
        CHECK( view.commands().size() == 6u );

        Clear( first );
        CHECK_FALSE( first.independentCompute() );
    }

    TEST_CASE( "Completed view slots transfer commands in original GPU order", "[render_smoke][renderer][render_stage]" )
    {
        GFX::CommandBuffer destination;
        std::array<GFX::CommandBuffer, 5u> slots;
        const auto append = []( GFX::CommandBuffer& commands, const U32 value )
        {
            GFX::EnqueueCommand<GFX::SetViewportCommand>( commands )->_viewport = Rect<I32>{ to_I32( value ), 0, 100, 100 };
        };
        append( destination, 10u );
        append( destination, 20u );
        append( destination, 30u );
        append( slots[0], 1u );
        append( slots[1], 2u );
        append( slots[3], 3u );
        append( slots[4], 4u );
        const auto* transferred = slots[0].commands().front();
        constexpr std::array<size_t, 5u> positions{{ 0u, 1u, 1u, 1u, 3u }};
        for ( size_t i = slots.size(); i > 0u; --i )
        {
            destination.insert( MOV( slots[i - 1u] ), positions[i - 1u] );
            CHECK( slots[i - 1u].commands().empty() );
        }
        REQUIRE( destination.commands().size() == 7u );
        CHECK( destination.commands().front() == transferred );
        constexpr std::array<U32, 7u> expected{{ 1u, 10u, 2u, 3u, 20u, 30u, 4u }};
        destination.batch();
        REQUIRE( destination.commands().size() == expected.size() );
        for ( size_t i = 0u; i < expected.size(); ++i )
        {
            CHECK( destination.commands()[i]->As<GFX::SetViewportCommand>()->_viewport.x == to_I32( expected[i] ) );
        }
        append( slots[0], 5u );
        destination.insert( MOV( slots[0] ), destination.commands().size() );
        CHECK( destination.commands().back()->As<GFX::SetViewportCommand>()->_viewport.x == 5 );
    }

    TEST_CASE( "View packages own independent LoD selections", "[render_smoke][renderer][render_stage]" )
    {
        RenderPackage first;
        RenderPackage second;
        CHECK( first.lodLevel() == 0u );
        first.lodLevel( 2u );
        second.lodLevel( 3u );
        CHECK( first.lodLevel() == 2u );
        CHECK( second.lodLevel() == 3u );
    }

    TEST_CASE( "Command-buffer range copies participate in batching", "[render_smoke][vegetation][compute_batch]" )
    {
        LockableBuffer buffer;
        GFX::CommandBuffer source;
        for ( size_t i = 0u; i < 2u; ++i )
        {
            auto* barrier = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( source );
            barrier->_bufferLocks.push_back( { { i * 16u, 16u }, BufferSyncUsage::GPU_WRITE_TO_GPU_READ, &buffer } );
        }
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>( source )->_workGroupSize.x = 1u;
        GFX::CommandBuffer copy;
        copy.add( source, 0u, 2u );
        source.clear();
        REQUIRE( copy.commands().size() == 2u );
        copy.batch();
        REQUIRE( copy.commands().size() == 1u );
        CHECK( copy.commands().front()->As<GFX::MemoryBarrierCommand>()->_bufferLocks.front()._range ==
               (BufferRange<>{ 0u, 32u }) );
    }

    TEST_CASE( "Overlapping and ordinary compute packages retain their ordering", "[render_smoke][vegetation][compute_batch]" )
    {
        GFX::InitPools( 4u );
        SCOPE_EXIT { GFX::DestroyPools(); };
        LockableBuffer buffer;
        RenderPackage first;
        RenderPackage second;
        RenderPackage third;
        RecordIndependentCompute( first, buffer, { 0u, 16u }, 1u );
        RecordIndependentCompute( second, buffer, { 8u, 16u }, 2u );
        RecordIndependentCompute( third, buffer, { 24u, 8u }, 3u );
        BufferLocks scratch;
        GFX::CommandBuffer view;

        SECTION( "overlap separates batches but adjacent later ranges can share barriers" )
        {
            const std::array packages{ &first, &second, &third };
            AppendPreparationCommands( packages, view, scratch );
            REQUIRE( view.commands().size() == 7u );
            CHECK( view.commands()[0]->type() == GFX::CommandType::MEMORY_BARRIER );
            CHECK( view.commands()[1]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 1u );
            CHECK( view.commands()[2]->type() == GFX::CommandType::MEMORY_BARRIER );
            CHECK( view.commands()[3]->type() == GFX::CommandType::MEMORY_BARRIER );
            CHECK( view.commands()[4]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 2u );
            CHECK( view.commands()[5]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 3u );
            CHECK( view.commands()[6]->type() == GFX::CommandType::MEMORY_BARRIER );
            CHECK( view.commands()[6]->As<GFX::MemoryBarrierCommand>()->_bufferLocks.front()._range == (BufferRange<>{ 8u, 24u }) );
        }

        SECTION( "a non-independent command is not crossed" )
        {
            second.independentCompute( false );
            const std::array packages{ &first, &second, &third };
            AppendPreparationCommands( packages, view, scratch );
            REQUIRE( view.commands().size() == 9u );
            for ( size_t i = 0u; i < 3u; ++i )
            {
                CHECK( view.commands()[i * 3u]->type() == GFX::CommandType::MEMORY_BARRIER );
                CHECK( view.commands()[i * 3u + 1u]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == i + 1u );
                CHECK( view.commands()[i * 3u + 2u]->type() == GFX::CommandType::MEMORY_BARRIER );
            }
        }
    }

    TEST_CASE( "Independent occlusion stays after the current view's Hi-Z", "[render_smoke][vegetation][compute_batch]" )
    {
        GFX::InitPools( 4u );
        SCOPE_EXIT { GFX::DestroyPools(); };
        LockableBuffer buffer;
        RenderPackage first;
        RenderPackage second;
        RecordIndependentCompute( first, buffer, { 0u, 16u }, 1u );
        RecordIndependentCompute( second, buffer, { 16u, 16u }, 2u );
        RecordIndependentCompute( first, buffer, { 0u, 16u }, 3u, true );
        RecordIndependentCompute( second, buffer, { 16u, 16u }, 4u, true );
        const std::array packages{ &first, &second };
        BufferLocks scratch;
        GFX::CommandBuffer view;
        AppendPreparationCommands( packages, view, scratch );
        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( view );
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( view );
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>( view )->_workGroupSize.x = 5u;
        AppendOcclusionCommands( packages, view, scratch );
        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( view );
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( view );

        REQUIRE( view.commands().size() == 13u );
        CHECK( view.commands()[1]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 1u );
        CHECK( view.commands()[2]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 2u );
        CHECK( view.commands()[4]->type() == GFX::CommandType::BEGIN_RENDER_PASS );
        CHECK( view.commands()[5]->type() == GFX::CommandType::END_RENDER_PASS );
        CHECK( view.commands()[6]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 5u );
        CHECK( view.commands()[7]->type() == GFX::CommandType::MEMORY_BARRIER );
        CHECK( view.commands()[8]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 3u );
        CHECK( view.commands()[9]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 4u );
        CHECK( view.commands()[10]->type() == GFX::CommandType::MEMORY_BARRIER );
        CHECK( view.commands()[11]->type() == GFX::CommandType::BEGIN_RENDER_PASS );
        CHECK( view.commands()[12]->type() == GFX::CommandType::END_RENDER_PASS );
    }

    TEST_CASE( "Unbatchable compute barriers preserve their original commands", "[renderer][vegetation][compute_batch]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        GFX::InitPools( 4u );
        SCOPE_EXIT { GFX::DestroyPools(); };
        LockableBuffer buffer;
        RenderPackage invalid;
        RenderPackage valid;
        RecordIndependentCompute( invalid, buffer, { 0u, 16u }, 1u );
        RecordIndependentCompute( valid, buffer, { 32u, 16u }, 2u );
        auto* source = GFX::Get( GetPreparationCommandBuffer( invalid ) );
        auto* before = source->commands().front()->As<GFX::MemoryBarrierCommand>();
        auto* after = source->commands().back()->As<GFX::MemoryBarrierCommand>();
        SECTION( "a whole-buffer sentinel is not widened" )
        {
            for ( BufferLock& lock : before->_bufferLocks )
            {
                lock._range = { 0u, U32_MAX };
            }
            after->_bufferLocks.front()._range = { 0u, U32_MAX };
        }
        SECTION( "an overflowing range is not merged" )
        {
            for ( BufferLock& lock : before->_bufferLocks )
            {
                lock._range = { SIZE_MAX - 4u, 8u };
            }
            after->_bufferLocks.front()._range = { SIZE_MAX - 4u, 8u };
        }
        SECTION( "unmatched post-compute ranges are not reordered" )
        {
            after->_bufferLocks.front()._range = { 0u, 8u };
        }
        const std::array packages{ &invalid, &valid };
        BufferLocks scratch;
        GFX::CommandBuffer view;
        AppendPreparationCommands( packages, view, scratch );
        REQUIRE( view.commands().size() == 6u );
        CHECK( view.commands()[0]->As<GFX::MemoryBarrierCommand>()->_bufferLocks.front()._range ==
               before->_bufferLocks.front()._range );
        CHECK( view.commands()[1]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 1u );
        CHECK( view.commands()[2]->As<GFX::MemoryBarrierCommand>()->_bufferLocks.front()._range ==
               after->_bufferLocks.front()._range );
        CHECK( view.commands()[4]->As<GFX::DispatchShaderTaskCommand>()->_workGroupSize.x == 2u );
    }

    TEST_CASE("Hi-Z occlusion keeps near-plane intersections and equal-depth surfaces", "[renderer][vegetation][hiz]")
    {
        CHECK_FALSE(HIZ_PROJECTABLE_CORNER(-1.f, 0.5f));
        CHECK_FALSE(HIZ_PROJECTABLE_CORNER(0.f, 0.5f));
        CHECK_FALSE(HIZ_PROJECTABLE_CORNER(1.f, 0.f));
        CHECK_FALSE(HIZ_PROJECTABLE_CORNER(1.f, -0.1f));
        CHECK(HIZ_PROJECTABLE_CORNER(1.f, 0.5f));
        CHECK_FALSE(HIZ_OCCLUDED(0.5f, 0.5f));
        CHECK_FALSE(HIZ_OCCLUDED(0.500001f, 0.5f));
        CHECK_FALSE(HIZ_OCCLUDED(0.1f, 0.5f));
        CHECK_FALSE(HIZ_OCCLUDED(0.99f, 1.f));
        CHECK(HIZ_OCCLUDED(0.8f, 0.5f));
    }

    TEST_CASE("Hi-Z four-tap footprints cover rectangular and odd-sized pyramids", "[renderer][vegetation][hiz]")
    {
        using std::ceil;
        using std::floor;
        using std::log2;
        using std::min;
        using std::max;
        for (const U32 size : {1u, 3u, 5u, 719u, 1279u, 1920u})
        {
            for (const F32 width : {0.f, 0.00001f, 0.001f, 0.1f, 0.5f, 1.f})
            {
                const U32 mip = to_U32(HIZ_MIP_LEVEL(width * to_F32(size)));
                const F32 dimension = to_F32(max(size >> mip, 1u));
                for (const F32 start : {0.f, 0.2f, 0.51f, 0.99f})
                {
                    const F32 end = min(start + width, 1.f);
                    const F32 first = HIZ_TEXEL(start, dimension);
                    const F32 last = HIZ_TEXEL(end, dimension);
                    CHECK(first >= 0.f);
                    CHECK(last < dimension);
                    CHECK(HIZ_FOUR_TAP_COVERS(first, last));
                }
            }
        }
        CHECK_FALSE(HIZ_FOUR_TAP_COVERS(0, 2));
    }

    TEST_CASE( "Visible tree consumers schedule their preparation owner once", "[render_smoke][vegetation][commands]" )
    {
        struct Owner {};
        struct Package {};
        Owner grassAndTrees;
        Owner treesOnly;
        Package preparedGrass;
        vector<std::pair<Owner*, Package*>> owners{
            { &treesOnly, nullptr },
            { &grassAndTrees, nullptr },
            { &treesOnly, nullptr },
            { &grassAndTrees, &preparedGrass },
            { &grassAndTrees, nullptr }
        };
        vector<std::pair<Owner*, Package*>> selected;

        RenderPassExecutor::VisitPreparationOwners( owners, [&]( Owner& owner, Package* prepared )
        {
            selected.emplace_back( &owner, prepared );
        } );

        REQUIRE( selected.size() == 2u );
        CHECK( std::ranges::count_if( selected, [&]( const auto& entry )
        {
            return entry.first == &grassAndTrees && entry.second == &preparedGrass;
        } ) == 1 );
        CHECK( std::ranges::count_if( selected, [&]( const auto& entry )
        {
            return entry.first == &treesOnly && entry.second == nullptr;
        } ) == 1 );
    }

    TEST_CASE( "Vulkan attachment selections address cubemap faces and array layers", "[render_smoke][render_target]" )
    {
        SubRange range{};
        for ( U16 cube = 0u; cube < 7u; ++cube )
        {
            for ( U8 face = 0u; face < 6u; ++face )
            {
                const DrawLayerEntry entry{ ._layer = { cube, 1u }, ._cubeFace = face };
                REQUIRE( vkRenderTarget::GetAttachmentLayerRange( entry, 7u, true, range ) );
                CHECK( range._offset == cube * 6u + face );
                CHECK( range._count == 1u );
            }
        }

        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, ALL_LAYERS } }, 7u, true, range ) );
        CHECK( (range == SubRange{ 0u, 42u }) );
        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 2u, ALL_LAYERS } }, 7u, true, range ) );
        CHECK( (range == SubRange{ 12u, 30u }) );
        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 2u, 3u } }, 7u, true, range ) );
        CHECK( (range == SubRange{ 12u, 18u }) );
        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 2u, ALL_LAYERS } }, 7u, false, range ) );
        CHECK( (range == SubRange{ 2u, 5u }) );
        CHECK( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 6u, 1u } }, 7u, false, range ) );
        CHECK( (range == SubRange{ 6u, 1u }) );

        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 7u, 1u } }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 6u, 2u } }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, 0u } }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, 1u }, ._cubeFace = 6u }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, 2u }, ._cubeFace = 1u }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, ALL_LAYERS }, ._cubeFace = 1u }, 7u, true, range ) );
        CHECK_FALSE( vkRenderTarget::GetAttachmentLayerRange( { ._layer = { 0u, 1u }, ._cubeFace = 1u }, 7u, false, range ) );
    }

    TEST_CASE( "Render package uniforms remain stable when their owning collection grows", "[render_smoke][uniforms]" )
    {
        STATIC_REQUIRE_FALSE( std::is_copy_constructible_v<RenderPackage> );
        STATIC_REQUIRE_FALSE( std::is_move_constructible_v<RenderPackage> );

        vector<std::unique_ptr<RenderPackage>> packages;
        packages.emplace_back( std::make_unique<RenderPackage>() );
        RenderPackage& package = *packages.front();
        GFX::CommandBuffer commands;
        GFX::SendPushConstantsCommand constants = package.additionalPushConstantsCmd();
        constexpr U64 binding = _ID( "dvd_terrainChunkOffset" );
        constants._uniformData->set( binding, PushConstantType::UINT, 7u );
        GFX::EnqueueCommand( commands, constants );

        for ( U32 i = 0u; i < 128u; ++i )
        {
            packages.emplace_back( std::make_unique<RenderPackage>() );
        }

        REQUIRE( commands.commands().size() == 1u );
        const auto* queued = commands.commands().front()->As<GFX::SendPushConstantsCommand>();
        REQUIRE( queued->_uniformData == packages.front()->pushConstantsCmd()._uniformData );
        REQUIRE( queued->_uniformData->entries().size() == 1u );
        CHECK( queued->_uniformData->entries().front()._bindingHash == binding );
    }

    TEST_CASE( "Instanced pass packages keep their node indirection index during initialization", "[renderer][indirection][uniforms]" )
    {
        constexpr U32 nodeIndex = 37u;
        std::array<RenderPackage, 3u> packages;
        for ( RenderPackage& package : packages )
        {
            SetNodeIndirectionIndex( package, true, nodeIndex );
            const GFX::SendPushConstantsCommand recorded = package.pushConstantsCmd();

            SetNodeIndirectionIndex( package, true, nodeIndex );
            REQUIRE( recorded._uniformData != nullptr );
            REQUIRE( recorded._uniformData->entries().size() == 1u );
            const UniformData::Entry& entry = recorded._uniformData->entries().front();
            CHECK( entry._bindingHash == _ID( "INDIRECT_DATA_IDX" ) );
            CHECK( entry._type == PushConstantType::UINT );
            REQUIRE( entry._range._length == sizeof( U32 ) );
            U32 stored = U32_MAX;
            std::memcpy( &stored, recorded._uniformData->data( entry._range._startOffset ), sizeof( stored ) );
            CHECK( stored == nodeIndex );

            Clear( package );
            CHECK( package.pushConstantsCmd()._fastData.dataFlags() == PushConstantsStruct::DataFlags::NONE );
            SetNodeIndirectionIndex( package, true, 0u );
            std::memcpy( &stored, package.pushConstantsCmd()._uniformData->data( entry._range._startOffset ), sizeof( stored ) );
            CHECK( stored == 0u );
        }

        RenderPackage ordinary;
        SetNodeIndirectionIndex( ordinary, false, NodeIndirectionData::INVALID_IDX );
        CHECK( ordinary.pushConstantsCmd()._uniformData->entries().empty() );
        CHECK( ordinary.pushConstantsCmd()._fastData.dataFlags() == PushConstantsStruct::DataFlags::NONE );
    }

    TEST_CASE( "Buffer metrics support concurrent allocation accounting and snapshots", "[renderer][threading][metrics]" )
    {
        PerformanceMetrics metrics;
        metrics._gpuTimeInMS = 1.5f;
        metrics._syncObjectsInFlight[1] = 9u;
        constexpr size_t threadCount = 8u;
        constexpr size_t iterations = 4096u;
        std::array<std::thread, threadCount> workers;
        for ( size_t worker = 0u; worker < threadCount; ++worker )
        {
            workers[worker] = std::thread( [&, worker]
            {
                for ( size_t i = 0u; i < iterations; ++i )
                {
                    metrics._bufferVRAMUsage += 3u;
                    metrics._gpuBufferCount = worker + 1u;
                }
            } );
        }
        for ( std::thread& worker : workers )
        {
            worker.join();
        }

        CHECK( metrics._bufferVRAMUsage.load() == threadCount * iterations * 3u );
        CHECK( metrics._gpuBufferCount.load() >= 1u );
        CHECK( metrics._gpuBufferCount.load() <= threadCount );

        const PerformanceMetrics snapshot = metrics;
        metrics._bufferVRAMUsage -= 3u;
        metrics._gpuBufferCount = 0u;
        CHECK( snapshot._bufferVRAMUsage.load() == threadCount * iterations * 3u );
        CHECK( snapshot._gpuBufferCount.load() >= 1u );
        CHECK( snapshot._gpuTimeInMS == 1.5f );
        CHECK( snapshot._syncObjectsInFlight[1] == 9u );

        PerformanceMetrics assigned;
        assigned = snapshot;
        CHECK( assigned._bufferVRAMUsage.load() == snapshot._bufferVRAMUsage.load() );
        CHECK( assigned._gpuBufferCount.load() == snapshot._gpuBufferCount.load() );
        CHECK( assigned._gpuTimeInMS == snapshot._gpuTimeInMS );
        CHECK( assigned._syncObjectsInFlight[1] == snapshot._syncObjectsInFlight[1] );
    }

    TEST_CASE( "File texture arrays validate loaded layers against the descriptor", "[render_smoke][texture]" )
    {
        TextureDescriptor descriptor{};
        descriptor._texType = TextureType::TEXTURE_2D_ARRAY;
        descriptor._layerCount = 8u;
        CHECK( Texture::HasValidFileLayerCount( descriptor, 8u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 7u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 1u ) );

        descriptor._texType = TextureType::TEXTURE_1D_ARRAY;
        descriptor._layerCount = 2u;
        CHECK( Texture::HasValidFileLayerCount( descriptor, 2u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 1u ) );

        descriptor._texType = TextureType::TEXTURE_CUBE_ARRAY;
        CHECK( Texture::HasValidFileLayerCount( descriptor, 6u ) );
        CHECK( Texture::HasValidFileLayerCount( descriptor, 12u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 0u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 7u ) );
        CHECK( Texture::TextureLayersForImageLayers( descriptor._texType, 6u ) == 1u );
        CHECK( Texture::TextureLayersForImageLayers( descriptor._texType, 12u ) == 2u );

        descriptor._texType = TextureType::TEXTURE_2D;
        CHECK( Texture::HasValidFileLayerCount( descriptor, 1u ) );
        CHECK_FALSE( Texture::HasValidFileLayerCount( descriptor, 2u ) );
        CHECK( Texture::TextureLayersForImageLayers( TextureType::TEXTURE_2D_ARRAY, 8u ) == 8u );
    }

    TEST_CASE( "Texture views preserve cube and array layer ranges", "[render_smoke][texture]" )
    {
        SubRange range{};
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_CUBE_ARRAY, 7u,
                                          { 0u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 0u, 7u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_CUBE_ARRAY, 7u,
                                          { 2u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 2u, 5u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_CUBE_MAP, 7u,
                                          { 6u, 1u }, range ) );
        CHECK( (range == SubRange{ 6u, 1u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_2D_ARRAY, 7u,
                                          { 0u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 0u, 42u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_2D, 7u,
                                          { 37u, 1u }, range ) );
        CHECK( (range == SubRange{ 37u, 1u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_2D_ARRAY, TextureType::TEXTURE_2D_ARRAY, 8u,
                                          { 3u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 3u, 5u }) );
        CHECK( Texture::GetViewLayerRange( TextureType::TEXTURE_3D, TextureType::TEXTURE_3D, 1u,
                                          { 0u, ALL_LAYERS }, range ) );
        CHECK( (range == SubRange{ 0u, 1u }) );
        CHECK_FALSE( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_CUBE_ARRAY, 7u,
                                                { 7u, 1u }, range ) );
        CHECK_FALSE( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_2D_ARRAY, 7u,
                                                { 41u, 2u }, range ) );
        CHECK_FALSE( Texture::GetViewLayerRange( TextureType::TEXTURE_CUBE_ARRAY, TextureType::TEXTURE_2D, 7u,
                                                { 0u, 2u }, range ) );
        CHECK_FALSE( Texture::GetViewLayerRange( TextureType::TEXTURE_2D_ARRAY, TextureType::TEXTURE_2D_ARRAY, 8u,
                                                { 0u, 0u }, range ) );
    }

    TEST_CASE( "Vulkan view barriers use the view's layer units", "[render_smoke][texture]" )
    {
        ImageSubRange input{ ._mipLevels = { 1u, 2u }, ._layerRange = { 2u, 3u } };
        VkImageSubresourceRange range = vkTexture::GetViewSubresourceRange(
            TextureType::TEXTURE_CUBE_ARRAY, input, VK_IMAGE_ASPECT_COLOR_BIT );
        CHECK( range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT );
        CHECK( range.baseMipLevel == 1u );
        CHECK( range.levelCount == 2u );
        CHECK( range.baseArrayLayer == 12u );
        CHECK( range.layerCount == 18u );

        input._mipLevels = { 0u, ALL_MIPS };
        input._layerRange = { 37u, 1u };
        range = vkTexture::GetViewSubresourceRange( TextureType::TEXTURE_2D_ARRAY, input, VK_IMAGE_ASPECT_DEPTH_BIT );
        CHECK( range.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT );
        CHECK( range.levelCount == VK_REMAINING_MIP_LEVELS );
        CHECK( range.baseArrayLayer == 37u );
        CHECK( range.layerCount == 1u );

        input._layerRange = { 2u, ALL_LAYERS };
        range = vkTexture::GetViewSubresourceRange( TextureType::TEXTURE_CUBE_ARRAY, input, VK_IMAGE_ASPECT_COLOR_BIT );
        CHECK( range.baseArrayLayer == 12u );
        CHECK( range.layerCount == VK_REMAINING_ARRAY_LAYERS );
    }

    TEST_CASE( "Immediate submission failures stop later work until reinitialization", "[render_smoke][submission]" )
    {
        VKImmediateSubmitStatus status{};
        CHECK_FALSE( status.failed() );
        CHECK( status.record( VK_SUCCESS ) );
        CHECK_FALSE( status.failed() );
        CHECK_FALSE( status.record( VK_ERROR_OUT_OF_HOST_MEMORY ) );
        CHECK( status.failed() );
        CHECK( status.record( VK_SUCCESS ) );
        CHECK( status.failed() );
        status.reset();
        CHECK_FALSE( status.failed() );
        CHECK_FALSE( status.record( VK_ERROR_DEVICE_LOST ) );
        CHECK( status.failed() );
    }

    TEST_CASE( "Per-node dirty ranges remain pending independently in every ring slot", "[render_smoke][per_node]" )
    {
        using Range = RenderPassExecutor::BufferUpdateRange;
        std::array<Range, 4> slots{};
        Range pending{ 0u, 9u };

        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 1u ) == Range{ 0u, 9u }) );
        CHECK( pending.range() == 0u );
        CHECK( slots[1].range() == 0u );
        CHECK( (slots[0] == Range{ 0u, 9u }) );
        CHECK( (slots[2] == Range{ 0u, 9u }) );
        CHECK( (slots[3] == Range{ 0u, 9u }) );

        pending = { 20u, 23u };
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 1u ) == Range{ 20u, 23u }) );
        CHECK( (slots[0] == Range{ 0u, 23u }) );
        CHECK( (slots[2] == Range{ 0u, 23u }) );
        CHECK( (slots[3] == Range{ 0u, 23u }) );
        CHECK( RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 1u ).range() == 0u );
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 2u ) == Range{ 0u, 23u }) );

        pending = { 4u, 5u };
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 2u ) == Range{ 4u, 5u }) );
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 0u ) == Range{ 0u, 23u }) );
        CHECK( (RenderPassExecutor::TakeBufferUpdateRange( pending, slots, 1u ) == Range{ 4u, 5u }) );
    }

    TEST_CASE( "Per-node upload starts align without dropping the dirty range", "[render_smoke][per_node]" )
    {
        using Range = RenderPassExecutor::BufferUpdateRange;
        Range aligned{ 5u, 7u };
        aligned.alignStart( 12u, 16u );
        CHECK( (aligned == Range{ 4u, 7u }) );
        CHECK( aligned.range() == 4u );

        aligned = { 7u, 8u };
        aligned.alignStart( 128u, 256u );
        CHECK( (aligned == Range{ 6u, 8u }) );
        aligned = { 3u, 3u };
        aligned.alignStart( 4u, 256u );
        CHECK( (aligned == Range{ 0u, 3u }) );
        aligned = { 0u, 3u };
        aligned.alignStart( 4u, 256u );
        CHECK( (aligned == Range{ 0u, 3u }) );
    }

    TEST_CASE( "Per-node player slots outlast Vulkan's bounded in-flight frames", "[render_smoke][per_node]" )
    {
        CHECK( VKSwapChain::FrameSlotCount() == Config::MAX_FRAMES_IN_FLIGHT );
        for ( U64 frame = 0u; frame < 20u; ++frame )
        {
            CHECK( VKSwapChain::FrameSlotIndex( frame ) == frame % Config::MAX_FRAMES_IN_FLIGHT );
        }
        CHECK( RenderPassExecutor::BufferQueueLengthForPlayers( 0u ) ==
               RenderPassExecutor::BufferQueueLengthForPlayers( 1u ) );

        for ( U8 players = 1u; players <= Config::MAX_LOCAL_PLAYER_COUNT; ++players )
        {
            const U16 length = RenderPassExecutor::BufferQueueLengthForPlayers( players );
            RingBuffer ring{ length };
            vector<I32> lastUsedFrame( length, -1 );
            for ( I32 frame = 0; frame < 20; ++frame )
            {
                for ( U8 player = 0u; player < players; ++player )
                {
                    ring.incQueue();
                    const U16 slot = ring.queueIndex();
                    if ( lastUsedFrame[slot] >= 0 )
                    {
                        CHECK( frame - lastUsedFrame[slot] >= Config::MAX_FRAMES_IN_FLIGHT );
                    }
                    lastUsedFrame[slot] = frame;
                }
            }
        }
    }

    TEST_CASE( "Separate-write ring indices stay in range and read the oldest write", "[renderer][ring_buffer]" )
    {
        constexpr U16 length = 6u;
        RingBufferSeparateWrite ahead{ length, true, true };
        RingBufferSeparateWrite behind{ length, true, false };
        vector<I32> writtenAt( length, -1 );
        for ( I32 frame = 0; frame < 3 * length; ++frame )
        {
            CHECK( ahead.queueWriteIndex() < length );
            CHECK( ahead.queueWriteIndex() == (ahead.queueReadIndex() + 1u) % length );
            REQUIRE( behind.queueWriteIndex() < length );
            writtenAt[behind.queueWriteIndex()] = frame;
            const I32 readFrame = writtenAt[behind.queueReadIndex()];
            if ( frame >= length )
            {
                CHECK( frame - readFrame == length - 1 );
            }
            ahead.incQueue();
            behind.incQueue();
        }
    }

    TEST_CASE( "GPU pass timings sum by command buffer name across frames", "[renderer][profiling]" )
    {
        using Name = GPUPassTimingTable::Name;
        GPUPassTimingTable table{};

        const std::array<Name, 3> names{ Name{ "Shadow" }, Name{ "Display" }, Name{ "Shadow" } };
        const std::array<F32, 4> frame0{ 0.f, 2.f, 5.f, 6.f };
        AccumulateGPUPassTimings( frame0, names, table );

        REQUIRE( table._frameCount == 1u );
        REQUIRE( table._entryCount == 2u );
        CHECK( table._entries[0]._name == Name{ "Shadow" } );
        CHECK( table._entries[0]._totalMS == Catch::Approx( 3.0 ) );
        CHECK( table._entries[1]._name == Name{ "Display" } );
        CHECK( table._entries[1]._totalMS == Catch::Approx( 3.0 ) );

        // Out-of-order timestamps clamp to zero instead of subtracting time.
        const std::array<F32, 4> frame1{ 0.f, 1.f, 0.5f, 1.5f };
        AccumulateGPUPassTimings( frame1, names, table );
        CHECK( table._frameCount == 2u );
        CHECK( table._entries[0]._totalMS == Catch::Approx( 5.0 ) );
        CHECK( table._entries[1]._totalMS == Catch::Approx( 3.0 ) );

        // Too few timestamps for the supplied names: the frame is ignored.
        const std::array<F32, 3> truncated{ 0.f, 1.f, 2.f };
        AccumulateGPUPassTimings( truncated, names, table );
        CHECK( table._frameCount == 2u );

        table.reset();
        CHECK( table._frameCount == 0u );
        CHECK( table._entryCount == 0u );
    }

    TEST_CASE( "GPU scope names drop per-instance decorations", "[renderer][profiling]" )
    {
        CHECK( NormalizeGPUScopeName( " - MainPass" ) == "MainPass" );
        CHECK( NormalizeGPUScopeName( "Custom pass ( SHADOW - Light 3 )" ) == "Custom pass" );
        CHECK( NormalizeGPUScopeName( "Post Render pass for stage [ DISPLAY ]" ) == "Post Render pass for stage" );
        CHECK( NormalizeGPUScopeName( "HiZ Construct & Cull" ) == "HiZ Construct & Cull" );
        CHECK( NormalizeGPUScopeName( "MainPass V1 L0 P0" ) == "MainPass V1 L0 P0" );
        CHECK( NormalizeGPUScopeName( "MainPass V1 L0 P1" ) != NormalizeGPUScopeName( "MainPass V1 L0 P0" ) );
        CHECK( NormalizeGPUScopeName( "MainPass V1 L8 P0" ) == "MainPass V1 L8 P0" );
        CHECK( NormalizeGPUScopeName( " - " ).empty() );
        CHECK( NormalizeGPUScopeName( "" ).empty() );
    }

    TEST_CASE( "Indirect command pass snapshots outlast in-flight frames", "[renderer][indirect][frame_lifetime]" )
    {
        using Tracker = RenderPassExecutor::IndirectCommandBufferTracker;
        constexpr size_t slotCount = Tracker::QUEUE_LENGTH;
        constexpr size_t passCount = 9u;
        constexpr size_t pageCount = 2u;
        constexpr size_t playerCount = Config::MAX_LOCAL_PLAYER_COUNT;
        std::array<Tracker, playerCount> trackers{};
        std::array<std::array<std::array<std::array<I32, passCount>, slotCount>, pageCount>, playerCount> lastUse{};
        for ( auto& player : lastUse )
        {
            for ( auto& page : player )
            {
                for ( auto& slot : page )
                {
                    slot.fill( -1 );
                }
            }
        }

        const std::array<U16, 5u> passes{ 2u, 1u, 0u, 8u, 1u };
        for ( I32 frame = 0; frame < 20; ++frame )
        {
            for ( size_t player = 0u; player < playerCount; ++player )
            {
                if ( player > 0u && frame % 3 == 1 )
                {
                    continue;
                }
                Tracker& tracker = trackers[player];
                tracker.beginFrame();
                U16 frameSlot = U16_MAX;
                for ( size_t i = 0u; i < passes.size(); ++i )
                {
                    const Tracker::Allocation allocation = tracker.acquire( passes[i], Config::MAX_VISIBLE_NODES );
                    CHECK( allocation._pageIndex == (i == passes.size() - 1u ? 1u : 0u) );
                    CHECK_FALSE( allocation._overflow );
                    REQUIRE( allocation._pageIndex < pageCount );
                    REQUIRE( allocation._queueIndex < slotCount );
                    if ( frameSlot == U16_MAX )
                    {
                        frameSlot = allocation._queueIndex;
                    }
                    CHECK( allocation._queueIndex == frameSlot );
                    I32& previous = lastUse[player][allocation._pageIndex][allocation._queueIndex][passes[i]];
                    if ( previous >= 0 )
                    {
                        CHECK( frame - previous > Config::MAX_FRAMES_IN_FLIGHT );
                    }
                    previous = frame;
                }
            }
        }
    }

    TEST_CASE( "Indirect command pages reset for sparse and repeated passes", "[renderer][indirect][frame_lifetime]" )
    {
        using Tracker = RenderPassExecutor::IndirectCommandBufferTracker;
        const std::array<std::array<U16, 6u>, 8u> framePasses{ {
            std::array<U16, 6u>{ { 2u, 1u, 0u, 8u, 1u, U16_MAX } },
            std::array<U16, 6u>{ { 0u, U16_MAX, U16_MAX, U16_MAX, U16_MAX, U16_MAX } },
            std::array<U16, 6u>{ { U16_MAX, U16_MAX, U16_MAX, U16_MAX, U16_MAX, U16_MAX } },
            std::array<U16, 6u>{ { 8u, 8u, 8u, U16_MAX, U16_MAX, U16_MAX } },
            std::array<U16, 6u>{ { 1u, 2u, U16_MAX, U16_MAX, U16_MAX, U16_MAX } },
            std::array<U16, 6u>{ { 2u, 1u, 0u, 8u, U16_MAX, U16_MAX } },
            std::array<U16, 6u>{ { 1u, 1u, 2u, 2u, U16_MAX, U16_MAX } },
            std::array<U16, 6u>{ { U16_MAX, U16_MAX, U16_MAX, U16_MAX, U16_MAX, U16_MAX } }
        } };

        Tracker tracker;
        for ( size_t frame = 0u; frame < framePasses.size() * 3u; ++frame )
        {
            tracker.beginFrame();
            std::array<size_t, 9u> passUses{};
            for ( const U16 pass : framePasses[frame % framePasses.size()] )
            {
                if ( pass == U16_MAX )
                {
                    continue;
                }
                const Tracker::Allocation allocation = tracker.acquire( pass, Config::MAX_VISIBLE_NODES );
                CHECK( allocation._pageIndex == passUses[pass]++ );
                CHECK_FALSE( allocation._overflow );
                CHECK( allocation._queueIndex == (frame + 1u) % Tracker::QUEUE_LENGTH );
            }
        }
    }

    TEST_CASE( "Oversized indirect passes use distinct reusable pages", "[renderer][indirect][frame_lifetime]" )
    {
        using Tracker = RenderPassExecutor::IndirectCommandBufferTracker;
        Tracker tracker{};
        std::array<std::array<I32, Tracker::QUEUE_LENGTH>, 2u> lastOverflowUse{};
        for ( auto& page : lastOverflowUse )
        {
            page.fill( -1 );
        }

        for ( I32 frame = 0; frame < 20; ++frame )
        {
            tracker.beginFrame();
            const Tracker::Allocation normal = tracker.acquire( 2u, Config::MAX_VISIBLE_NODES );
            const Tracker::Allocation firstOverflow = tracker.acquire( 2u, Config::MAX_VISIBLE_NODES + 1u );
            const Tracker::Allocation repeatedNormal = tracker.acquire( 2u, 1u );
            const Tracker::Allocation secondOverflow = tracker.acquire( 8u, Config::MAX_VISIBLE_NODES + 2u );
            const Tracker::Allocation otherNormal = tracker.acquire( 8u, 0u );

            CHECK_FALSE( normal._overflow );
            CHECK( normal._pageIndex == 0u );
            CHECK_FALSE( repeatedNormal._overflow );
            CHECK( repeatedNormal._pageIndex == 1u );
            CHECK_FALSE( otherNormal._overflow );
            CHECK( otherNormal._pageIndex == 0u );
            CHECK( firstOverflow._overflow );
            CHECK( firstOverflow._pageIndex == 0u );
            CHECK( secondOverflow._overflow );
            CHECK( secondOverflow._pageIndex == 1u );

            for ( const Tracker::Allocation allocation : { firstOverflow, secondOverflow } )
            {
                REQUIRE( allocation._queueIndex < Tracker::QUEUE_LENGTH );
                CHECK( allocation._queueIndex == normal._queueIndex );
                I32& previous = lastOverflowUse[allocation._pageIndex][allocation._queueIndex];
                if ( previous >= 0 )
                {
                    CHECK( frame - previous > Config::MAX_FRAMES_IN_FLIGHT );
                }
                previous = frame;
            }
        }
    }

    TEST_CASE( "Indirect command collection rebases oversized pass packages", "[renderer][indirect][frame_lifetime]" )
    {
        using Tracker = RenderPassExecutor::IndirectCommandBufferTracker;
        constexpr U16 passIndex = 5u;
        DrawCommandContainer commands{};
        std::array<RenderPackage, 3u> packages{};

        for ( const bool overflow : { false, true } )
        {
            const std::array<U32, 3u> perPassCounts{{
                Config::MAX_VISIBLE_NODES - 2u, overflow ? 2u : 1u, 1u
            }};
            U32 recordings = 0u;
            const U32 startOffset = Tracker::CollectCommands( passIndex, commands, [&]( const U32 baseOffset )
            {
                ++recordings;
                for ( size_t pass = 0u; pass < packages.size(); ++pass )
                {
                    packages[pass].drawCmdOffset( baseOffset + to_U32( commands.size() ) );
                    for ( U32 i = 0u; i < perPassCounts[pass]; ++i )
                    {
                        IndirectIndexedDrawCommand& cmd = commands.emplace_back();
                        cmd.indexCount = to_U32( pass ) + 1u;
                    }
                }
            } );

            const U32 expectedCount = Config::MAX_VISIBLE_NODES + (overflow ? 1u : 0u);
            CHECK( recordings == (overflow ? 2u : 1u) );
            CHECK( startOffset == (overflow ? 0u : Config::MAX_VISIBLE_NODES * passIndex) );
            REQUIRE( commands.size() == expectedCount );
            CHECK( packages[0].drawCmdOffset() == startOffset );
            CHECK( packages[1].drawCmdOffset() == startOffset + perPassCounts[0] );
            CHECK( packages[2].drawCmdOffset() == startOffset + perPassCounts[0] + perPassCounts[1] );
            CHECK( commands.front().indexCount == 1u );
            CHECK( commands[perPassCounts[0]].indexCount == 2u );
            CHECK( commands.back().indexCount == 3u );
        }

        constexpr U32 requiredCount = 8193u;
        const std::array<std::pair<size_t, U32>, 6u> alignments{{
            { 1u, 8193u }, { 4u, 8193u }, { 16u, 8196u },
            { 24u, 8196u }, { 256u, 8256u }, { 4096u, 9216u }
        }};
        for ( const auto& [alignment, expectedCapacity] : alignments )
        {
            CAPTURE( alignment );
            U32 capacity = 0u;
            REQUIRE( Tracker::TryOverflowCapacity( requiredCount, alignment, SIZE_MAX, capacity ) );
            CHECK( capacity == expectedCapacity );
            CHECK( (to_size( capacity ) * sizeof( IndirectIndexedDrawCommand )) % alignment == 0u );
            const size_t requiredBytes = to_size( capacity ) * sizeof( IndirectIndexedDrawCommand );
            CHECK( Tracker::TryOverflowCapacity( requiredCount, alignment, requiredBytes, capacity ) );
            CHECK_FALSE( Tracker::TryOverflowCapacity( requiredCount, alignment, requiredBytes - 1u, capacity ) );
        }
        U32 capacity = 0u;
        CHECK_FALSE( Tracker::TryOverflowCapacity( requiredCount, 0u, SIZE_MAX, capacity ) );
        CHECK_FALSE( Tracker::TryOverflowCapacity( 0u, 256u, SIZE_MAX, capacity ) );
        CHECK_FALSE( Tracker::TryOverflowCapacity( U32_MAX, 256u, SIZE_MAX, capacity ) );
    }

    TEST_CASE( "Vulkan rendering attachments discard resolve state between passes", "[render_smoke][render_target]" )
    {
        VkRenderingAttachmentInfo info{};
        vkRenderTarget::ResetAttachmentInfo( info );
        info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        info.resolveImageView = reinterpret_cast<VkImageView>( &info );
        info.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        vkRenderTarget::ResetAttachmentInfo( info );
        CHECK( info.sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO );
        CHECK( info.imageLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
        CHECK( info.resolveMode == VK_RESOLVE_MODE_NONE );
        CHECK( info.resolveImageView == VK_NULL_HANDLE );
        CHECK( info.resolveImageLayout == VK_IMAGE_LAYOUT_UNDEFINED );
        CHECK( info.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR );
    }

    TEST_CASE( "Vulkan render passes load retained attachments before resolving", "[render_smoke][render_target]" )
    {
        for ( const bool clear : { false, true } )
        {
            for ( const bool resolve : { false, true } )
            {
                for ( const bool keep : { false, true } )
                {
                    CAPTURE( clear, resolve, keep );
                    VkRenderingAttachmentInfo info{};
                    vkRenderTarget::ConfigureAttachmentLoadStore( info, clear, resolve, keep );
                    CHECK( info.loadOp == ( clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD ) );
                    CHECK( info.storeOp == ( resolve && !keep ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                              : VK_ATTACHMENT_STORE_OP_STORE ) );
                }
            }
        }

        VkRenderingAttachmentInfo retainedPass{};
        vkRenderTarget::ConfigureAttachmentLoadStore( retainedPass, true, true, true );
        REQUIRE( retainedPass.storeOp == VK_ATTACHMENT_STORE_OP_STORE );

        VkRenderingAttachmentInfo finalPass{};
        vkRenderTarget::ConfigureAttachmentLoadStore( finalPass, false, true, false );
        CHECK( finalPass.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD );
        CHECK( finalPass.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE );
    }

    TEST_CASE( "Vulkan retained attachments transition only their rendered mip", "[render_smoke][render_target]" )
    {
        using Transition = vkTexture::TransitionType;
        for ( const U16 mip : { U16{ 0u }, U16{ 1u }, U16{ 5u } } )
        {
            for ( const bool depth : { false, true } )
            {
                CAPTURE( mip, depth );
                const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                const VkImageSubresourceRange range = vkRenderTarget::GetAttachmentTransitionRange( aspect, mip );
                CHECK( range.aspectMask == aspect );
                CHECK( range.baseMipLevel == mip );
                CHECK( range.levelCount == 1u );
                CHECK( range.baseArrayLayer == 0u );
                CHECK( range.layerCount == VK_REMAINING_ARRAY_LAYERS );

                VkImageMemoryBarrier2 toAttachment{};
                VkImageMemoryBarrier2 toRead{};
                vkTexture::TransitionTexture( depth ? Transition::SHADER_READ_TO_DEPTH_ATTACHMENT : Transition::SHADER_READ_TO_COLOUR_ATTACHMENT,
                                              range, {}, toAttachment );
                vkTexture::TransitionTexture( depth ? Transition::DEPTH_ATTACHMENT_TO_SHADER_READ : Transition::COLOUR_ATTACHMENT_TO_SHADER_READ,
                                              range, {}, toRead );
                CHECK( toAttachment.oldLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
                CHECK( toAttachment.newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
                CHECK( toRead.oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
                CHECK( toRead.newLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
                CHECK( toRead.subresourceRange.baseMipLevel == mip );
                CHECK( toRead.subresourceRange.levelCount == 1u );
                CHECK( toRead.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS );
            }
        }
    }

    TEST_CASE( "Vulkan resolve targets return to shader-read after a pass", "[render_smoke][render_target]" )
    {
        CHECK( vkRenderTarget::ResolveTargetUsage( true ) == RTUsageTracker::Layout::ATTACHMENT );
        CHECK( vkRenderTarget::ResolveTargetUsage( false ) == RTUsageTracker::Layout::SHADER_READ );

        for ( const bool depth : { false, true } )
        {
            VkImageSubresourceRange range
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0u,
                .levelCount = 1u,
                .baseArrayLayer = 0u,
                .layerCount = 1u
            };
            if ( depth )
            {
                range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            VkImageMemoryBarrier2 endBarrier{};
            vkTexture::TransitionTexture( depth ? vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ
                                                : vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ,
                                          range, { ._name = "resolve", ._isResolveImage = true }, endBarrier );
            CHECK( endBarrier.oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
            CHECK( endBarrier.newLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );

            VkImageMemoryBarrier2 beginBarrier{};
            vkTexture::TransitionTexture( depth ? vkTexture::TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT
                                                : vkTexture::TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT,
                                          range, { ._name = "resolve", ._isResolveImage = true }, beginBarrier );
            CHECK( beginBarrier.oldLayout == endBarrier.newLayout );
            CHECK( beginBarrier.newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
        }
    }

    TEST_CASE( "Vulkan attachment blits restore layouts and attachment load access", "[render_smoke][image_barrier]" )
    {
        using Transition = vkTexture::TransitionType;
        for ( const bool depth : { false, true } )
        {
            for ( const bool isResolve : { false, true } )
            {
                CAPTURE( depth, isResolve );
                const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                const VkImageSubresourceRange range
                {
                    .aspectMask = aspect,
                    .baseMipLevel = 2u,
                    .levelCount = 1u,
                    .baseArrayLayer = 3u,
                    .layerCount = 1u
                };
                const VkAccessFlags2 attachmentAccess = depth
                    ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                    : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                const VkPipelineStageFlags2 attachmentStages = depth
                    ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                    : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

                const std::array cases
                {
                    std::pair
                    {
                        depth ? Transition::ATTACHMENT_TO_BLIT_READ_DEPTH : Transition::ATTACHMENT_TO_BLIT_READ_COLOUR,
                        depth ? Transition::BLIT_READ_TO_ATTACHMENT_DEPTH : Transition::BLIT_READ_TO_ATTACHMENT_COLOUR
                    },
                    std::pair
                    {
                        depth ? Transition::ATTACHMENT_TO_BLIT_WRITE_DEPTH : Transition::ATTACHMENT_TO_BLIT_WRITE_COLOUR,
                        depth ? Transition::BLIT_WRITE_TO_ATTACHMENT_DEPTH : Transition::BLIT_WRITE_TO_ATTACHMENT_COLOUR
                    }
                };
                for ( size_t i = 0u; i < cases.size(); ++i )
                {
                    const bool read = i == 0u;
                    const VkAccessFlags2 transferAccess = read ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    const VkImageLayout transferLayout = read ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    VkImageMemoryBarrier2 before{};
                    VkImageMemoryBarrier2 after{};
                    const vkTexture::NamedVKImage image{ ._name = "attachment", ._isResolveImage = isResolve };
                    vkTexture::TransitionTexture( cases[i].first, range, image, before );
                    vkTexture::TransitionTexture( cases[i].second, range, image, after );
                    CHECK( before.oldLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
                    CHECK( before.newLayout == transferLayout );
                    CHECK( after.oldLayout == transferLayout );
                    CHECK( after.newLayout == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL );
                    CHECK( ( before.srcAccessMask & attachmentAccess ) == attachmentAccess );
                    CHECK( ( before.srcStageMask & attachmentStages ) == attachmentStages );
                    CHECK( ( before.dstAccessMask & transferAccess ) == transferAccess );
                    CHECK( ( after.srcAccessMask & transferAccess ) == transferAccess );
                    CHECK( ( after.dstAccessMask & attachmentAccess ) == attachmentAccess );
                    CHECK( ( after.dstStageMask & attachmentStages ) == attachmentStages );
                    CHECK( before.subresourceRange.baseMipLevel == range.baseMipLevel );
                    CHECK( before.subresourceRange.baseArrayLayer == range.baseArrayLayer );
                    CHECK( after.subresourceRange.aspectMask == range.aspectMask );
                    CHECK( after.subresourceRange.levelCount == range.levelCount );
                    CHECK( after.subresourceRange.layerCount == range.layerCount );
                }
            }
        }
    }

    TEST_CASE( "Vulkan shader-read transitions do not infer layout from resolve usage", "[render_smoke][image_barrier]" )
    {
        using Transition = vkTexture::TransitionType;
        struct TransitionCase
        {
            Transition _type;
            VkImageLayout _oldLayout;
            VkImageLayout _newLayout;
        };
        constexpr std::array cases
        {
            TransitionCase{ Transition::UNDEFINED_TO_SHADER_READ, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
            TransitionCase{ Transition::SHADER_READ_TO_SHADER_WRITE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL },
            TransitionCase{ Transition::SHADER_READ_TO_SHADER_READ_WRITE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL },
            TransitionCase{ Transition::SHADER_READ_TO_BLIT_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
            TransitionCase{ Transition::SHADER_READ_TO_BLIT_WRITE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL },
            TransitionCase{ Transition::BLIT_READ_TO_SHADER_READ, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
            TransitionCase{ Transition::BLIT_WRITE_TO_SHADER_READ, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
            TransitionCase{ Transition::SHADER_READ_TO_COPY_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
            TransitionCase{ Transition::SHADER_READ_TO_COPY_WRITE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL },
            TransitionCase{ Transition::COPY_READ_TO_SHADER_READ, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
            TransitionCase{ Transition::COPY_WRITE_TO_SHADER_READ, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL }
        };
        constexpr VkImageSubresourceRange range
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 2u,
            .levelCount = 1u,
            .baseArrayLayer = 3u,
            .layerCount = 1u
        };
        for ( const TransitionCase& test : cases )
        {
            INFO( vkTexture::Names::transitionType[to_base( test._type )] );
            for ( const bool isResolve : { false, true } )
            {
                INFO( isResolve );
                VkImageMemoryBarrier2 barrier{};
                vkTexture::TransitionTexture( test._type, range, { ._name = "image", ._isResolveImage = isResolve }, barrier );
                CHECK( barrier.oldLayout == test._oldLayout );
                CHECK( barrier.newLayout == test._newLayout );
                CHECK( barrier.subresourceRange.baseMipLevel == range.baseMipLevel );
                CHECK( barrier.subresourceRange.baseArrayLayer == range.baseArrayLayer );
                if ( test._oldLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL )
                {
                    CHECK( ( barrier.srcAccessMask & VK_ACCESS_2_SHADER_READ_BIT ) != 0u );
                }
                if ( test._newLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL )
                {
                    CHECK( ( barrier.dstAccessMask & VK_ACCESS_2_SHADER_READ_BIT ) != 0u );
                }
            }
        }
    }

    TEST_CASE( "Vulkan initializes sampleable attachments in shader-read layout", "[render_smoke][image_barrier]" )
    {
        TextureDescriptor descriptor{};
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::SHADER_READ );

        AddImageUsageFlag( descriptor, ImageUsage::RT_COLOUR_ATTACHMENT );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::SHADER_READ );
        AddImageUsageFlag( descriptor, ImageUsage::RT_RESOLVE_TARGET );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::SHADER_READ );
        RemoveImageUsageFlag( descriptor, ImageUsage::SHADER_READ );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_RESOLVE_TARGET );
        RemoveImageUsageFlag( descriptor, ImageUsage::RT_RESOLVE_TARGET );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_COLOUR_ATTACHMENT );

        RemoveImageUsageFlag( descriptor, ImageUsage::RT_COLOUR_ATTACHMENT );
        AddImageUsageFlag( descriptor, ImageUsage::RT_DEPTH_ATTACHMENT );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_DEPTH_ATTACHMENT );
        AddImageUsageFlag( descriptor, ImageUsage::RT_RESOLVE_TARGET );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_RESOLVE_TARGET );
        AddImageUsageFlag( descriptor, ImageUsage::SHADER_READ );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::SHADER_READ );
        RemoveImageUsageFlag( descriptor, ImageUsage::SHADER_READ );
        RemoveImageUsageFlag( descriptor, ImageUsage::RT_RESOLVE_TARGET );
        RemoveImageUsageFlag( descriptor, ImageUsage::RT_DEPTH_ATTACHMENT );
        AddImageUsageFlag( descriptor, ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT );
        CHECK( vkTexture::InitialUsage( descriptor ) == ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT );
    }

    TEST_CASE( "Vulkan initializes every sampled attachment subresource before shader reads", "[render_smoke][image_barrier]" )
    {
        TextureDescriptor descriptor{};
        for ( const GFXImagePacking packing : { GFXImagePacking::NORMALIZED, GFXImagePacking::DEPTH, GFXImagePacking::DEPTH_STENCIL } )
        {
            descriptor._packing = packing;
            const vkTexture::InitialClearPlan plan = vkTexture::MakeInitialClearPlan( descriptor );
            CHECK( plan._range.aspectMask == vkTexture::GetAspectFlags( descriptor ) );
            CHECK( plan._range.baseMipLevel == 0u );
            CHECK( plan._range.levelCount == VK_REMAINING_MIP_LEVELS );
            CHECK( plan._range.baseArrayLayer == 0u );
            CHECK( plan._range.layerCount == VK_REMAINING_ARRAY_LAYERS );
            CHECK( plan._isDepthStencil == ( packing != GFXImagePacking::NORMALIZED ) );
            CHECK( plan._depthStencil.depth == 1.f );
            CHECK( plan._depthStencil.stencil == 0u );
            CHECK( plan._colour.float32[0] == 0.f );
            CHECK( plan._colour.float32[1] == 0.f );
            CHECK( plan._colour.float32[2] == 0.f );
            CHECK( plan._colour.float32[3] == 0.f );

            for ( const bool isResolve : { false, true } )
            {
                VkImageMemoryBarrier2 before{};
                VkImageMemoryBarrier2 after{};
                vkTexture::TransitionTexture( vkTexture::TransitionType::UNDEFINED_TO_COPY_WRITE, plan._range,
                                              { ._name = "initial clear", ._isResolveImage = isResolve }, before );
                vkTexture::TransitionTexture( vkTexture::TransitionType::COPY_WRITE_TO_SHADER_READ, plan._range,
                                              { ._name = "initial clear", ._isResolveImage = isResolve }, after );
                CHECK( before.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED );
                CHECK( before.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
                CHECK( ( before.dstAccessMask & VK_ACCESS_2_TRANSFER_WRITE_BIT ) != 0u );
                CHECK( after.oldLayout == before.newLayout );
                CHECK( after.newLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
                CHECK( ( after.srcAccessMask & VK_ACCESS_2_TRANSFER_WRITE_BIT ) != 0u );
                CHECK( ( after.dstAccessMask & VK_ACCESS_2_SHADER_READ_BIT ) != 0u );
            }
        }
    }

    TEST_CASE( "Render debug scopes clear the active entry without exceeding the stack", "[render_smoke][debug_scope]" )
    {
        GFXDevice::ClearDebugMessages();
        SCOPE_EXIT
        {
            GFXDevice::ClearDebugMessages();
        };

        for ( U32 i = 0u; i < Config::MAX_DEBUG_SCOPE_DEPTH; ++i )
        {
            GFXDevice::PushDebugMessage( "scope", i );
        }
        REQUIRE( GFXDevice::s_debugScopeDepth == Config::MAX_DEBUG_SCOPE_DEPTH );

        for ( U32 i = Config::MAX_DEBUG_SCOPE_DEPTH; i > 0u; --i )
        {
            CHECK( GFXDevice::s_debugScope[i - 1u]._id == i - 1u );
            GFXDevice::PopDebugMessage();
            CHECK( GFXDevice::s_debugScopeDepth == i - 1u );
            CHECK( GFXDevice::s_debugScope[i - 1u]._name == nullptr );
            CHECK( GFXDevice::s_debugScope[i - 1u]._id == U32_MAX );
        }
    }

    TEST_CASE( "Vulkan images allow VMA to choose dedicated or pooled memory", "[render_smoke][allocation]" )
    {
        const VmaAllocationCreateInfo info = vkTexture::ImageAllocationInfo();
        CHECK( info.usage == VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE );
        CHECK( ( info.flags & VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT ) == 0u );
        CHECK( info.priority == 1.f );
    }

    TEST_CASE( "VMA debug logging defaults off and survives configuration round trips", "[render_smoke][allocation][configuration]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-vma-config-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove_all( directory, error );
        };
        const ResourcePath path{ directory.string() };
        {
            std::ofstream missing{ directory / "missing.xml" };
            missing << "<config><debug><renderer/></debug></config>";
            REQUIRE( missing.good() );
        }
        {
            std::ofstream enabled{ directory / "enabled.xml" };
            enabled << "<config><debug><renderer><enableVMADebugLogging>true</enableVMADebugLogging>"
                       "</renderer></debug></config>";
            REQUIRE( enabled.good() );
        }

        Configuration defaults{};
        CHECK_FALSE( defaults.debug.renderer.enableVMADebugLogging );
        REQUIRE( XML::loadFromXML( defaults, path, "missing.xml" ) );
        CHECK_FALSE( defaults.debug.renderer.enableVMADebugLogging );

        Configuration config{};
        REQUIRE( XML::loadFromXML( config, path, "enabled.xml" ) );
        REQUIRE( config.debug.renderer.enableVMADebugLogging );
        config.debug.renderer.enableVMADebugLogging = false;
        REQUIRE( XML::saveToXML( config, path, "saved-disabled.xml" ) );
        Configuration savedDisabled{};
        REQUIRE( XML::loadFromXML( savedDisabled, path, "saved-disabled.xml" ) );
        CHECK_FALSE( savedDisabled.debug.renderer.enableVMADebugLogging );

        config.debug.renderer.enableVMADebugLogging = true;
        REQUIRE( XML::saveToXML( config, path, "saved-enabled.xml" ) );
        Configuration savedEnabled{};
        REQUIRE( XML::loadFromXML( savedEnabled, path, "saved-enabled.xml" ) );
        CHECK( savedEnabled.debug.renderer.enableVMADebugLogging );
    }

    TEST_CASE( "VMA trace arguments are evaluated only while logging is enabled", "[render_smoke][allocation]" )
    {
        const bool previous = g_enableVMADebugLogging.exchange( false, std::memory_order_relaxed );
        SCOPE_EXIT
        {
            g_enableVMADebugLogging.store( previous, std::memory_order_relaxed );
        };
        I32 evaluations = 0;
        VMA_DEBUG_LOG( "VMA logging gate {}", ++evaluations );
        CHECK( evaluations == 0 );
        g_enableVMADebugLogging.store( true, std::memory_order_relaxed );
        VMA_DEBUG_LOG( "VMA logging gate {}", ++evaluations );
        CHECK( evaluations == 1 );
    }

    TEST_CASE( "Device-fault capture requires smoke mode, its extension and feature", "[render_smoke][device_fault]" )
    {
        CHECK( VKDevice::CanEnableDeviceFault( true, true, true ) );
        CHECK_FALSE( VKDevice::CanEnableDeviceFault( false, true, true ) );
        CHECK_FALSE( VKDevice::CanEnableDeviceFault( true, false, true ) );
        CHECK_FALSE( VKDevice::CanEnableDeviceFault( true, true, false ) );
    }

    TEST_CASE( "Vulkan smoke readback accepts only single-sample RGBA8 scene images", "[render_smoke][readback]" )
    {
        CHECK( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK( vkTexture::SupportsSmokeReadback( VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK( vkTexture::SupportsSmokeReadback( VK_FORMAT_B8G8R8A8_SRGB, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK_FALSE( vkTexture::SupportsSmokeReadback( VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, 1u, 1u ) );
        CHECK_FALSE( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_4_BIT, 1u, 1u ) );
        CHECK_FALSE( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, 2u, 1u ) );
        CHECK_FALSE( vkTexture::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, 1u, 2u ) );
    }

    TEST_CASE( "Vulkan frame timestamps convert masked ticks to milliseconds", "[renderer][vulkan]" )
    {
        CHECK( VKSwapChain::TimestampDeltaMS( 100u, 200u, 0u, 1.f ) == 0.f );
        CHECK( VKSwapChain::TimestampDeltaMS( 0u, 2'000'000u, 64u, 1.f ) == Catch::Approx( 2.f ) );
        CHECK( VKSwapChain::TimestampDeltaMS( 0u, 1'000'000u, 64u, 10.f ) == Catch::Approx( 10.f ) );
        // A 32-bit counter wrapping between the two samples still yields the forward delta.
        CHECK( VKSwapChain::TimestampDeltaMS( 0xFFFFFF00ull, 0x00000100ull, 32u, 1.f ) == Catch::Approx( 0x200 * 1e-6 ) );
    }

    TEST_CASE( "Vulkan editor readback transitions directly from the last screen pass", "[render_smoke][readback]" )
    {
        CHECK( VKSwapChain::ScreenPassInitialLayout( true, false, false ) == VK_IMAGE_LAYOUT_UNDEFINED );
        CHECK( VKSwapChain::ScreenPassInitialLayout( true, false, true ) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        CHECK( VKSwapChain::ScreenPassInitialLayout( true, true, true ) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        CHECK( VKSwapChain::ScreenPassInitialLayout( true, true, false ) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        CHECK( VKSwapChain::ScreenPassInitialLayout( false, true, true ) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        CHECK( VKSwapChain::ScreenPassInitialLayout( false, true, false ) == VK_IMAGE_LAYOUT_UNDEFINED );

        for ( const bool capture : { false, true } )
        {
            const VkImageMemoryBarrier2 barrier = VKSwapChain::SmokePresentBarrier( VK_NULL_HANDLE, capture );
            CHECK( barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 );
            CHECK( barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED );
            CHECK( barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED );
            CHECK( barrier.srcStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT );
            CHECK( barrier.srcAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT );
            CHECK( barrier.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            CHECK( barrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT );
            CHECK( barrier.subresourceRange.levelCount == 1u );
            CHECK( barrier.subresourceRange.layerCount == 1u );
            CHECK( barrier.newLayout == ( capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ) );
            CHECK( barrier.dstStageMask == ( capture ? VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT : VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT ) );
            CHECK( barrier.dstAccessMask == ( capture ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_NONE ) );
        }
    }

    TEST_CASE( "Vulkan editor readback converts swapchain pixels and image origin", "[render_smoke][readback]" )
    {
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_R8G8B8A8_SRGB ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_B8G8R8A8_UNORM ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_B8G8R8A8_SRGB ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_A2B10G10R10_UNORM_PACK32 ) );
        CHECK( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_A2R10G10B10_UNORM_PACK32 ) );
        CHECK_FALSE( VKSwapChain::SupportsSmokeReadback( VK_FORMAT_R16G16B16A16_SFLOAT ) );

        const std::array<Byte, 16> bgra
        {{
            Byte{ 3 }, Byte{ 2 }, Byte{ 1 }, Byte{ 255 }, Byte{ 6 }, Byte{ 5 }, Byte{ 4 }, Byte{ 255 },
            Byte{ 9 }, Byte{ 8 }, Byte{ 7 }, Byte{ 255 }, Byte{ 12 }, Byte{ 11 }, Byte{ 10 }, Byte{ 255 }
        }};
        const std::array<Byte, 16> expected
        {{
            Byte{ 7 }, Byte{ 8 }, Byte{ 9 }, Byte{ 255 }, Byte{ 10 }, Byte{ 11 }, Byte{ 12 }, Byte{ 255 },
            Byte{ 1 }, Byte{ 2 }, Byte{ 3 }, Byte{ 255 }, Byte{ 4 }, Byte{ 5 }, Byte{ 6 }, Byte{ 255 }
        }};
        std::array<Byte, 16> converted{};
        REQUIRE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_B8G8R8A8_SRGB, 2u, 2u, bgra, converted ) );
        CHECK( converted == expected );
        REQUIRE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, 2u, 2u, expected, converted ) );
        CHECK( converted[0] == Byte{ 1 } );
        CHECK( converted[2] == Byte{ 3 } );
        CHECK( converted[8] == Byte{ 7 } );

        const std::array<Byte, 4> expectedPacked{{ Byte{ 255 }, Byte{ 128 }, Byte{ 0 }, Byte{ 170 } }};
        for ( const VkFormat format : { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32 } )
        {
            const U32 pixel = ( 2u << 30u ) | ( 512u << 10u ) |
                              ( format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ? 1023u : 1023u << 20u );
            const std::array<U32, 1> source{ pixel };
            std::array<Byte, 4> output{};
            REQUIRE( VKSwapChain::ConvertSmokeReadback( format, 1u, 1u, std::as_bytes( std::span{ source } ), output ) );
            CHECK( output == expectedPacked );
        }

        const auto unchanged = converted;
        CHECK_FALSE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, 0u, 2u, bgra, converted ) );
        CHECK_FALSE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_R8G8B8A8_UNORM, 3u, 2u, bgra, converted ) );
        CHECK_FALSE( VKSwapChain::ConvertSmokeReadback( VK_FORMAT_R16G16B16A16_SFLOAT, 2u, 2u, bgra, converted ) );
        CHECK( converted == unchanged );
    }

    TEST_CASE( "Compute workgroup support treats device limits as inclusive maxima", "[compute][device_limits]" )
    {
        DeviceInformation info{};
        info._maxWorkgroupSize[0] = 256u;
        info._maxWorkgroupSize[1] = 128u;
        info._maxWorkgroupSize[2] = 64u;
        info._maxWorkgroupInvocations = 256u;

        CHECK( SupportsComputeWorkgroup( info, 1u, 1u, 1u ) );
        CHECK( SupportsComputeWorkgroup( info, 256u, 1u, 1u ) );
        CHECK( SupportsComputeWorkgroup( info, 16u, 16u, 1u ) );
        CHECK( SupportsComputeWorkgroup( info, 2u, 2u, 64u ) );

        CHECK_FALSE( SupportsComputeWorkgroup( info, 0u, 1u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 1u, 0u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 1u, 1u, 0u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 257u, 1u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 1u, 129u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 1u, 1u, 65u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 32u, 32u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( info, 16u, 8u, 4u ) );

        // The product is computed in 64 bits so huge per-axis limits cannot wrap the invocation check.
        info._maxWorkgroupSize[0] = info._maxWorkgroupSize[1] = info._maxWorkgroupSize[2] = U32_MAX;
        info._maxWorkgroupInvocations = 1024u;
        CHECK_FALSE( SupportsComputeWorkgroup( info, 65536u, 65536u, 1u ) );

        // Vulkan's guaranteed minimum limits.
        DeviceInformation vkMinimum{};
        vkMinimum._maxWorkgroupSize[0] = 128u;
        vkMinimum._maxWorkgroupSize[1] = 128u;
        vkMinimum._maxWorkgroupSize[2] = 64u;
        vkMinimum._maxWorkgroupInvocations = 128u;
        CHECK( SupportsComputeWorkgroup( vkMinimum, 8u, 8u, 1u ) );
        CHECK( SupportsComputeWorkgroup( vkMinimum, 64u, 1u, 1u ) );
        CHECK_FALSE( SupportsComputeWorkgroup( vkMinimum, 16u, 16u, 1u ) );
    }

    TEST_CASE( "Instanced draws agree with material indirection requirements", "[instancing][indirection]" )
    {
        for ( const U32 instanceCount : { 0u, 1u, 2u, 64u } )
        {
            const bool drawRequiresInstancing = instanceCount > 1u;
            const bool nonInstancedMaterial = RequiresInstancedIndirection( false, false, instanceCount );
            CHECK( nonInstancedMaterial == drawRequiresInstancing );
            CHECK( IndirectionBaseInstance( nonInstancedMaterial, 37u ) == (drawRequiresInstancing ? 0u : 38u) );

            const bool instancedMaterial = RequiresInstancedIndirection( false, true, instanceCount );
            CHECK( instancedMaterial );
            CHECK( IndirectionBaseInstance( instancedMaterial, 37u ) == 0u );
            CHECK( RequiresInstancedIndirection( true, false, instanceCount ) );
        }

        CHECK( IndirectionBaseInstance( false, 0u ) == 1u );
    }

    TEST_CASE( "Light descriptor visibility names match their shader stages", "[descriptors][lighting]" )
    {
        constexpr auto vertex = to_base( ShaderStageVisibility::VERTEX );
        constexpr auto fragment = to_base( ShaderStageVisibility::FRAGMENT );
        constexpr auto compute = to_base( ShaderStageVisibility::COMPUTE );
        constexpr auto fragmentCompute = to_base( ShaderStageVisibility::COMPUTE_AND_FRAGMENT );
        constexpr auto lightBuffer = to_base( ShaderStageVisibility::VERTEX_FRAGMENT_COMPUTE );

        CHECK( fragmentCompute == (fragment | compute) );
        CHECK( (fragmentCompute & vertex) == 0u );
        CHECK( lightBuffer == (vertex | fragment | compute) );
        CHECK( (lightBuffer & to_base( ShaderStageVisibility::GEOMETRY )) == 0u );
        CHECK( to_base( ShaderStageVisibility::COMPUTE_AND_DRAW ) == fragmentCompute );
    }

    struct WindowManagerTestAccess
    {
        static ErrorCode ApplyContextSettings(WindowManager& manager, const bool debug)
        {
            manager._apiSettings._createDebugContext = debug;
            return manager.applyAPISettingsPreCreate(0u, RenderAPI::OpenGL);
        }
    };

    TEST_CASE( "OpenGL debug retries clear no-error context attributes", "[window_context]" )
    {
        platformInitRunListener::PlatformInit();
        const char* previousDriverHint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
        const std::string previousDriver = previousDriverHint == nullptr ? "" : previousDriverHint;
        const bool hadDriverHint = previousDriverHint != nullptr;
        REQUIRE( SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy") );
        SCOPE_EXIT
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            if (hadDriverHint)
            {
                SDL_SetHint(SDL_HINT_VIDEO_DRIVER, previousDriver.c_str());
            }
            else
            {
                SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
            }
        };
        REQUIRE( SDL_InitSubSystem(SDL_INIT_VIDEO) );
        WindowManager manager;
        for (const bool debug : { false, true, false, true })
        {
            REQUIRE( WindowManagerTestAccess::ApplyContextSettings(manager, debug) == ErrorCode::NO_ERR );
            int noError = -1;
            int flags = 0;
            REQUIRE( SDL_GL_GetAttribute(SDL_GL_CONTEXT_NO_ERROR, &noError) );
            REQUIRE( SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &flags) );
            CHECK( noError == (debug ? 0 : 1) );
            CHECK( ((flags & SDL_GL_CONTEXT_DEBUG_FLAG) != 0) == debug );
        }
    }

    TEST_CASE( "Debug previews explicitly reset splash linearisation state", "[texture_fallbacks]" )
    {
        DebugView view;
        const auto& entries = view._shaderData.entries();
        const auto entry = std::find_if( entries.begin(), entries.end(), [](const auto& value)
        {
            return value._bindingHash == _ID( "skipLinearisation" );
        } );
        REQUIRE( entry != entries.end() );
        CHECK( entry->_type == PushConstantType::UINT );
        U32 value = 1u;
        std::memcpy( &value, view._shaderData.data( entry->_range._startOffset ), sizeof(value) );
        CHECK( value == 0u );
    }

    TEST_CASE( "Debug previews reset numerical display options", "[texture_fallbacks]" )
    {
        DebugView view;
        const auto& entries = view._shaderData.entries();
        for (const U64 hash : { _ID( "channelsArePacked" ), _ID( "scaleAndBias" ), _ID( "normalizeOutput" ) })
        {
            const auto entry = std::find_if( entries.begin(), entries.end(), [hash](const auto& value)
            {
                return value._bindingHash == hash;
            } );
            REQUIRE( entry != entries.end() );
            CHECK( entry->_type == PushConstantType::UINT );
            U32 value = 1u;
            std::memcpy( &value, view._shaderData.data( entry->_range._startOffset ), sizeof(value) );
            CHECK( value == 0u );
        }
    }

    TEST_CASE( "Default debug views initialize once per registry lifetime", "[render_smoke][debug_views][startup]" )
    {
        DebugViewRegistry registry;
        CHECK( registry.beginDefaultViewInitialization() );
        CHECK_FALSE( registry.beginDefaultViewInitialization() );

        registry.add( std::make_shared<DebugView>() );
        registry.clear();
        vector<std::tuple<string, I16, I16, bool>> names;
        registry.getNames( names );
        CHECK( names.empty() );
        CHECK( registry.beginDefaultViewInitialization() );
        CHECK_FALSE( registry.beginDefaultViewInitialization() );
    }

    TEST_CASE( "Removed debug views retain queued uniform data until frame flush", "[render_smoke][debug_views]" )
    {
        DebugViewRegistry registry;
        DebugView_ptr view = std::make_shared<DebugView>();
        view->_shaderData.set( _ID( "layer" ), PushConstantType::INT, I32{ 7 } );
        std::weak_ptr<DebugView> weakView = view;
        DebugView* const raw = registry.add( view );
        registry.add( view );
        GFX::SendPushConstantsCommand command;
        command._uniformData = &view->_shaderData;
        view.reset();

        REQUIRE( registry.remove( raw ) );
        CHECK_FALSE( registry.remove( raw ) );
        CHECK_FALSE( registry.remove( nullptr ) );
        vector<std::tuple<string, I16, I16, bool>> names;
        registry.getNames( names );
        CHECK( names.empty() );
        REQUIRE_FALSE( weakView.expired() );
        REQUIRE( command._uniformData == &weakView.lock()->_shaderData );
        const auto& entries = command._uniformData->entries();
        const auto layer = std::find_if( entries.begin(), entries.end(), []( const auto& entry )
        {
            return entry._bindingHash == _ID( "layer" );
        } );
        REQUIRE( layer != entries.end() );
        I32 layerValue = -1;
        std::memcpy( &layerValue, command._uniformData->data( layer->_range._startOffset ), sizeof( layerValue ) );
        CHECK( layerValue == 7 );

        registry.releaseRetired();
        CHECK( weakView.expired() );
    }

    TEST_CASE( "Debug views sort by group and index and support concurrent removal", "[render_smoke][debug_views]" )
    {
        DebugViewRegistry registry;
        std::array<DebugView_ptr, 3> ordered = {
            std::make_shared<DebugView>( 2 ),
            std::make_shared<DebugView>( 0 ),
            std::make_shared<DebugView>( 1 )
        };
        for ( size_t i = 0u; i < ordered.size(); ++i )
        {
            ordered[i]->_groupID = to_I16( i + 1u );
            ordered[i]->_name = Util::StringFormat( "view{}", i );
            registry.add( ordered[i] );
        }
        vector<std::tuple<string, I16, I16, bool>> names;
        registry.getNames( names );
        REQUIRE( names.size() == ordered.size() );
        CHECK( std::get<0>( names[0] ) == "view0" );
        CHECK( std::get<0>( names[1] ) == "view1" );
        CHECK( std::get<0>( names[2] ) == "view2" );

        constexpr size_t viewCount = 32u;
        std::array<DebugView_ptr, viewCount> views;
        std::array<std::weak_ptr<DebugView>, viewCount> weakViews;
        for ( size_t i = 0u; i < viewCount; ++i )
        {
            views[i] = std::make_shared<DebugView>();
            weakViews[i] = views[i];
        }

        std::atomic_bool removedAll{ true };
        std::array<std::thread, 4> workers;
        for ( size_t worker = 0u; worker < workers.size(); ++worker )
        {
            workers[worker] = std::thread( [&, worker]
            {
                for ( size_t i = worker; i < viewCount; i += workers.size() )
                {
                    registry.add( views[i] );
                    if ( !registry.remove( views[i].get() ) )
                    {
                        removedAll.store( false );
                    }
                }
            } );
        }
        for ( std::thread& worker : workers )
        {
            worker.join();
        }
        REQUIRE( removedAll.load() );
        views.fill( nullptr );
        for ( const auto& weak : weakViews )
        {
            CHECK_FALSE( weak.expired() );
        }
        registry.releaseRetired();
        for ( const auto& weak : weakViews )
        {
            CHECK( weak.expired() );
        }
    }

    TEST_CASE( "Vulkan indexed indirect batches require one vertex and one index buffer", "[renderer][indirect_batch]" )
    {
        using enum BufferUsageType;
        CHECK( IndexedIndirectBatchBuffersCompatible( VERTEX_BUFFER, INDEX_BUFFER ) );
        CHECK( IndexedIndirectBatchBuffersCompatible( INDEX_BUFFER, VERTEX_BUFFER ) );
        for ( U8 first = 0u; first < to_base( COUNT ); ++first )
        {
            for ( U8 second = 0u; second < to_base( COUNT ); ++second )
            {
                const auto a = static_cast<BufferUsageType>( first );
                const auto b = static_cast<BufferUsageType>( second );
                const bool expected = ( a == VERTEX_BUFFER && b == INDEX_BUFFER ) ||
                                      ( a == INDEX_BUFFER && b == VERTEX_BUFFER );
                CHECK( IndexedIndirectBatchBuffersCompatible( a, b ) == expected );
            }
        }
    }

    TEST_CASE( "Generic draw commands preserve natural member alignment", "[renderer][indirect_batch]" )
    {
        STATIC_CHECK( sizeof(GenericDrawCommand) == 40u );
        STATIC_CHECK( alignof(GenericDrawCommand) >= alignof(const PoolHandle*) );
        STATIC_CHECK( offsetof(GenericDrawCommand, _sourceBuffers) % alignof(const PoolHandle*) == 0u );
        STATIC_CHECK( offsetof(GenericDrawCommand, _cmd) % alignof(IndirectIndexedDrawCommand) == 0u );

        struct PrefixedCommands
        {
            U8 _prefix = 0u;
            std::array<GenericDrawCommand, 2u> _commands{};
        } prefixed;
        std::array<PoolHandle, 1u> buffers{ PoolHandle{ 1u, 1u } };
        const auto check = [&]( GenericDrawCommand& command )
        {
            REQUIRE( reinterpret_cast<uintptr_t>( &command._sourceBuffers ) % alignof(const PoolHandle*) == 0u );
            command._sourceBuffers = buffers.data();
            const PoolHandle* const& sourceBuffers = command._sourceBuffers;
            CHECK( sourceBuffers == buffers.data() );
        };
        for ( auto& command : prefixed._commands )
        {
            check( command );
        }
        GenericDrawCommandContainer commands;
        commands.resize( 1u );
        check( commands[0] );
        commands.resize( 2u );
        for ( auto& command : commands )
        {
            check( command );
        }
    }

    TEST_CASE( "Indirect draw batches preserve adjacent submission state", "[renderer][indirect_batch]" )
    {
        std::array<UniformData, 3u> uniforms;
        std::array<PoolHandle, 2u> buffers{ PoolHandle{ 1u, 1u }, PoolHandle{ 2u, 1u } };
        std::array<PoolHandle, 2u> otherBuffers{ PoolHandle{ 1u, 1u }, PoolHandle{ 3u, 1u } };
        std::array<GFX::SendPushConstantsCommand*, 3u> pushes{};
        std::array<GFX::DrawCommand*, 3u> commandsByDraw{};
        std::array<GenericDrawCommand*, 3u> draws{};
        GFX::CommandBuffer commands;
        for ( size_t i = 0u; i < draws.size(); ++i )
        {
            uniforms[i].set( 7u, PushConstantType::UINT, 5u );
            pushes[i] = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( commands );
            pushes[i]->_uniformData = &uniforms[i];
            commandsByDraw[i] = GFX::EnqueueCommand<GFX::DrawCommand>( commands );
            GenericDrawCommand& draw = commandsByDraw[i]->_drawCommands.emplace_back();
            draw._sourceBuffers = buffers.data();
            draw._sourceBuffersCount = to_U16( buffers.size() );
            draw._commandOffset = 10u + to_U32( i );
            draw._cmd.indexCount = 3u;
            draw._cmd.baseInstance = 100u + to_U32( i );
            enableOption( draw, CmdRenderOptions::RENDER_INDIRECT );
            draws[i] = &draw;
        }

        GenericDrawCommand batch{};
        const auto collect = [&]( const bool shaderCompatible = true, const U32 maxDrawCount = 16u )
        {
            return GFX::CollectIndirectDrawBatch( commands.commands(), 1u, shaderCompatible, maxDrawCount, batch );
        };

        SECTION( "matching draws use their original indirect records without modifying queued commands" )
        {
            REQUIRE( collect() == 4u );
            CHECK( batch._drawCount == 3u );
            CHECK( batch._commandOffset == 10u );
            CHECK( batch._sourceBuffers == buffers.data() );
            for ( const GenericDrawCommand* draw : draws )
            {
                CHECK( draw->_drawCount == 1u );
            }
        }
        SECTION( "a later mismatch ends the batch without skipping the rest" )
        {
            draws[2]->_sourceBuffers = otherBuffers.data();
            REQUIRE( collect() == 2u );
            CHECK( batch._drawCount == 2u );
        }
        SECTION( "a shader that reads draw index is never batched" )
        {
            CHECK( collect( false ) == 0u );
        }
        SECTION( "buffer handles and indirect offsets must match" )
        {
            draws[1]->_sourceBuffers = otherBuffers.data();
            CHECK( collect() == 0u );
            draws[1]->_sourceBuffers = buffers.data();
            draws[1]->_sourceBuffersCount = 1u;
            CHECK( collect() == 0u );
            draws[1]->_sourceBuffersCount = 2u;
            draws[1]->_commandOffset = 12u;
            CHECK( collect() == 0u );
            draws[0]->_commandOffset = U32_MAX;
            draws[1]->_commandOffset = 0u;
            CHECK( collect() == 0u );
        }
        SECTION( "render options and draw shape must be eligible" )
        {
            enableOption( *draws[1], CmdRenderOptions::RENDER_WIREFRAME );
            CHECK( collect() == 0u );
            disableOption( *draws[1], CmdRenderOptions::RENDER_WIREFRAME );
            disableOption( *draws[1], CmdRenderOptions::RENDER_INDIRECT );
            CHECK( collect() == 0u );
            enableOption( *draws[1], CmdRenderOptions::RENDER_INDIRECT );
            draws[1]->_cmd.instanceCount = 2u;
            CHECK( collect() == 0u );
            draws[1]->_cmd.instanceCount = 0u;
            CHECK( collect() == 0u );
            draws[1]->_cmd.instanceCount = 1u;
            draws[1]->_drawCount = 2u;
            CHECK( collect() == 0u );
            draws[1]->_drawCount = 1u;
            draws[1]->_cmd.indexCount = 0u;
            CHECK( collect() == 0u );
        }
        SECTION( "device limits retain the backend's strict draw-count bound" )
        {
            CHECK( collect( true, 2u ) == 0u );
            REQUIRE( collect( true, 3u ) == 2u );
            CHECK( batch._drawCount == 2u );
            REQUIRE( collect( true, 4u ) == 4u );
            CHECK( batch._drawCount == 3u );
        }
        SECTION( "uniform values are compared when the batch is collected" )
        {
            REQUIRE( collect() == 4u );
            uniforms[1].set( 7u, PushConstantType::UINT, 6u );
            CHECK( collect() == 0u );
            uniforms[1].set( 7u, PushConstantType::UINT, 5u );
            CHECK( collect() == 4u );
            pushes[1]->_uniformData = nullptr;
            CHECK( collect() == 0u );
        }
        SECTION( "uniform binding metadata and payload lengths must match" )
        {
            UniformData differentBinding;
            differentBinding.set( 8u, PushConstantType::UINT, 5u );
            pushes[1]->_uniformData = &differentBinding;
            CHECK( collect() == 0u );

            UniformData differentType;
            differentType.set( 7u, PushConstantType::INT, 5u );
            pushes[1]->_uniformData = &differentType;
            CHECK( collect() == 0u );

            UniformData differentLength;
            const std::array<U32, 2u> values{ 5u, 5u };
            differentLength.set( 7u, PushConstantType::UINT, values.data(), values.size() );
            pushes[1]->_uniformData = &differentLength;
            CHECK( collect() == 0u );
        }
        SECTION( "fast constants compare flags and exact bytes" )
        {
            pushes[0]->_fastData.data[0] = MAT4_ZERO;
            pushes[1]->_fastData.data[0] = MAT4_ZERO;
            pushes[0]->_fastData.computeFlags();
            CHECK( collect() == 0u );
            pushes[1]->_fastData.computeFlags();
            pushes[1]->_fastData.data[0].mat[0] = -0.f;
            CHECK( collect() == 0u );
            pushes[1]->_fastData.data[0].mat[0] = 0.f;
            REQUIRE( collect() == 2u );
        }
        SECTION( "an intervening command stops batching" )
        {
            GFX::MemoryBarrierCommand barrier;
            GFX::CommandBuffer::CommandList interrupted;
            interrupted.push_back( pushes[0] );
            interrupted.push_back( commandsByDraw[0] );
            interrupted.push_back( &barrier );
            interrupted.push_back( pushes[1] );
            interrupted.push_back( commandsByDraw[1] );
            CHECK( GFX::CollectIndirectDrawBatch( interrupted, 1u, true, 16u, batch ) == 0u );
        }
    }

    TEST_CASE( "Push command merging preserves shared uniform storage", "[push_constants]" )
    {
        UniformData uniforms;
        const F32 value = 3.f;
        uniforms.set( 1u, PushConstantType::FLOAT, value );
        GFX::SendPushConstantsCommand lhs, rhs;
        lhs._uniformData = rhs._uniformData = &uniforms;
        lhs._fastData.data[0] = MAT4_IDENTITY;
        lhs._fastData.computeFlags();
        rhs._fastData.data[1] = MAT4_ZERO;
        rhs._fastData.computeFlags();
        REQUIRE( GFX::Merge( &lhs, &rhs ) );
        REQUIRE( uniforms.entries().size() == 1u );
        F32 stored = 0.f;
        std::memcpy( &stored, uniforms.data( uniforms.entries()[0]._range._startOffset ), sizeof(stored) );
        CHECK( stored == value );
    }

    TEST_CASE( "Uniform merges are transactional and later values win", "[push_constants]" )
    {
        UniformData lhs, rhs;
        const F32 oldValue = 1.f, newValue = 2.f;
        lhs.set( 1u, PushConstantType::FLOAT, oldValue );
        rhs.set( 2u, PushConstantType::FLOAT, newValue );
        rhs.set( 1u, PushConstantType::FLOAT, newValue );
        REQUIRE( Merge( lhs, rhs ) );
        CHECK( rhs.entries().empty() );
        REQUIRE( lhs.entries().size() == 2u );
        for ( const auto& entry : lhs.entries() )
        {
            F32 stored = 0.f;
            std::memcpy( &stored, lhs.data( entry._range._startOffset ), sizeof(stored) );
            CHECK( stored == newValue );
        }

        rhs.set( 3u, PushConstantType::FLOAT, oldValue );
        const std::array<F32, 2> incompatible{{oldValue, newValue}};
        rhs.set( 1u, PushConstantType::FLOAT, incompatible.data(), incompatible.size() );
        CHECK_FALSE( Merge( lhs, rhs ) );
        CHECK( lhs.entries().size() == 2u );
        CHECK( rhs.entries().size() == 2u );
        F32 stored = 0.f;
        std::memcpy( &stored, lhs.data( lhs.entries()[0]._range._startOffset ), sizeof(stored) );
        CHECK( stored == newValue );

        UniformData empty;
        REQUIRE( Merge( empty, rhs ) );
        CHECK( empty.entries().size() == 2u );
        CHECK( rhs.entries().empty() );
    }

    TEST_CASE( "Push cache replays all known slots after shader changes", "[push_constants]" )
    {
        PushConstantsStruct cache, firstShader, secondShader, update;
        update.data[0] = MAT4_IDENTITY;
        update.computeFlags();
        cache.merge( update );
        firstShader.merge( cache );

        secondShader.merge( cache );
        CHECK( secondShader.data[0] == MAT4_IDENTITY );
        CHECK( secondShader.dataFlags() == PushConstantsStruct::FIRST );

        update = {};
        update.data[1] = MAT4_ZERO;
        update.computeFlags();
        cache.merge( update );
        secondShader.merge( cache );
        CHECK( secondShader.data[0] == MAT4_IDENTITY );
        CHECK( secondShader.data[1] == MAT4_ZERO );

        firstShader.merge( cache );
        CHECK( firstShader == secondShader );
        CHECK( firstShader.dataFlags() == PushConstantsStruct::BOTH );
    }

    TEST_CASE( "Push commands coalesce compatible slots without changing conflicts", "[push_constants]" )
    {
        GFX::SendPushConstantsCommand lhs, rhs;
        lhs._fastData.data[0] = MAT4_IDENTITY;
        lhs._fastData.computeFlags();
        rhs._fastData.data[1] = MAT4_ZERO;
        rhs._fastData.computeFlags();
        REQUIRE( GFX::Merge( &lhs, &rhs ) );
        CHECK( lhs._fastData.data[0] == MAT4_IDENTITY );
        CHECK( lhs._fastData.data[1] == MAT4_ZERO );
        CHECK( lhs._fastData.dataFlags() == PushConstantsStruct::BOTH );

        rhs = {};
        rhs._fastData.data[0] = MAT4_ZERO;
        rhs._fastData.computeFlags();
        const PushConstantsStruct before = lhs._fastData;
        CHECK_FALSE( GFX::Merge( &lhs, &rhs ) );
        CHECK( lhs._fastData == before );

        rhs._fastData.data[0] = MAT4_IDENTITY;
        rhs._fastData.computeFlags();
        CHECK( GFX::Merge( &lhs, &rhs ) );
        CHECK( lhs._fastData == before );
    }

    TEST_CASE( "Optional Vulkan dynamic state requires every consumed feature", "[vulkan_features]" )
    {
        for ( U8 mask = 0; mask < 8; ++mask )
        {
            VkPhysicalDeviceExtendedDynamicState3FeaturesEXT features{};
            features.extendedDynamicState3ColorBlendEnable = (mask & 1u) != 0u;
            features.extendedDynamicState3ColorBlendEquation = (mask & 2u) != 0u;
            features.extendedDynamicState3ColorWriteMask = (mask & 4u) != 0u;
            CHECK( VKDevice::HasRequiredDynamicState3Features( features ) == (mask == 7u) );
        }
    }

    TEST_CASE( "Partial push constants preserve previously supplied slots", "[push_constants]" )
    {
        PushConstantsStruct cached, update;
        update.data[0] = MAT4_IDENTITY;
        REQUIRE( update.computeFlags() == PushConstantsStruct::FIRST );
        cached.merge( update );
        CHECK( cached.data[0] == MAT4_IDENTITY );
        CHECK( cached.data[1] == MAT4_NEGATIVE_ONE );

        update = {};
        update.data[1] = MAT4_ZERO;
        REQUIRE( update.computeFlags() == PushConstantsStruct::SECOND );
        cached.merge( update );
        CHECK( cached.data[0] == MAT4_IDENTITY );
        CHECK( cached.data[1] == MAT4_ZERO );
        CHECK( cached.dataFlags() == PushConstantsStruct::BOTH );

        update = {};
        cached.merge( update );
        CHECK( cached.data[0] == MAT4_IDENTITY );
        CHECK( cached.data[1] == MAT4_ZERO );
        update.data[0] = MAT4_ZERO;
        update.computeFlags();
        cached.merge( update );
        CHECK( cached.data[1] == MAT4_ZERO );
        CHECK( cached.data[0] == MAT4_ZERO );
    }

    TEST_CASE( "Vulkan texture aspects follow packing, not attachment usage", "[texture_fallbacks]" )
    {
        TextureDescriptor descriptor;
        CHECK( vkTexture::GetAspectFlags( descriptor ) == VK_IMAGE_ASPECT_COLOR_BIT );
        descriptor._packing = GFXImagePacking::DEPTH;
        CHECK( vkTexture::GetAspectFlags( descriptor ) == VK_IMAGE_ASPECT_DEPTH_BIT );
        descriptor._packing = GFXImagePacking::DEPTH_STENCIL;
        CHECK( vkTexture::GetAspectFlags( descriptor ) == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) );
    }

    TEST_CASE( "Sampler reflection retains fallback compatibility traits", "[texture_fallbacks]" )
    {
        struct CompilerScope
        {
            bool _owned{ !SpirvHelper::s_isInit };
            CompilerScope() { if ( _owned ) SpirvHelper::Init(); }
            ~CompilerScope() { if ( _owned ) SpirvHelper::Finalize(); }
        } compiler;
        const char* source = R"(#version 450
layout(set = 3, binding = 0) uniform sampler2DArray sArray;
layout(set = 3, binding = 1) uniform samplerCube sCube;
layout(set = 3, binding = 2) uniform samplerCubeArray sCubeArray;
layout(set = 3, binding = 3) uniform isampler3D sInteger;
layout(set = 3, binding = 4) uniform usampler1D sUnsigned;
layout(set = 3, binding = 5) uniform sampler2DShadow sDepth;
layout(set = 3, binding = 6) uniform sampler2DMS sMSAA;
layout(location = 0) out vec4 colour;
void main() {
    colour = texture(sArray, vec3(0.5, 0.5, 0.0))
           + texture(sCube, vec3(1.0, 0.0, 0.0))
           + texture(sCubeArray, vec4(1.0, 0.0, 0.0, 0.0))
           + vec4(texture(sInteger, vec3(0.5)))
           + vec4(texture(sUnsigned, 0.5))
           + vec4(texture(sDepth, vec3(0.5)))
           + texelFetch(sMSAA, ivec2(0), 0);
}
)";
        std::vector<unsigned int> spirv;
        REQUIRE( SpirvHelper::GLSLtoSPV( ShaderType::FRAGMENT, source, spirv, RenderAPI::Vulkan ) );
        Reflection::Data data;
        REQUIRE( SpirvHelper::BuildReflectionData( ShaderType::FRAGMENT, spirv, RenderAPI::Vulkan, data ) );
        const std::array expectedTypes{
            TextureType::TEXTURE_2D_ARRAY, TextureType::TEXTURE_CUBE_MAP, TextureType::TEXTURE_CUBE_ARRAY,
            TextureType::TEXTURE_3D, TextureType::TEXTURE_1D, TextureType::TEXTURE_2D, TextureType::TEXTURE_2D
        };
        REQUIRE( data._images.size() == expectedTypes.size() );
        for ( U8 slot = 0u; slot < expectedTypes.size(); ++slot )
        {
            const auto image = std::find_if( data._images.begin(), data._images.end(), [slot](const Reflection::ImageEntry& it)
            {
                return it._bindingSlot == slot;
            } );
            REQUIRE( image != data._images.end() );
            CHECK( image->_textureType == expectedTypes[slot] );
            CHECK( image->_combinedImageSampler );
            CHECK( image->_isMultiSampled == (slot == 6u) );
            CHECK( image->_isDepth == (slot == 5u) );
            CHECK( image->_sampledDataType == (slot == 3u ? GFXDataFormat::SIGNED_INT :
                                             slot == 4u ? GFXDataFormat::UNSIGNED_INT : GFXDataFormat::UNSIGNED_BYTE) );
        }
    }

    TEST_CASE( "Default sampler textures preserve dimensionality and sample format", "[texture_fallbacks]" )
    {
        for ( U8 type = 0u; type < to_base(TextureType::COUNT); ++type )
        {
            const TextureType textureType = static_cast<TextureType>(type);
            const TextureDescriptor colour = Texture::DefaultTextureDescriptor( textureType, GFXDataFormat::UNSIGNED_BYTE, false );
            CHECK( colour._texType == textureType );
            CHECK( colour._layerCount == 1u );
            CHECK( colour._msaaSamples == 0u );
            CHECK( colour._mipMappingState == MipMappingState::OFF );
            CHECK( colour._baseFormat == GFXImageFormat::RGBA );
            CHECK( colour._packing == GFXImagePacking::NORMALIZED );
            for ( const GFXDataFormat dataType : {GFXDataFormat::SIGNED_INT, GFXDataFormat::UNSIGNED_INT} )
            {
                const TextureDescriptor integer = Texture::DefaultTextureDescriptor( textureType, dataType, false );
                CHECK( integer._texType == textureType );
                CHECK( integer._dataType == dataType );
                CHECK( integer._packing == GFXImagePacking::UNNORMALIZED );
            }
            if ( textureType != TextureType::TEXTURE_3D )
            {
                const TextureDescriptor depth = Texture::DefaultTextureDescriptor( textureType, GFXDataFormat::UNSIGNED_BYTE, true );
                CHECK( depth._texType == textureType );
                CHECK( depth._baseFormat == GFXImageFormat::RED );
                CHECK( depth._dataType == GFXDataFormat::FLOAT_32 );
                CHECK( depth._packing == GFXImagePacking::DEPTH );
            }
        }
    }

    TEST_CASE( "Texture update source layouts", "[texture_updates]" )
    {
        TextureDescriptor descriptor;
        TextureUpdateLayout layout;

        SECTION( "compact RGBA rectangle uses region size, not texture size" )
        {
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 12u );
            CHECK( layout._rowStride == 12u );
            CHECK( layout._sourceOffset == 0u );
            CHECK( layout._stagingSize == 24u );
            CHECK( layout._requiredSize == 24u );
        }
        SECTION( "full atlas buffer uses texel row length and source skips" )
        {
            const PixelAlignment alignment{ ._rowLength = 64u, ._skipPixels = 5u, ._skipRows = 7u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, alignment, layout ) );
            CHECK( layout._rowStride == 256u );
            CHECK( layout._sourceOffset == 1812u );
            CHECK( layout._requiredSize == 2080u );
            CHECK( layout._stagingSize == 24u );
        }
        SECTION( "RGB row alignment rounds up instead of multiplying" )
        {
            descriptor._baseFormat = GFXImageFormat::RGB;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 9u );
            CHECK( layout._rowStride == 12u );
            CHECK( layout._requiredSize == 21u );
            CHECK( layout._stagingSize == 18u );
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 1u }, layout ) );
            CHECK( layout._requiredSize == 18u );
        }
        SECTION( "single-channel font atlas" )
        {
            descriptor._baseFormat = GFXImageFormat::RED;
            const PixelAlignment alignment{ ._alignment = 1u, ._rowLength = 64u, ._skipPixels = 5u, ._skipRows = 7u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, alignment, layout ) );
            CHECK( layout._sourceOffset == 453u );
            CHECK( layout._requiredSize == 520u );
            CHECK( layout._stagingSize == 6u );
        }
        SECTION( "multiple layers or depth slices retain source pitch" )
        {
            const PixelAlignment alignment{ ._rowLength = 8u, ._skipPixels = 1u, ._skipRows = 1u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {2u, 3u, 2u}, alignment, layout ) );
            CHECK( layout._sliceStride == 96u );
            CHECK( layout._sourceOffset == 36u );
            CHECK( layout._requiredSize == 204u );
            CHECK( layout._stagingSize == 48u );
        }
        SECTION( "packed pixels respect requested row alignment" )
        {
            descriptor._packing = GFXImagePacking::RGBA_4444;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 1u }, layout ) );
            CHECK( layout._rowBytes == 6u );
            CHECK( layout._rowStride == 6u );
            CHECK( layout._requiredSize == 12u );
        }
        SECTION( "R11G11B10 uploads describe the shared half-float source" )
        {
            descriptor._baseFormat = GFXImageFormat::RGB;
            descriptor._dataType = GFXDataFormat::FLOAT_16;
            descriptor._packing = GFXImagePacking::RGB_111110F;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 18u );
            CHECK( layout._rowStride == 20u );
            CHECK( layout._requiredSize == 38u );
            CHECK( layout._stagingSize == 36u );
        }
        SECTION( "1D uploads ignore row skips like OpenGL" )
        {
            descriptor._texType = TextureType::TEXTURE_1D;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 1u, 1u}, { ._rowLength = 8u, ._skipPixels = 2u, ._skipRows = 7u }, layout ) );
            CHECK( layout._sourceOffset == 8u );
            CHECK( layout._requiredSize == 20u );
        }
        SECTION( "BC blocks include incomplete edge blocks" )
        {
            descriptor._baseFormat = GFXImageFormat::BC1;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {7u, 5u, 2u}, {}, layout ) );
            CHECK( layout._rowBytes == 16u );
            CHECK( layout._rowCount == 2u );
            CHECK( layout._requiredSize == 64u );
            descriptor._baseFormat = GFXImageFormat::BC3;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {7u, 5u, 2u}, {}, layout ) );
            CHECK( layout._requiredSize == 128u );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {4u, 4u, 1u}, { ._rowLength = 8u }, layout ) );
        }
        SECTION( "invalid and overflowing pixel-store inputs are rejected" )
        {
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 3u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = 2u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = 4u, ._skipPixels = 2u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = SIZE_MAX }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._skipRows = SIZE_MAX }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {0u, 2u, 1u}, {}, layout ) );
        }
    }

    TEST_CASE( "Texture update mip and layer bounds", "[texture_updates]" )
    {
        SECTION( "nonzero mip coordinates are already mip-local" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 2u, {13u, 6u, 0u}, {3u, 2u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 2u, {14u, 6u, 0u}, {3u, 2u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, ALL_MIPS, {}, {1u, 1u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 7u, {}, {1u, 1u, 1u} ) );
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 6u, {}, {1u, 1u, 1u} ) );
        }
        SECTION( "array layers do not shrink with mips" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D_ARRAY, {64u, 32u, 1u}, 4u, 7u, 2u, {0u, 0u, 2u}, {4u, 4u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D_ARRAY, {64u, 32u, 1u}, 4u, 7u, 2u, {0u, 0u, 3u}, {4u, 4u, 2u} ) );
        }
        SECTION( "cube z selects individual faces" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_CUBE_ARRAY, {32u, 32u, 1u}, 2u, 6u, 1u, {0u, 0u, 11u}, {4u, 4u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_CUBE_ARRAY, {32u, 32u, 1u}, 2u, 6u, 1u, {0u, 0u, 12u}, {4u, 4u, 1u} ) );
        }
        SECTION( "3D depth shrinks with mips" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_3D, {32u, 32u, 8u}, 1u, 6u, 1u, {0u, 0u, 2u}, {4u, 4u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_3D, {32u, 32u, 8u}, 1u, 6u, 1u, {0u, 0u, 3u}, {4u, 4u, 2u} ) );
        }
        SECTION( "1D arrays use z for layers" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_1D_ARRAY, {32u, 1u, 1u}, 4u, 6u, 1u, {0u, 0u, 2u}, {4u, 1u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_1D_ARRAY, {32u, 1u, 1u}, 4u, 6u, 1u, {0u, 1u, 2u}, {4u, 1u, 2u} ) );
        }
        SECTION( "zero extents and wrapped coordinates are invalid" )
        {
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 0u, {}, {1u, 0u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 0u, {65535u, 0u, 0u}, {2u, 1u, 1u} ) );
        }
    }

    struct VKAPITestAccessor
    {
        static VKTransferQueue& queue() noexcept { return VK_API::s_transferQueue; }
    };

    TEST_CASE("VKTransferQueue producer non-blocking and Flush drains", "[vk_transfer_queue]") {
        // Drain any leftover state
        VKTransferQueue::TransferRequest tmp{};

        while (VKAPITestAccessor::queue()._requests.try_dequeue(tmp))
        {
        }

        VKAPITestAccessor::queue()._dirty.store(false, std::memory_order_release);

        SECTION("single enqueue is non-blocking and visible")
        {
            VKTransferQueue::TransferRequest req{};
            req.srcBuffer = VK_NULL_HANDLE; // valid request form for this test

            VK_API::RegisterTransferRequest(req);

            // Immediately visible on the lock-free queue
            REQUIRE(VKAPITestAccessor::queue()._requests.try_dequeue(tmp));
        }

        SECTION("Flush drains all queued requests and clears dirty")
        {
            // Enqueue multiple requests
            constexpr size_t N = 128;
            VKTransferQueue::TransferRequest req{};
            req.srcBuffer = VK_NULL_HANDLE;

            for (size_t i = 0; i < N; ++i)
            {
                VK_API::RegisterTransferRequest(req);
            }

            // Sanity: producers published work
            REQUIRE(VKAPITestAccessor::queue()._dirty.load(std::memory_order_acquire));

            // Call the consumer (simulating the render thread). VK_NULL_HANDLE is acceptable for this unit test.
            VK_API::FlushBufferTransferRequests(VK_NULL_HANDLE);

            // After Flush, queue should be drained and dirty cleared.
            REQUIRE(!VKAPITestAccessor::queue()._dirty.load(std::memory_order_acquire));
            REQUIRE(!VKAPITestAccessor::queue()._requests.try_dequeue(tmp));
        }
    }
} //namespace Divide
