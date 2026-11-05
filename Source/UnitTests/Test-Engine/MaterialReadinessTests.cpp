// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/TaskPool.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Platform/Headers/ConditionalWait.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide
{
    struct MaterialTestAccess
    {
        using TransparencyInputs = Material::TransparencyInputs;

        [[nodiscard]] static ShaderProgramInfo& Slot( Material& material, const RenderStagePass pass )
        {
            return material.shaderInfo( pass );
        }

        static void SetTexturePresence( Material& material, const TextureSlot slot, const bool present, const bool useInGeometryPasses = false )
        {
            Material::TextureInfo& texture = material._textures[to_base( slot )];
            texture._ptr = present ? Handle<Texture>{ ._generation = 0u, ._index = 1u } : INVALID_HANDLE<Texture>;
            texture._useInGeometryPasses = useInGeometryPasses;
        }

        static void AppendTextureDefines( const Material& material, ShaderProgramDescriptor& descriptor, const bool prePass, const bool shadowPass )
        {
            material.appendTextureShaderDefines( descriptor, prePass, shadowPass );
        }

        [[nodiscard]] static bool HasCommonInputs( const ShaderProgramDescriptor& descriptor )
        {
            return Material::HasCommonMaterialInputs( descriptor );
        }

        static void AddCommonBindings( DescriptorSet& descriptor, const ImageView& fallback, const SamplerDescriptor sampler )
        {
            Material::AddMissingCommonMaterialBindings( descriptor, fallback, sampler );
        }

        [[nodiscard]] static TranslucencySource SelectTransparency( const TransparencyInputs& inputs )
        {
            return Material::SelectTranslucencySource( inputs );
        }

        [[nodiscard]] static DescriptorSet& CachedMainDescriptor( Material& material )
        {
            return material._descriptorSetMainPass;
        }
    };

    namespace
    {
        [[nodiscard]] ShaderProgramDescriptor MaterialProgram( const char* vertexFile, const char* fragmentFile, const char* fragmentVariant = "" )
        {
            ShaderProgramDescriptor descriptor{};
            ShaderModuleDescriptor vertex{};
            vertex._moduleType = ShaderType::VERTEX;
            vertex._sourceFile = vertexFile;
            ShaderModuleDescriptor fragment{};
            fragment._moduleType = ShaderType::FRAGMENT;
            fragment._sourceFile = fragmentFile;
            fragment._variant = fragmentVariant;
            descriptor._modules.push_back( vertex );
            descriptor._modules.push_back( fragment );
            return descriptor;
        }

        [[nodiscard]] const DescriptorSetBinding* FindBinding( const DescriptorSet& descriptor, const U8 slot )
        {
            for ( U8 i = 0u; i < descriptor._bindingCount; ++i )
            {
                if ( descriptor._bindings[i]._slot == slot )
                {
                    return &descriptor._bindings[i];
                }
            }
            return nullptr;
        }

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

    TEST_CASE( "Standard material texture presence shares shader identities", "[materials][shader][render_smoke]" )
    {
        Material material{ ResourceDescriptor<Material>{ "common-input-test" } };
        SCOPE_EXIT
        {
            MaterialTestAccess::SetTexturePresence( material, TextureSlot::UNIT0, false );
            MaterialTestAccess::SetTexturePresence( material, TextureSlot::OPACITY, false );
        };

        struct ProgramCase
        {
            const char* _vertex;
            const char* _fragment;
            const char* _variant;
            bool _prePass;
            bool _shadowPass;
        };
        constexpr std::array cases{
            ProgramCase{ "baseVertexShaders.glsl", "material.glsl", "", false, false },
            ProgramCase{ "baseVertexShaders.glsl", "depthPass.glsl", "PrePass", true, false },
            ProgramCase{ "baseVertexShaders.glsl", "depthPass.glsl", "Shadow.VSM", false, true },
            ProgramCase{ "tree.glsl", "material.glsl", "", false, false },
            ProgramCase{ "tree.glsl", "depthPass.glsl", "Shadow.VSM", false, true }
        };
        for ( const ProgramCase& test : cases )
        {
            CAPTURE( test._vertex, test._fragment, test._variant );
            size_t expectedHash = 0u;
            for ( const bool albedo : { false, true } )
            {
                for ( const bool opacity : { false, true } )
                {
                    MaterialTestAccess::SetTexturePresence( material, TextureSlot::UNIT0, albedo );
                    MaterialTestAccess::SetTexturePresence( material, TextureSlot::OPACITY, opacity );
                    ShaderProgramDescriptor descriptor = MaterialProgram( test._vertex, test._fragment, test._variant );
                    MaterialTestAccess::AppendTextureDefines( material, descriptor, test._prePass, test._shadowPass );
                    CHECK( MaterialTestAccess::HasCommonInputs( descriptor ) );
                    if ( !albedo && !opacity )
                    {
                        expectedHash = GetHash( descriptor );
                    }
                    else
                    {
                        CHECK( GetHash( descriptor ) == expectedHash );
                    }
                }
            }
        }
    }

    TEST_CASE( "Custom material programs retain presence-specific texture defines", "[materials][shader][render_smoke]" )
    {
        Material material{ ResourceDescriptor<Material>{ "custom-input-test" } };
        SCOPE_EXIT
        {
            MaterialTestAccess::SetTexturePresence( material, TextureSlot::UNIT0, false );
            MaterialTestAccess::SetTexturePresence( material, TextureSlot::OPACITY, false );
        };

        for ( const ShaderProgramDescriptor& prototype : {
                  MaterialProgram( "terrainTess.glsl", "terrainTess.glsl" ),
                  MaterialProgram( "baseVertexShaders.glsl", "material.glsl", "Custom" ),
                  MaterialProgram( "baseVertexShaders.glsl", "depthPass.glsl", "LineariseDepthBuffer" ) } )
        {
            ShaderProgramDescriptor missing = prototype;
            MaterialTestAccess::AppendTextureDefines( material, missing, false, false );
            CHECK_FALSE( MaterialTestAccess::HasCommonInputs( missing ) );
            MaterialTestAccess::SetTexturePresence( material, TextureSlot::UNIT0, true );
            ShaderProgramDescriptor present = prototype;
            MaterialTestAccess::AppendTextureDefines( material, present, false, false );
            CHECK_FALSE( MaterialTestAccess::HasCommonInputs( present ) );
            CHECK( GetHash( present ) != GetHash( missing ) );
            MaterialTestAccess::SetTexturePresence( material, TextureSlot::UNIT0, false );
        }

        ShaderProgramDescriptor multistage = MaterialProgram( "baseVertexShaders.glsl", "material.glsl" );
        ShaderModuleDescriptor geometry{};
        geometry._moduleType = ShaderType::GEOMETRY;
        geometry._sourceFile = "custom.glsl";
        multistage._modules.push_back( geometry );
        MaterialTestAccess::AppendTextureDefines( material, multistage, false, false );
        CHECK_FALSE( MaterialTestAccess::HasCommonInputs( multistage ) );
        CHECK( multistage._globalDefines.empty() );

        material.properties().texturesInFragmentStageOnly( false );
        ShaderProgramDescriptor allStages = MaterialProgram( "baseVertexShaders.glsl", "material.glsl" );
        allStages._globalDefines.emplace_back( "NEED_TEXTURE_DATA_ALL_STAGES", true );
        MaterialTestAccess::AppendTextureDefines( material, allStages, false, false );
        CHECK_FALSE( MaterialTestAccess::HasCommonInputs( allStages ) );
        CHECK( allStages._globalDefines.size() == 1u );

        material.properties().texturesInFragmentStageOnly( true );
        ShaderProgramDescriptor customDefine = MaterialProgram( "baseVertexShaders.glsl", "material.glsl" );
        customDefine._modules.front()._defines.emplace_back( "NEED_TEXTURE_DATA_ALL_STAGES", true );
        MaterialTestAccess::AppendTextureDefines( material, customDefine, false, false );
        CHECK_FALSE( MaterialTestAccess::HasCommonInputs( customDefine ) );
        CHECK( customDefine._globalDefines.empty() );

        ShaderProgramDescriptor valueDefine = MaterialProgram( "baseVertexShaders.glsl", "material.glsl" );
        valueDefine._modules.front()._defines.emplace_back( "NEED_TEXTURE_DATA_ALL_STAGES 1", true );
        MaterialTestAccess::AppendTextureDefines( material, valueDefine, false, false );
        CHECK_FALSE( MaterialTestAccess::HasCommonInputs( valueDefine ) );
        CHECK( valueDefine._globalDefines.empty() );

        ShaderProgramDescriptor rawDefine = MaterialProgram( "baseVertexShaders.glsl", "material.glsl" );
        rawDefine._modules.front()._defines.emplace_back( "#define NEED_TEXTURE_DATA_ALL_STAGES", false );
        MaterialTestAccess::AppendTextureDefines( material, rawDefine, false, false );
        CHECK_FALSE( MaterialTestAccess::HasCommonInputs( rawDefine ) );
        CHECK( rawDefine._globalDefines.empty() );

        ShaderProgramDescriptor vertexOnly = MaterialProgram( "baseVertexShaders.glsl", "material.glsl" );
        vertexOnly._modules.pop_back();
        MaterialTestAccess::AppendTextureDefines( material, vertexOnly, false, false );
        CHECK_FALSE( MaterialTestAccess::HasCommonInputs( vertexOnly ) );
        CHECK( vertexOnly._globalDefines.empty() );
    }

    TEST_CASE( "Missing common material bindings replace previous draw slots", "[materials][descriptors][render_smoke]" )
    {
        ImageView previous{};
        previous._targetType = TextureType::TEXTURE_CUBE_MAP;
        ImageView current{};
        current._targetType = TextureType::TEXTURE_2D_ARRAY;
        current._subRange._layerRange._count = 4u;
        ImageView fallback{};
        fallback._targetType = TextureType::TEXTURE_2D_ARRAY;
        fallback._subRange._layerRange._count = 1u;
        const SamplerDescriptor sampler{};

        DescriptorSet prior{};
        Set( AddBinding( prior, 0u, ShaderStageVisibility::FRAGMENT )._data, previous, sampler );
        Set( AddBinding( prior, 1u, ShaderStageVisibility::FRAGMENT )._data, previous, sampler );
        GFXDevice::GFXDescriptorSet merged{};
        merged.update( DescriptorSetUsage::PER_DRAW, prior );

        DescriptorSet next{};
        Set( AddBinding( next, 0u, ShaderStageVisibility::FRAGMENT )._data, current, sampler );
        MaterialTestAccess::AddCommonBindings( next, fallback, sampler );
        MaterialTestAccess::AddCommonBindings( next, fallback, sampler );
        REQUIRE( next._bindingCount == 2u );
        merged.update( DescriptorSetUsage::PER_DRAW, next );

        const DescriptorSetBinding* albedo = FindBinding( merged.impl(), 0u );
        const DescriptorSetBinding* opacity = FindBinding( merged.impl(), 1u );
        REQUIRE( albedo != nullptr );
        REQUIRE( opacity != nullptr );
        CHECK( albedo->_data._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER );
        CHECK( opacity->_data._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER );
        CHECK( albedo->_data._sampledImage._image == current );
        CHECK( opacity->_data._sampledImage._image == fallback );

        DescriptorSet noTextures{};
        MaterialTestAccess::AddCommonBindings( noTextures, fallback, sampler );
        merged.update( DescriptorSetUsage::PER_DRAW, noTextures );
        albedo = FindBinding( merged.impl(), 0u );
        opacity = FindBinding( merged.impl(), 1u );
        REQUIRE( albedo != nullptr );
        REQUIRE( opacity != nullptr );
        CHECK( albedo->_data._sampledImage._image == fallback );
        CHECK( opacity->_data._sampledImage._image == fallback );
    }

    TEST_CASE( "Material descriptor copies replace old output and update visibility", "[materials][descriptors][render_smoke]" )
    {
        Material material{ ResourceDescriptor<Material>{ "descriptor-copy-test" } };
        const RenderStagePass pass{ RenderStage::DISPLAY, RenderPassType::MAIN_PASS };
        const ShaderProgramDescriptor custom{};
        DescriptorSet result{};
        ImageView placeholder{};
        placeholder._targetType = TextureType::TEXTURE_2D_ARRAY;
        const SamplerDescriptor sampler{};
        Set( AddBinding( result, 0u, ShaderStageVisibility::FRAGMENT )._data, placeholder, sampler );
        REQUIRE( material.getDescriptorSet( pass, custom, result ) );
        CHECK( result._bindingCount == 0u );

        DescriptorSet& cached = MaterialTestAccess::CachedMainDescriptor( material );
        Set( AddBinding( cached, 4u, ShaderStageVisibility::FRAGMENT )._data, placeholder, sampler );
        REQUIRE( material.getDescriptorSet( pass, custom, result ) );
        REQUIRE( result._bindingCount == 1u );
        CHECK( result._bindings[0]._shaderStageVisibility == to_base( ShaderStageVisibility::FRAGMENT ) );
        material.properties().texturesInFragmentStageOnly( false );
        REQUIRE( material.getDescriptorSet( pass, custom, result ) );
        CHECK( result._bindings[0]._shaderStageVisibility == to_base( ShaderStageVisibility::ALL_DRAW ) );
        material.properties().baseColour( FColour4( 1.f, 1.f, 1.f, 0.5f ) );
        static_cast<void>( material.update( 0u ) );
        CHECK( cached._bindingCount == 0u );
    }

    TEST_CASE( "Material alpha selection honours active texture operations", "[materials][transparency][render_smoke]" )
    {
        MaterialTestAccess::TransparencyInputs inputs{};
        inputs._baseAlpha = 0.5f;
        inputs._albedoOperation = TextureOperation::REPLACE;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::ALBEDO_COLOUR );

        inputs._hasAlbedo = true;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::COUNT );
        inputs._albedoHasTransparency = true;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::ALBEDO_TEX );
        inputs._albedoOperation = TextureOperation::MULTIPLY;
        inputs._hasOpacity = true;
        inputs._opacityOperation = TextureOperation::MULTIPLY;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::OPACITY_MAP_R );
        inputs._opacityUsesAlpha = true;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::OPACITY_MAP_A );
        inputs._opacityOperation = TextureOperation::NONE;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::ALBEDO_TEX );
        inputs._ignoreAlbedoAlpha = true;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::ALBEDO_COLOUR );
        inputs._albedoOperation = TextureOperation::NONE;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::ALBEDO_COLOUR );
        inputs._enabled = false;
        CHECK( MaterialTestAccess::SelectTransparency( inputs ) == TranslucencySource::COUNT );
    }

    TEST_CASE( "Material runtime alpha updates preserve pending shader changes", "[materials][transparency][render_smoke]" )
    {
        Material material{ ResourceDescriptor<Material>{ "transparency-dirty-test" } };
        static_cast<void>( material.update( 0u ) );
        material.properties().isStatic( true );
        material.properties().baseColour( DefaultColours::WHITE );
        const U32 pendingUpdate = material.update( 0u );
        CHECK( ( pendingUpdate & to_base( MaterialUpdateResult::NEW_SHADER ) ) != 0u );

        material.properties().baseColour( FColour4( 1.f, 1.f, 1.f, 0.5f ) );
        material.setTextureOperation( TextureSlot::UNIT0, TextureOperation::REPLACE );
        CHECK( material.properties().transparencyUpdated() );
        static_cast<void>( material.update( 0u ) );
        CHECK( material.properties().translucencySource() == TranslucencySource::ALBEDO_COLOUR );

        material.properties().ignoreTexDiffuseAlpha( true );
        CHECK( material.properties().transparencyUpdated() );
        static_cast<void>( material.update( 0u ) );
        CHECK_FALSE( material.properties().transparencyUpdated() );
    }

    TEST_CASE( "Missing common textures pack disabled runtime operations", "[materials][transparency][render_smoke]" )
    {
        Material material{ ResourceDescriptor<Material>{ "missing-operations-test" } };
        material.setTextureOperation( TextureSlot::UNIT0, TextureOperation::REPLACE );
        material.setTextureOperation( TextureSlot::OPACITY, TextureOperation::MULTIPLY );
        NodeMaterialData data{};
        material.getData( 0u, data );
        CHECK( Util::UNPACK_UNORM4x8_U8( data._textureOperations.x ).x == to_U8( TextureOperation::NONE ) );
        CHECK( Util::UNPACK_UNORM4x8_U8( data._textureOperations.y ).w == to_U8( TextureOperation::NONE ) );

        MaterialTestAccess::SetTexturePresence( material, TextureSlot::UNIT0, true );
        MaterialTestAccess::SetTexturePresence( material, TextureSlot::OPACITY, true );
        SCOPE_EXIT
        {
            MaterialTestAccess::SetTexturePresence( material, TextureSlot::UNIT0, false );
            MaterialTestAccess::SetTexturePresence( material, TextureSlot::OPACITY, false );
        };
        material.getData( 0u, data );
        CHECK( Util::UNPACK_UNORM4x8_U8( data._textureOperations.x ).x == to_U8( TextureOperation::REPLACE ) );
        CHECK( Util::UNPACK_UNORM4x8_U8( data._textureOperations.y ).w == to_U8( TextureOperation::MULTIPLY ) );
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
