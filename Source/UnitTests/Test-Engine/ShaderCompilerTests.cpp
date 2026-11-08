/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Console.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/OcclusionCullDispatch.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Shaders/Headers/GLSLToSPIRV.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Shaders/glsw/Headers/glsw.h"

#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <spirv-reflect/spirv_reflect.h>

#include <iostream>

#if defined(IS_WINDOWS_BUILD) && defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace Divide
{
struct ShaderProgramTestAccess
{
    static void InitAtomLocations() { ShaderProgram::InitAtomLocations(); }
    static bool InitThread( const RenderAPI api, const Configuration& config ) { return ShaderProgram::InitShaderThread( api, config ); }
    static bool InitGLSW( const RenderAPI api, const Configuration& config ) { return ShaderProgram::InitGLSW( api, config ); }
    static ShaderProgram::BindingSetData& Bindings() { return ShaderProgram::s_bindingsPerSet; }
    static auto& BindingSlots() { return ShaderProgram::s_bindingSlots; }
    static bool IsStorageBindingWithinLimit( const RenderAPI api, const U8 binding, const U32 limit )
    {
        return ShaderProgram::IsStorageBindingWithinLimit( api, binding, limit );
    }
    static ResourcePath& CommonAtomLocation() { return ShaderProgram::shaderAtomLocationPrefix[to_base( ShaderType::COUNT )]; }

    static void ExpandIncludes( string& source, eastl::set<U64>& atoms )
    {
        ShaderProgram::PreprocessIncludes( "shader test", source, atoms );
    }

    static void Parse( const RenderAPI api, const ModuleDefines& defines, ShaderProgram::LoadData& data,
                       eastl::set<U64>& atoms, ShaderProgram::UniformBlockLayouts& uniformBlocks )
    {
        ShaderProgram::loadAndParseGLSL( "shader test", api, defines, data, uniformBlocks, atoms );
    }

    static void Parse( const RenderAPI api, const ModuleDefines& defines, ShaderProgram::LoadData& data,
                       eastl::set<U64>& atoms )
    {
        ShaderProgram::UniformBlockLayouts uniformBlocks{};
        Parse( api, defines, data, atoms, uniformBlocks );
    }

    static U8 FindUniformBlockIndex( const ShaderProgram::UniformBlockLayouts& uniformBlocks,
                                     const Reflection::UniformsSet& uniforms )
    {
        return ShaderProgram::FindUniformBlockIndex( uniformBlocks, uniforms );
    }

    static bool CachedUniformBlockMatches( const ShaderProgram::UniformBlockLayouts& uniformBlocks,
                                           const ShaderProgram::LoadData& data, U8& blockIndex )
    {
        return ShaderProgram::CachedUniformBlockMatches( uniformBlocks, data, blockIndex );
    }

    static Reflection::PushConstantsState DetectPushConstantsUsage( const string& source, const RenderAPI api )
    {
        return ShaderProgram::DetectPushConstantsUsage( source, api );
    }

    static bool ReadCachedSPIRV( std::istream& stream, std::vector<SpvWord>& spirv )
    {
        return ShaderProgram::ReadCachedSPIRV( stream, spirv );
    }

    static void ClearAtoms()
    {
        ShaderProgram::s_atoms.clear();
    }

    static void EraseAtom( const U64 atomID ) { ShaderProgram::EraseAtom( atomID ); }
};

namespace
{
    struct NonInteractiveAssertions
    {
#if defined(IS_WINDOWS_BUILD) && defined(_MSC_VER) && defined(_DEBUG)
        NonInteractiveAssertions()
            : _errorMode( _set_error_mode( _REPORT_ERRMODE ) )
            , _abortBehavior( _set_abort_behavior( 0, 0 ) )
            , _errorReportMode( _CrtSetReportMode( _CRT_ERROR, _CRTDBG_REPORT_MODE ) )
            , _errorReportFile( _CrtSetReportFile( _CRT_ERROR, _CRTDBG_REPORT_FILE ) )
            , _assertReportMode( _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_REPORT_MODE ) )
            , _assertReportFile( _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_REPORT_FILE ) )
        {
            _saved = _errorMode != -1 && _errorReportMode != -1 && _assertReportMode != -1 &&
                     _errorReportFile != _CRTDBG_HFILE_ERROR && _assertReportFile != _CRTDBG_HFILE_ERROR;
            if ( _saved )
            {
                _ready = _set_error_mode( _OUT_TO_STDERR ) != -1 &&
                         _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR ) != _CRTDBG_HFILE_ERROR &&
                         _CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG ) != -1 &&
                         _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR ) != _CRTDBG_HFILE_ERROR &&
                         _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG ) != -1;
                _set_abort_behavior( 0, _CALL_REPORTFAULT );
            }
        }

        ~NonInteractiveAssertions()
        {
            if ( _saved )
            {
                _set_error_mode( _errorMode );
                _set_abort_behavior( _abortBehavior, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
                _CrtSetReportFile( _CRT_ERROR, _errorReportFile );
                _CrtSetReportMode( _CRT_ERROR, _errorReportMode );
                _CrtSetReportFile( _CRT_ASSERT, _assertReportFile );
                _CrtSetReportMode( _CRT_ASSERT, _assertReportMode );
            }
        }

        int _errorMode;
        unsigned int _abortBehavior;
        int _errorReportMode;
        _HFILE _errorReportFile;
        int _assertReportMode;
        _HFILE _assertReportFile;
        bool _saved{ false };
        bool _ready{ false };
#else
        bool _ready{ true };
#endif
    };

    struct ShaderCompilerLifetime
    {
        ShaderCompilerLifetime() : _ownsInitialization( !SpirvHelper::s_isInit )
        {
            if ( _ownsInitialization )
            {
                SpirvHelper::Init();
            }
        }

        ~ShaderCompilerLifetime()
        {
            if ( _ownsInitialization )
            {
                SpirvHelper::Finalize();
            }
        }

        bool _ownsInitialization;
    };

    struct ConsoleScope
    {
        ConsoleScope()
            : _out( std::cout.rdbuf() )
            , _err( std::cerr.rdbuf() )
        {
            for ( size_t i = 0u; i < _flags.size(); ++i )
            {
                _previousFlags[i] = Console::IsFlagSet( _flags[i] );
            }
#if defined(IS_WINDOWS_BUILD)
            constexpr std::string_view nullDevice = "NUL";
#else
            constexpr std::string_view nullDevice = "/dev/null";
#endif
            Console::Start( ResourcePath{}, nullDevice, nullDevice, false );
            std::cout.rdbuf( _out );
            std::cerr.rdbuf( _err );
            Console::ToggleFlag( Console::Flags::PRINT_IMMEDIATE, true );
            Console::ToggleFlag( Console::Flags::PRINT_TO_STD_CONSOLE, false );
        }

        ~ConsoleScope()
        {
            Console::Flush();
            Console::Stop();
            std::cout.rdbuf( _out );
            std::cerr.rdbuf( _err );
            for ( size_t i = 0u; i < _flags.size(); ++i )
            {
                Console::ToggleFlag( _flags[i], _previousFlags[i] );
            }
        }

        static constexpr std::array<Console::Flags, 9u> _flags{{
            Console::Flags::DECORATE_TIMESTAMP, Console::Flags::DECORATE_THREAD_ID,
            Console::Flags::DECORATE_SEVERITY, Console::Flags::DECORATE_FRAME,
            Console::Flags::ENABLE_OUTPUT, Console::Flags::ENABLE_ERROR_STREAM,
            Console::Flags::PRINT_IMMEDIATE, Console::Flags::FLUSH_ON_PRINT,
            Console::Flags::PRINT_TO_STD_CONSOLE
        }};
        std::array<bool, _flags.size()> _previousFlags{};
        std::streambuf* _out;
        std::streambuf* _err;
    };
}

namespace
{
    namespace SSAOCompositionFixture
    {
        using vec3 = float3;
        using vec4 = float4;
        using std::exp;
        using std::max;

        struct NodeMaterialData {};
        struct IBLGradients {};
        struct PBRMaterial
        {
            vec3 _emissive;
            F32 _roughness;
        };
        struct FogDetails
        {
            vec4 _colourAndDensity;
            vec4 _colourSunScatter;
        };
        struct Varyings
        {
            vec4 _vertexW;
        };

        constexpr F32 M_EPSILON = 1e-5f;
        vec3 dvd_CameraPosition{};
        FogDetails dvd_fogDetails{};
        Varyings VAR{};
        vec3 indirect{};
        vec3 direct{};
        vec3 reflection{};
        F32 visibility = 1.f;
        F32 reflectionConfidence = 0.f;

        F32 dot( const vec3& a, const vec3& b ) { return a.dot( b ); }
        F32 distance( const vec3& a, const vec3& b ) { return a.distance( b ); }
        vec3 normalize( vec3 v ) { v.normalize(); return v; }
        vec3 mix( const vec3& a, const vec3& b, const F32 weight ) { return a * ( 1.f - weight ) + b * weight; }
        U32 dvd_ProbeIndex( const NodeMaterialData& ) { return 0u; }
        bool dvd_ReceivesShadows( const NodeMaterialData& ) { return true; }
        vec3 ApplyIBL( const PBRMaterial&, const vec3&, const vec3&, F32, const vec3&, U32,
                       const vec4&, const vec4&, const IBLGradients& ) { return indirect; }
        vec3 ApplySSR( const F32 roughness, const vec3& input )
        {
            return mix( input, reflection, reflectionConfidence * ( 1.f - roughness ) );
        }
        vec3 getLightContribution( const PBRMaterial& material, const vec3&, const vec3&, bool,
                                   const vec4&, const vec4&, const vec3& input )
        {
            return input + direct + material._emissive;
        }

#define _MATERIAL_DATA_FRAG_
#define _LIGHTING_CALC_FRAG_
#define in
#define ApplySSAO(R) ((R) * visibility)
#include "../../../Assets/Shaders/GLSL/FragmentAtoms/BRDF.frag"
#undef ApplySSAO
#undef in
#undef _BRDF_FRAG_
#undef _LIGHTING_CALC_FRAG_
#undef _MATERIAL_DATA_FRAG_
    }
}

TEST_CASE( "SSAO attenuates indirect lighting without dimming direct light, emission, reflections, or fog", "[shader][ssao]" )
{
    using namespace SSAOCompositionFixture;
    indirect = float3{ 2.f, 4.f, 8.f };
    direct = float3{ 4.f, 2.f, 3.f };
    reflection = float3{ 10.f, 6.f, 2.f };
    reflectionConfidence = 0.f;
    dvd_CameraPosition = float3{ 0.f };
    dvd_fogDetails = {};
    VAR._vertexW = float4{ 0.f, 10.f, 0.f, 1.f };
    const PBRMaterial material{ float3{ 1.f, 5.f, 7.f }, 0.25f };

    const auto pixel = [&]()
    {
        return getPixelColour( float4{ 0.f, 0.f, 0.f, 0.65f }, {}, float3{ 0.f, 1.f, 0.f }, material,
                               float3{ 0.f, 1.f, 0.f }, {}, float4{ 0.f }, float4{ 0.f } );
    };
    const auto checkPixel = []( const float4& actual, const float3& expected )
    {
        CHECK( actual.r == Catch::Approx( expected.r ) );
        CHECK( actual.g == Catch::Approx( expected.g ) );
        CHECK( actual.b == Catch::Approx( expected.b ) );
        CHECK( actual.a == Catch::Approx( 0.65f ) );
    };

    SECTION( "Ambient visibility preserves direct and emissive radiance" )
    {
        for ( const F32 amount : { 0.f, 0.25f, 1.f } )
        {
            visibility = amount;
            checkPixel( pixel(), indirect * amount + direct + material._emissive );
        }
    }
    SECTION( "Screen-space reflections are blended after ambient occlusion" )
    {
        visibility = 0.f;
        reflectionConfidence = 0.5f;
        const F32 weight = reflectionConfidence * ( 1.f - material._roughness );
        checkPixel( pixel(), reflection * weight + direct + material._emissive );
    }
    SECTION( "Fog colour is applied after lighting rather than occluded" )
    {
        visibility = 0.f;
        dvd_fogDetails._colourAndDensity = float4{ 3.f, 2.f, 1.f, 0.1f };
        dvd_fogDetails._colourSunScatter.a = 0.8f;
        const F32 fogAmount = 0.8f * ( 1.f - std::exp( -1.f ) ) / ( 1.f + M_EPSILON );
        checkPixel( pixel(), mix( direct + material._emissive, dvd_fogDetails._colourAndDensity.rgb, fogAmount ) );
    }
}

TEST_CASE( "OpenGL buffer bindings are unique per namespace across shader stages", "[shader][descriptor][render_smoke]" )
{
    CHECK( ShaderProgramTestAccess::IsStorageBindingWithinLimit( RenderAPI::OpenGL, 15u, 16u ) );
    CHECK_FALSE( ShaderProgramTestAccess::IsStorageBindingWithinLimit( RenderAPI::OpenGL, 16u, 16u ) );
    CHECK( ShaderProgramTestAccess::IsStorageBindingWithinLimit( RenderAPI::Vulkan, 15u, 4u ) );

    const auto oldBindings = ShaderProgramTestAccess::Bindings();
    const auto oldSlots = ShaderProgramTestAccess::BindingSlots();
    SCOPE_EXIT
    {
        ShaderProgramTestAccess::Bindings() = oldBindings;
        ShaderProgramTestAccess::BindingSlots() = oldSlots;
    };

    ShaderProgramTestAccess::Bindings() = {};
    ShaderProgramTestAccess::BindingSlots() = {};
    for ( U8 slot = 0u; slot < 16u; ++slot )
    {
        if ( slot < 6u )
        {
            ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW, slot,
                                                     DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC,
                                                     ShaderStageVisibility::ALL );
            CHECK( ShaderProgram::GetGLBindingForDescriptorSlot( DescriptorSetUsage::PER_DRAW, slot ) == slot );
        }

        const auto visibility = slot % 2u == 0u ? ShaderStageVisibility::COMPUTE : ShaderStageVisibility::FRAGMENT;
        const auto type = slot % 2u == 0u ? DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC :
                                          DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_BATCH, slot, type, visibility );
        CHECK( ShaderProgram::GetGLBindingForDescriptorSlot( DescriptorSetUsage::PER_BATCH, slot ) == slot );

        const auto [storageUsage, storageSlot] = ShaderProgram::GetDescriptorSlotForGLBinding(
            slot, DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC );
        CHECK( storageUsage == DescriptorSetUsage::PER_BATCH );
        CHECK( storageSlot == slot );
        if ( slot < 6u )
        {
            const auto [uniformUsage, uniformSlot] = ShaderProgram::GetDescriptorSlotForGLBinding(
                slot, DescriptorSetBindingType::UNIFORM_BUFFER_STATIC );
            CHECK( uniformUsage == DescriptorSetUsage::PER_DRAW );
            CHECK( uniformSlot == slot );
        }
    }
}

TEST_CASE( "Shader compilation uses the selected backend's SPIR-V environment", "[shader]" )
{
    const ShaderCompilerLifetime compilerLifetime{};
    constexpr char openGLShader[] = R"(
        #version 460
        layout(location = 0) in vec3 position;
        layout(std140, binding = 1) uniform DrawData { mat4 transform; };
        void main() { gl_Position = transform * vec4(position, 1.f); }
    )";
    constexpr char vulkanShader[] = R"(
        #version 460
        layout(location = 0) in vec3 position;
        layout(std140, set = 1, binding = 1) uniform DrawData { mat4 transform; };
        layout(push_constant) uniform PushData { vec4 offset; } pushData;
        void main() { gl_Position = transform * vec4(position, 1.f) + pushData.offset; }
    )";

    std::vector<unsigned int> spirv;
    REQUIRE( SpirvHelper::GLSLtoSPV( ShaderType::VERTEX, openGLShader, spirv, RenderAPI::OpenGL ) );
    REQUIRE( spirv.size() >= 5 );
    CHECK( spirv[0] == 0x07230203u );
    CHECK( spirv[1] == 0x00010000u );

    spirv.clear();
    REQUIRE( SpirvHelper::GLSLtoSPV( ShaderType::VERTEX, vulkanShader, spirv, RenderAPI::Vulkan ) );
    REQUIRE( spirv.size() >= 5 );
    CHECK( spirv[0] == 0x07230203u );
    CHECK( spirv[1] == 0x00010600u );
}

TEST_CASE( "SPIR-V draw-index detection inspects compiled shader modules", "[shader][indirect_batch]" )
{
    const ShaderCompilerLifetime compilerLifetime{};
    constexpr char withoutDrawIndex[] = R"(
        #version 460
        layout(location = 0) in vec3 position;
        void main() { gl_Position = vec4(position, 1.f); }
    )";
    constexpr char withDrawIndex[] = R"(
        #version 460
        layout(location = 0) in vec3 position;
        void main() { gl_Position = vec4(position + vec3(float(gl_DrawID)), 1.f); }
    )";

    std::vector<unsigned int> ordinary;
    std::vector<unsigned int> drawIndex;
    REQUIRE( SpirvHelper::GLSLtoSPV( ShaderType::VERTEX, withoutDrawIndex, ordinary, RenderAPI::Vulkan ) );
    REQUIRE( SpirvHelper::GLSLtoSPV( ShaderType::VERTEX, withDrawIndex, drawIndex, RenderAPI::Vulkan ) );

    bool usesDrawIndex = true;
    REQUIRE( SpirvHelper::UsesDrawIndex( ordinary, usesDrawIndex ) );
    CHECK_FALSE( usesDrawIndex );
    REQUIRE( SpirvHelper::UsesDrawIndex( drawIndex, usesDrawIndex ) );
    CHECK( usesDrawIndex );

    SECTION( "member and decoration-group forms are conservative" )
    {
        auto member = ordinary;
        member.insert( member.end(), { ( 5u << 16u ) | SpvOpMemberDecorate, 1u, 0u,
                                       SpvDecorationBuiltIn, SpvBuiltInDrawIndex } );
        REQUIRE( SpirvHelper::UsesDrawIndex( member, usesDrawIndex ) );
        CHECK( usesDrawIndex );

        auto group = ordinary;
        group.insert( group.end(), { ( 4u << 16u ) | SpvOpDecorate, 1u,
                                     SpvDecorationBuiltIn, SpvBuiltInDrawIndex } );
        REQUIRE( SpirvHelper::UsesDrawIndex( group, usesDrawIndex ) );
        CHECK( usesDrawIndex );
    }
    SECTION( "alternative built-in decoration forms cannot enable batching" )
    {
        for ( const auto opcode : { SpvOpDecorateId, SpvOpDecorateString } )
        {
            auto alternative = ordinary;
            alternative.insert( alternative.end(), { ( 4u << 16u ) | opcode, 1u,
                                                     SpvDecorationBuiltIn, 0u } );
            REQUIRE( SpirvHelper::UsesDrawIndex( alternative, usesDrawIndex ) );
            CHECK( usesDrawIndex );
        }

        auto member = ordinary;
        member.insert( member.end(), { ( 5u << 16u ) | SpvOpMemberDecorateString, 1u, 0u,
                                       SpvDecorationBuiltIn, 0u } );
        REQUIRE( SpirvHelper::UsesDrawIndex( member, usesDrawIndex ) );
        CHECK( usesDrawIndex );
    }
    SECTION( "truncated and malformed instruction streams are rejected" )
    {
        auto broken = ordinary;
        broken.resize( 4u );
        CHECK_FALSE( SpirvHelper::UsesDrawIndex( broken, usesDrawIndex ) );

        broken = ordinary;
        broken[0] = 0u;
        CHECK_FALSE( SpirvHelper::UsesDrawIndex( broken, usesDrawIndex ) );

        broken = ordinary;
        broken.push_back( SpvOpNop );
        CHECK_FALSE( SpirvHelper::UsesDrawIndex( broken, usesDrawIndex ) );

        broken = ordinary;
        broken.push_back( ( 4u << 16u ) | SpvOpDecorate );
        CHECK_FALSE( SpirvHelper::UsesDrawIndex( broken, usesDrawIndex ) );

        broken = ordinary;
        broken.insert( broken.end(), { ( 3u << 16u ) | SpvOpDecorate, 1u, SpvDecorationBuiltIn } );
        CHECK_FALSE( SpirvHelper::UsesDrawIndex( broken, usesDrawIndex ) );

        broken = ordinary;
        broken.insert( broken.end(), { ( 3u << 16u ) | SpvOpDecorateId, 1u, SpvDecorationBuiltIn } );
        CHECK_FALSE( SpirvHelper::UsesDrawIndex( broken, usesDrawIndex ) );
    }
}

TEST_CASE( "Shader thread preprocessing survives repeated initialization", "[shader][render_smoke]" )
{
    const NonInteractiveAssertions nonInteractive{};
    REQUIRE( nonInteractive._ready );
    platformInitRunListener::PlatformInit();
    const platformInitRunListener::LocalizationScope localization{};
    const ConsoleScope console{};
    REQUIRE( glswGetCurrentContext() == nullptr );
    SCOPE_EXIT
    {
        if ( glswGetCurrentContext() != nullptr )
        {
            glswShutdown();
        }
        ShaderProgramTestAccess::ClearAtoms();
    };

    ShaderProgramTestAccess::InitAtomLocations();
    Configuration config{};
    const string shaderPath = Paths::Shaders::GLSL::g_GLSLShaderLoc.string() + Paths::g_pathSeparator;
    for ( U8 iteration = 0u; iteration < 6u; ++iteration )
    {
        CAPTURE( iteration );
        REQUIRE( ShaderProgramTestAccess::InitThread( RenderAPI::Vulkan, config ) );
        REQUIRE( glswSetPath( shaderPath.c_str(), ".glsl" ) == 1 );
        ShaderProgram::LoadData data{};
        data._type = ShaderType::VERTEX;
        data._sourceFile = "baseVertexShaders.glsl";
        data._sourceName = "baseVertexShaders.Vertex.FullScreenQuad";
        data._shaderName = data._sourceName;
        eastl::set<U64> atoms;
        ShaderProgramTestAccess::Parse( RenderAPI::Vulkan, {}, data, atoms );
        REQUIRE_FALSE( data._sourceCodeGLSL.empty() );
        CHECK( data._sourceCodeGLSL.find( "gl_Position" ) != string::npos );
        REQUIRE( glswShutdown() == 1 );
    }
}

TEST_CASE( "Shader diagnostics accept full-length source text", "[shader][render_smoke]" )
{
    const NonInteractiveAssertions nonInteractive{};
    REQUIRE( nonInteractive._ready );
    platformInitRunListener::PlatformInit();
    const ShaderCompilerLifetime compilerLifetime{};
    const ConsoleScope console{};
    Console::ToggleFlag( Console::Flags::PRINT_TO_STD_CONSOLE, true );

    const std::string invalidShader = "#version 460\n/* 100% diagnostic " + std::string( 5000u, 'x' ) +
                                      "*/\nvoid main() { invalid_shader_token; }\n";
    bool capturedSource = false;
    size_t callbackID = Console::BindConsoleOutput( [&]( const Console::OutputEntry& entry )
    {
        capturedSource |= std::string_view{ entry._text }.find( invalidShader ) != std::string_view::npos;
    } );
    SCOPE_EXIT
    {
        [[maybe_unused]] const bool unbound = Console::UnbindConsoleOutput( callbackID );
        assert( unbound );
    };

    std::vector<unsigned int> spirv;
    CHECK_FALSE( SpirvHelper::GLSLtoSPV( ShaderType::FRAGMENT, invalidShader.c_str(), spirv, RenderAPI::Vulkan ) );
    CHECK( capturedSource );
}

TEST_CASE( "Terrain shader includes and variants use the engine preprocessing pipeline", "[shader][terrain][render_smoke]" )
{
    const NonInteractiveAssertions nonInteractive{};
    REQUIRE( nonInteractive._ready );
    platformInitRunListener::PlatformInit();
    const platformInitRunListener::LocalizationScope localization{};
    const ShaderCompilerLifetime compilerLifetime{};
    const ConsoleScope console{};
    REQUIRE( glswGetCurrentContext() == nullptr );
    string diagnostics;
    size_t callbackID = Console::BindConsoleOutput( [&]( const Console::OutputEntry& entry )
    {
        if ( entry._type == Console::EntryType::ERR )
        {
            diagnostics.append( entry._text );
        }
    } );
    SCOPE_EXIT
    {
        [[maybe_unused]] const bool unbound = Console::UnbindConsoleOutput( callbackID );
        assert( unbound );
    };

    const auto oldBindings = ShaderProgramTestAccess::Bindings();
    SCOPE_EXIT
    {
        if ( glswGetCurrentContext() != nullptr )
        {
            glswShutdown();
        }
        ShaderProgramTestAccess::ClearAtoms();
        ShaderProgramTestAccess::Bindings() = oldBindings;
    };

    ShaderProgramTestAccess::InitAtomLocations();
    for ( U8 set = 0u; set < to_base( DescriptorSetUsage::COUNT ); ++set )
    {
        for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
        {
            ShaderProgramTestAccess::Bindings()[set][slot]._glBinding = to_U8( set * MAX_BINDINGS_PER_DESCRIPTOR_SET + slot );
        }
    }

    const std::array validIncludes{
        "#include \"terrainUtils.cmn\"   \t",
        "  # include \"terrainUtils.cmn\" // terrain settings",
        "#include <terrainUtils.cmn> /* terrain settings */   "
    };
    for ( const char* include : validIncludes )
    {
        INFO( include );
        string source = include;
        eastl::set<U64> atoms;
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.find( "#define USE_CUSTOM_TEXTURE_OMR" ) != string::npos );
        CHECK( atoms.contains( _ID( "terrainUtils.cmn" ) ) );
        CHECK( atoms.contains( _ID( "nodeBufferedInput.cmn" ) ) );
    }

    const std::array invalidIncludes{
        "#include \"terrainUtils.cmn\" extra",
        "#include \"terrainUtils.cmn\" /* comment */ extra */",
        "#include \"terrainUtils.cmn>",
        "#include \" \"",
        "#include \"terrainUtils.bad\"",
        "#include \"missing-shader-test-atom.cmn\""
    };
    for ( const char* include : invalidIncludes )
    {
        INFO( include );
        string source = include;
        eastl::set<U64> atoms;
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.empty() );
    }

    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() /
                                              ( "divide-shader-includes-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( tempDir ) );
        const std::filesystem::path parentFile = tempDir / "parent.cmn";
        const std::filesystem::path childFile = tempDir / "child.cmn";
        const std::filesystem::path wrapperFile = tempDir / "wrapper.cmn";
        std::vector<std::filesystem::path> deepFiles;
        const ResourcePath oldLocation = ShaderProgramTestAccess::CommonAtomLocation();
        SCOPE_EXIT
        {
            ShaderProgramTestAccess::CommonAtomLocation() = oldLocation;
            ShaderProgramTestAccess::ClearAtoms();
            std::error_code error;
            for ( const auto& path : deepFiles )
            {
                std::filesystem::remove( path, error );
                assert( !error );
            }
            std::filesystem::remove( wrapperFile, error );
            assert( !error );
            std::filesystem::remove( childFile, error );
            assert( !error );
            std::filesystem::remove( parentFile, error );
            assert( !error );
            std::filesystem::remove( tempDir, error );
            assert( !error );
        };
        ShaderProgramTestAccess::CommonAtomLocation() = ResourcePath{ tempDir.string() };

        const auto writeAtom = []( const std::filesystem::path& path, const char* text )
        {
            std::ofstream file{ path };
            file << text;
            file.close();
            return file.good();
        };
        REQUIRE( writeAtom( parentFile, "#include \"child.cmn\"\n" ) );

        string source = "#include \"parent.cmn\"";
        eastl::set<U64> atoms;
        diagnostics.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.empty() );
        CHECK( diagnostics.find( "cannot open include file" ) != string::npos );

        REQUIRE( writeAtom( childFile, "const float nestedAtomFixture = 1.f;\n" ) );
        source = "#include \"parent.cmn\"";
        atoms.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.find( "nestedAtomFixture" ) != string::npos );
        CHECK( atoms.contains( _ID( "parent.cmn" ) ) );
        CHECK( atoms.contains( _ID( "child.cmn" ) ) );

        REQUIRE( writeAtom( wrapperFile, "#include \"parent.cmn\"\n" ) );
        source = "#include \"wrapper.cmn\"";
        atoms.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.find( "nestedAtomFixture" ) != string::npos );
        CHECK( atoms.contains( _ID( "wrapper.cmn" ) ) );
        CHECK( atoms.contains( _ID( "parent.cmn" ) ) );
        CHECK( atoms.contains( _ID( "child.cmn" ) ) );

        REQUIRE( writeAtom( childFile, "const float updatedNestedAtomFixture = 2.f;\n" ) );
        ShaderProgramTestAccess::EraseAtom( _ID( "child.cmn" ) );
        source = "#include \"wrapper.cmn\"";
        atoms.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.find( "updatedNestedAtomFixture" ) != string::npos );
        CHECK( atoms.contains( _ID( "wrapper.cmn" ) ) );
        CHECK( atoms.contains( _ID( "parent.cmn" ) ) );
        CHECK( atoms.contains( _ID( "child.cmn" ) ) );

        ShaderProgramTestAccess::ClearAtoms();
        REQUIRE( writeAtom( childFile, "#include \"invalid.cmn\" extra\n" ) );
        source = "#include \"parent.cmn\"";
        diagnostics.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.empty() );
        CHECK( diagnostics.find( "Invalid shader include" ) != string::npos );

        ShaderProgramTestAccess::ClearAtoms();
        REQUIRE( writeAtom( childFile, "#include \"parent.cmn\"\n" ) );
        source = "#include \"parent.cmn\"";
        diagnostics.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.empty() );
        CHECK( diagnostics.find( "Cyclic shader include" ) != string::npos );

        ShaderProgramTestAccess::ClearAtoms();
        for ( size_t i = 0u; i < 65u; ++i )
        {
            deepFiles.emplace_back( tempDir / ( "deep" + std::to_string( i ) + ".cmn" ) );
        }
        for ( size_t i = 0u; i < 63u; ++i )
        {
            const std::string next = "#include \"deep" + std::to_string( i + 1u ) + ".cmn\"\n";
            REQUIRE( writeAtom( deepFiles[i], next.c_str() ) );
        }
        REQUIRE( writeAtom( deepFiles[63], "const float deepestAtomFixture = 3.f;\n" ) );
        REQUIRE( writeAtom( deepFiles[64], "const float tooDeepAtomFixture = 4.f;\n" ) );

        source = "#include \"deep0.cmn\"";
        atoms.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.find( "deepestAtomFixture" ) != string::npos );
        CHECK( atoms.size() == 64u );

        source = "#include \"deep0.cmn\"";
        atoms.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.find( "deepestAtomFixture" ) != string::npos );
        CHECK( atoms.size() == 64u );

        REQUIRE( writeAtom( wrapperFile, "#include \"deep0.cmn\"\n" ) );
        source = "#include \"wrapper.cmn\"";
        diagnostics.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.empty() );
        CHECK( diagnostics.find( "Header inclusion depth limit reached" ) != string::npos );

        ShaderProgramTestAccess::ClearAtoms();
        REQUIRE( writeAtom( deepFiles[63], "#include \"deep64.cmn\"\n" ) );
        source = "#include \"deep0.cmn\"";
        diagnostics.clear();
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK( source.empty() );
        CHECK( diagnostics.find( "Header inclusion depth limit reached" ) != string::npos );
    }

    const std::filesystem::path shaderDirectory{ Paths::Shaders::GLSL::g_GLSLShaderLoc.string().c_str() };
    const std::array<std::string_view, 10u> shaderExtensions{{
        ".glsl", ".cmn", ".frag", ".vert", ".geom", ".tesc", ".tese", ".comp", ".mesh", ".task"
    }};
    size_t shaderSourceCount = 0u;
    for ( const auto& entry : std::filesystem::recursive_directory_iterator{ shaderDirectory } )
    {
        if ( !entry.is_regular_file() )
        {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if ( std::find( shaderExtensions.begin(), shaderExtensions.end(), extension ) == shaderExtensions.end() )
        {
            continue;
        }

        INFO( entry.path().string() );
        std::ifstream input{ entry.path(), std::ios::binary };
        REQUIRE( input.good() );
        const std::string contents{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        string source{ contents.c_str(), contents.size() };
        eastl::set<U64> atoms;
        ShaderProgramTestAccess::ExpandIncludes( source, atoms );
        CHECK_FALSE( source.empty() );
        ++shaderSourceCount;
    }
    CHECK( shaderSourceCount > 0u );

    ModuleDefines commonDefines;
    for ( const char* define : {
              "ENABLE_TBN", "TEXTURE_TILE_SIZE 512", "TERRAIN_HEIGHT_OFFSET -39.02",
              "WORLD_SCALE_X 128", "WORLD_SCALE_Y 114.15", "WORLD_SCALE_Z 128",
              "INV_CONTROL_VTX_PER_TILE_EDGE 0.11111111", "CONTROL_VTX_PER_TILE_EDGE 9",
              "PATCHES_PER_TILE_EDGE 8", "MAX_TEXTURE_LAYERS 2", "STATIC_TERRAIN",
              "REDUCE_TEXTURE_TILE_ARTIFACT", "UNDERWATER_TILE_SCALE 100", "SHADING_MODE_PBR_MR",
              "GEOMETRY_PATCH", "HAS_POSITION_ATTRIBUTE", "HAS_COLOR_ATTRIBUTE", "NODE_STATIC",
              "OVERRIDE_DATA_IDX", "NEED_TEXTURE_DATA_ALL_STAGES", "USE_UNIT0_TEXTURE",
              "USE_OPACITY_TEXTURE", "USE_NORMALMAP_TEXTURE", "USE_HEIGHTMAP_TEXTURE",
              "USE_SPECULAR_TEXTURE", "USE_UNIT1_TEXTURE" } )
    {
        commonDefines.emplace_back( define );
    }
    ModuleDefine tileFactors{};
    tileFactors._addPrefix = false;
    tileFactors._define = "const vec2 CURRENT_TILE_FACTORS[8] = { vec2(1), vec2(1), vec2(1), vec2(1), "
                          "vec2(1), vec2(1), vec2(1), vec2(1) };";
    commonDefines.push_back( tileFactors );

    Configuration config{};
    config.rendering.numLightsPerCluster = 100;
    bool firstAPI = true;
    for ( const RenderAPI api : { RenderAPI::Vulkan, RenderAPI::OpenGL } )
    {
        if ( firstAPI )
        {
            REQUIRE( ShaderProgramTestAccess::InitThread( api, config ) );
            firstAPI = false;
        }
        else
        {
            REQUIRE( glswShutdown() == 1 );
            REQUIRE( ShaderProgramTestAccess::InitGLSW( api, config ) );
        }
        const string shaderPath = Paths::Shaders::GLSL::g_GLSLShaderLoc.string() + Paths::g_pathSeparator;
        REQUIRE( glswSetPath( shaderPath.c_str(), ".glsl" ) == 1 );

        for ( const U8 variant : { U8{ 0u }, U8{ 1u }, U8{ 2u } } )
        {
            for ( const bool msaa : { false, true } )
            {
                CAPTURE( api, variant, msaa );
                ModuleDefines defines = commonDefines;
                if ( variant == 0u )
                {
                    defines.emplace_back( "PRE_PASS" );
                }
                if ( variant != 2u )
                {
                    defines.emplace_back( "MAIN_DISPLAY_PASS" );
                }
                else
                {
                    defines.emplace_back( "LOW_QUALITY" );
                }
                if ( msaa )
                {
                    defines.emplace_back( "MSAA_SCREEN_TARGET" );
                }

                ShaderProgram::LoadData data{};
                data._type = ShaderType::FRAGMENT;
                data._sourceFile = "terrainTess.glsl";
                data._sourceName = variant == 0u ? "terrainTess.Fragment.PrePass" :
                                   variant == 1u ? "terrainTess.Fragment" : "terrainTess.Fragment.LQPass";
                data._shaderName = data._sourceName;
                eastl::set<U64> atoms;
                ShaderProgramTestAccess::Parse( api, defines, data, atoms );
                REQUIRE_FALSE( data._sourceCodeGLSL.empty() );
                CHECK( atoms.contains( _ID( "terrainUtils.cmn" ) ) );
                CHECK( atoms.contains( _ID( "terrainSplatting.frag" ) ) );
                CHECK( data._sourceCodeGLSL.find( "GetBlend(" ) == string::npos );
                CHECK( data._sourceCodeGLSL.find( "GetCaustics(" ) == string::npos );
                if ( variant == 1u )
                {
                    CHECK( atoms.contains( _ID( "shadowStrength.cmn" ) ) );
                    CHECK( atoms.contains( _ID( "spotShadowProjection.cmn" ) ) );
                    CHECK( data._sourceCodeGLSL.find( "singleDepthMaps" ) != string::npos );
                }
                if ( variant != 0u )
                {
                    CHECK( data._sourceCodeGLSL.find( "getIBLAmbient" ) != string::npos );
                    CHECK( data._sourceCodeGLSL.find( "getReflectionLookup" ) == string::npos );
                    const size_t main = data._sourceCodeGLSL.rfind( "void main" );
                    REQUIRE( main != string::npos );
                    CHECK( data._sourceCodeGLSL.find( "dFdx(iblNormalW)", main ) == string::npos );
                    CHECK( data._sourceCodeGLSL.find( "dFdy(iblNormalW)", main ) == string::npos );
                }

                std::vector<unsigned int> spirv;
                diagnostics.clear();
                const bool compiled = SpirvHelper::GLSLtoSPV( ShaderType::FRAGMENT, data._sourceCodeGLSL.c_str(), spirv, api );
                INFO( diagnostics.c_str() );
                REQUIRE( compiled );
                CHECK_FALSE( spirv.empty() );
            }
        }

        constexpr std::array<std::pair<ShaderType, const char*>, 3u> otherStages{{
            { ShaderType::VERTEX, "terrainTess.Vertex" },
            { ShaderType::TESSELLATION_CTRL, "terrainTess.TessellationC" },
            { ShaderType::TESSELLATION_EVAL, "terrainTess.TessellationE" }
        }};
        for ( const U8 variant : { U8{ 0u }, U8{ 1u }, U8{ 2u }, U8{ 3u } } )
        {
            ModuleDefines defines = commonDefines;
            if ( variant == 0u )
            {
                defines.emplace_back( "PRE_PASS" );
            }
            if ( variant < 2u )
            {
                defines.emplace_back( "MAIN_DISPLAY_PASS" );
            }
            else
            {
                defines.emplace_back( "MAX_TESS_LEVEL 16" );
                if ( variant == 2u )
                {
                    defines.emplace_back( "LOW_QUALITY" );
                }
            }

            for ( const auto& [stage, sourceName] : otherStages )
            {
                CAPTURE( api, variant, sourceName );
                ShaderProgram::LoadData data{};
                data._type = stage;
                data._sourceFile = "terrainTess.glsl";
                data._sourceName = sourceName;
                data._shaderName = data._sourceName;
                eastl::set<U64> atoms;
                ShaderProgramTestAccess::Parse( api, defines, data, atoms );
                REQUIRE_FALSE( data._sourceCodeGLSL.empty() );
                CHECK( atoms.contains( _ID( "terrainUtils.cmn" ) ) );
                CHECK( atoms.contains( _ID( "nodeBufferedInput.cmn" ) ) );

                std::vector<unsigned int> spirv;
                diagnostics.clear();
                const bool compiled = SpirvHelper::GLSLtoSPV( stage, data._sourceCodeGLSL.c_str(), spirv, api );
                INFO( diagnostics.c_str() );
                REQUIRE( compiled );
                CHECK_FALSE( spirv.empty() );
            }
        }

        struct LinkedStage
        {
            ShaderType _type;
            EShLanguage _language;
            const char* _source;
        };
        constexpr std::array<LinkedStage, 4u> linkedStages{{
            { ShaderType::VERTEX, EShLangVertex, "terrainTess.Vertex" },
            { ShaderType::TESSELLATION_CTRL, EShLangTessControl, "terrainTess.TessellationC" },
            { ShaderType::TESSELLATION_EVAL, EShLangTessEvaluation, "terrainTess.TessellationE" },
            { ShaderType::FRAGMENT, EShLangFragment, "terrainTess.Fragment" }
        }};
        for ( const bool prePass : { false, true } )
        {
            CAPTURE( api, prePass );
            ModuleDefines defines = commonDefines;
            defines.emplace_back( "MAIN_DISPLAY_PASS" );
            if ( prePass )
            {
                defines.emplace_back( "PRE_PASS" );
            }

            TBuiltInResource resources{};
            SpirvHelper::InitResources( resources );
            const EShMessages messages = static_cast<EShMessages>(
                EShMsgSpvRules | ( api == RenderAPI::Vulkan ? EShMsgVulkanRules : 0u ) );
            std::array<ShaderProgram::LoadData, linkedStages.size()> sourceData{};
            std::array<const char*, linkedStages.size()> sourcePointers{};
            std::array<std::unique_ptr<glslang::TShader>, linkedStages.size()> shaders{};
            glslang::TProgram program;
            eastl::set<U64> atoms;
            ShaderProgram::UniformBlockLayouts uniformBlocks{};
            for ( size_t i = 0u; i < linkedStages.size(); ++i )
            {
                const LinkedStage& stage = linkedStages[i];
                ShaderProgram::LoadData& data = sourceData[i];
                data._type = stage._type;
                data._sourceFile = "terrainTess.glsl";
                data._sourceName = prePass && stage._type == ShaderType::FRAGMENT
                    ? "terrainTess.Fragment.PrePass" : stage._source;
                data._shaderName = data._sourceName;
                ShaderProgramTestAccess::Parse( api, defines, data, atoms, uniformBlocks );
                REQUIRE_FALSE( data._sourceCodeGLSL.empty() );

                shaders[i] = std::make_unique<glslang::TShader>( stage._language );
                glslang::TShader& shader = *shaders[i];
                sourcePointers[i] = data._sourceCodeGLSL.c_str();
                shader.setStrings( &sourcePointers[i], 1 );
                if ( api == RenderAPI::Vulkan )
                {
                    shader.setEnvInput( glslang::EShSourceGlsl, stage._language, glslang::EShClientVulkan, 100 );
                    shader.setEnvClient( glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4 );
                    shader.setEnvTarget( glslang::EShTargetSpv, glslang::EShTargetSpv_1_6 );
                }
                else
                {
                    shader.setEnvInput( glslang::EShSourceGlsl, stage._language, glslang::EShClientOpenGL, 100 );
                    shader.setEnvClient( glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450 );
                    shader.setEnvTarget( glslang::EShTargetSpv, glslang::EShTargetSpv_1_0 );
                }

                const bool parsed = shader.parse( &resources, 100, false, messages );
                INFO( shader.getInfoLog() );
                REQUIRE( parsed );
                program.addShader( &shader );
            }
            const bool linked = program.link( messages );
            INFO( program.getInfoLog() );
            REQUIRE( linked );
        }
    }
}

    TEST_CASE( "Half-resolution SSAO resampling shaders compile for both rendering backends", "[shader][ssao][render_smoke]" )
    {
        const NonInteractiveAssertions nonInteractive{};
        REQUIRE( nonInteractive._ready );
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        const ShaderCompilerLifetime compilerLifetime{};
        const ConsoleScope console{};
        REQUIRE( glswGetCurrentContext() == nullptr );

        string diagnostics;
        size_t callbackID = Console::BindConsoleOutput( [&]( const Console::OutputEntry& entry )
        {
            if ( entry._type == Console::EntryType::ERR )
            {
                diagnostics.append( entry._text );
            }
        } );
        SCOPE_EXIT
        {
            [[maybe_unused]] const bool unbound = Console::UnbindConsoleOutput( callbackID );
            assert( unbound );
        };

        const auto oldBindings = ShaderProgramTestAccess::Bindings();
        SCOPE_EXIT
        {
            if ( glswGetCurrentContext() != nullptr )
            {
                glswShutdown();
            }
            ShaderProgramTestAccess::ClearAtoms();
            ShaderProgramTestAccess::Bindings() = oldBindings;
        };

        ShaderProgramTestAccess::InitAtomLocations();
        for ( U8 set = 0u; set < to_base( DescriptorSetUsage::COUNT ); ++set )
        {
            for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
            {
                ShaderProgramTestAccess::Bindings()[set][slot]._glBinding = to_U8( set * MAX_BINDINGS_PER_DESCRIPTOR_SET + slot );
            }
        }

        struct Variant
        {
            const char* _name;
            U8 _firstSlot;
            U8 _slotCount;
            bool _halfGeneration = false;
        };
        constexpr std::array variants{
            Variant{ "SSAOPass.Fragment.SSAODownsample", 1u, 2u },
            Variant{ "SSAOPass.Fragment.SSAOUpsample", 0u, 4u },
            Variant{ "SSAOPass.Fragment.SSAOCalc", 0u, 3u, false },
            Variant{ "SSAOPass.Fragment.SSAOCalc", 0u, 3u, true }
        };

        Configuration config{};
        bool firstAPI = true;
        for ( const RenderAPI api : { RenderAPI::Vulkan, RenderAPI::OpenGL } )
        {
            if ( firstAPI )
            {
                REQUIRE( ShaderProgramTestAccess::InitThread( api, config ) );
                firstAPI = false;
            }
            else
            {
                REQUIRE( glswShutdown() == 1 );
                REQUIRE( ShaderProgramTestAccess::InitGLSW( api, config ) );
            }
            const string shaderPath = Paths::Shaders::GLSL::g_GLSLShaderLoc.string() + Paths::g_pathSeparator;
            REQUIRE( glswSetPath( shaderPath.c_str(), ".glsl" ) == 1 );

            for ( const Variant& variant : variants )
            {
                CAPTURE( api, variant._name );
                ShaderProgram::LoadData data{};
                data._type = ShaderType::FRAGMENT;
                data._sourceFile = "SSAOPass.glsl";
                data._sourceName = variant._name;
                data._shaderName = data._sourceName;
                eastl::set<U64> atoms{};
                diagnostics.clear();
                ModuleDefines defines{ { "SSAO_SAMPLE_COUNT 8" } };
                if ( variant._halfGeneration )
                {
                    defines.emplace_back( "COMPUTE_HALF_RES" );
                }
                ShaderProgramTestAccess::Parse( api, defines, data, atoms );
                INFO( diagnostics.c_str() );
                REQUIRE_FALSE( data._sourceCodeGLSL.empty() );
                CHECK( atoms.contains( _ID( "ssaoResampling.cmn" ) ) );

                std::vector<unsigned int> spirv;
                diagnostics.clear();
                const bool compiled = SpirvHelper::GLSLtoSPV( ShaderType::FRAGMENT, data._sourceCodeGLSL.c_str(), spirv, api );
                INFO( diagnostics.c_str() );
                REQUIRE( compiled );

                if ( api == RenderAPI::Vulkan )
                {
                    Reflection::Data reflection{};
                    REQUIRE( SpirvHelper::BuildReflectionData( ShaderType::FRAGMENT, spirv, api, reflection ) );
                    CHECK( reflection._fragmentOutputs[0u] );
                    REQUIRE( reflection._images.size() == variant._slotCount );
                    for ( U8 slot = variant._firstSlot; slot < variant._firstSlot + variant._slotCount; ++slot )
                    {
                        CHECK( std::ranges::any_of( reflection._images, [slot]( const Reflection::ImageEntry& image )
                        {
                            return image._bindingSet == to_base( DescriptorSetUsage::PER_DRAW ) &&
                                   image._bindingSlot == slot && !image._isMultiSampled;
                        } ) );
                    }
                }
            }
        }
    }

    TEST_CASE( "Water shader variants compile for both rendering backends", "[shader][water][render_smoke]" )
    {
        const NonInteractiveAssertions nonInteractive{};
        REQUIRE( nonInteractive._ready );
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        const ShaderCompilerLifetime compilerLifetime{};
        const ConsoleScope console{};
        REQUIRE( glswGetCurrentContext() == nullptr );
        string diagnostics;
        size_t callbackID = Console::BindConsoleOutput( [&]( const Console::OutputEntry& entry )
        {
            if ( entry._type == Console::EntryType::ERR )
            {
                diagnostics.append( entry._text );
            }
        } );
        SCOPE_EXIT
        {
            [[maybe_unused]] const bool unbound = Console::UnbindConsoleOutput( callbackID );
            assert( unbound );
        };

        const auto oldBindings = ShaderProgramTestAccess::Bindings();
        SCOPE_EXIT
        {
            if ( glswGetCurrentContext() != nullptr )
            {
                glswShutdown();
            }
            ShaderProgramTestAccess::ClearAtoms();
            ShaderProgramTestAccess::Bindings() = oldBindings;
        };

        ShaderProgramTestAccess::InitAtomLocations();
        for ( U8 set = 0u; set < to_base( DescriptorSetUsage::COUNT ); ++set )
        {
            for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
            {
                ShaderProgramTestAccess::Bindings()[set][slot]._glBinding = to_U8( set * MAX_BINDINGS_PER_DESCRIPTOR_SET + slot );
            }
        }

        ModuleDefines commonDefines;
        for ( const char* define : {
                  "ENABLE_TBN", "SHADING_MODE_BLINN_PHONG", "GEOMETRY_TRIANGLES",
                  "HAS_POSITION_ATTRIBUTE", "HAS_NORMAL_ATTRIBUTE", "HAS_TEXCOORD_ATTRIBUTE",
                  "USE_NORMALMAP_TEXTURE", "_refractionTint PushData0[0].xyz",
                  "_specularShininess PushData0[0].w", "_waterDistanceFogColour PushData0[1].xyz",
                  "_noiseTile PushData0[2].xy", "_noiseFactor PushData0[2].zw",
                  "_fogStartEndDistances PushData0[3].xy" } )
        {
            commonDefines.emplace_back( define );
        }

        struct Variant
        {
            bool _display;
            bool _prePass;
            bool _reflection;
            bool _refraction;
            bool _oit;
            bool _msaa;
            bool _disableShadows = false;
        };
        constexpr std::array variants{
            Variant{ true, false, true, true, false, false },
            Variant{ true, false, true, true, true, true },
            Variant{ true, false, true, false, false, false },
            Variant{ true, false, false, true, false, false },
            Variant{ true, false, false, false, false, false },
            Variant{ true, true, true, true, false, false },
            Variant{ false, false, true, true, false, false },
            Variant{ true, false, true, false, false, false, true },
            Variant{ true, false, false, false, false, false, true }
        };

        Configuration config{};
        bool firstAPI = true;
        for ( const RenderAPI api : { RenderAPI::Vulkan, RenderAPI::OpenGL } )
        {
            if ( firstAPI )
            {
                REQUIRE( ShaderProgramTestAccess::InitThread( api, config ) );
                firstAPI = false;
            }
            else
            {
                REQUIRE( glswShutdown() == 1 );
                REQUIRE( ShaderProgramTestAccess::InitGLSW( api, config ) );
            }
            const string shaderPath = Paths::Shaders::GLSL::g_GLSLShaderLoc.string() + Paths::g_pathSeparator;
            REQUIRE( glswSetPath( shaderPath.c_str(), ".glsl" ) == 1 );

            for ( const Variant& variant : variants )
            {
                CAPTURE( api, variant._display, variant._prePass, variant._reflection,
                         variant._refraction, variant._oit, variant._msaa, variant._disableShadows );
                ModuleDefines defines = commonDefines;
                if ( variant._display ) defines.emplace_back( "MAIN_DISPLAY_PASS" );
                if ( variant._prePass )
                {
                    defines.emplace_back( "PRE_PASS" );
                    defines.emplace_back( "SKIP_REFLECT_REFRACT" );
                }
                if ( variant._reflection ) defines.emplace_back( "USE_PLANAR_REFLECTOR" );
                if ( variant._refraction ) defines.emplace_back( "USE_PLANAR_REFRACTOR" );
                if ( variant._oit ) defines.emplace_back( "OIT_PASS" );
                if ( variant._msaa ) defines.emplace_back( "MSAA_SCREEN_TARGET" );
                if ( variant._disableShadows ) defines.emplace_back( "DISABLE_SHADOW_MAPPING" );

                ShaderProgram::LoadData data{};
                data._type = ShaderType::FRAGMENT;
                data._sourceFile = "water.glsl";
                data._sourceName = "water.Fragment";
                data._shaderName = data._sourceName;
                eastl::set<U64> atoms;
                ShaderProgramTestAccess::Parse( api, defines, data, atoms );
                REQUIRE_FALSE( data._sourceCodeGLSL.empty() );
                CHECK( atoms.contains( _ID( "waterComposite.cmn" ) ) );
                if ( !variant._prePass && (variant._reflection || variant._refraction) )
                {
                    const size_t main = data._sourceCodeGLSL.rfind( "void main" );
                    REQUIRE( main != string::npos );
                    if ( variant._reflection )
                    {
                        const size_t dx = data._sourceCodeGLSL.find( "dFdx(distortedUV)", main );
                        const size_t dy = data._sourceCodeGLSL.find( "dFdy(distortedUV)", main );
                        const size_t reflection = data._sourceCodeGLSL.find( "texReflect, distortedUV", main );
                        REQUIRE( dx != string::npos );
                        REQUIRE( dy != string::npos );
                        REQUIRE( reflection != string::npos );
                        CHECK( dx < reflection );
                        CHECK( dy < reflection );
                        CHECK( data._sourceCodeGLSL.find( "texReflect, waterUV", main ) == string::npos );
                    }
                    if ( variant._refraction )
                    {
                        CHECK( data._sourceCodeGLSL.find( "texRefract, distortedUV", main ) != string::npos );
                    }
                }

                std::vector<unsigned int> spirv;
                diagnostics.clear();
                const bool compiled = SpirvHelper::GLSLtoSPV( ShaderType::FRAGMENT, data._sourceCodeGLSL.c_str(), spirv, api );
                INFO( diagnostics.c_str() );
                REQUIRE( compiled );
                CHECK_FALSE( spirv.empty() );
            }
        }
    }

    TEST_CASE( "Hi-Z indirect culling shader compiles for both rendering backends", "[shader][occlusion][render_smoke]" )
    {
        const NonInteractiveAssertions nonInteractive{};
        REQUIRE( nonInteractive._ready );
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        const ShaderCompilerLifetime compilerLifetime{};
        const ConsoleScope console{};
        REQUIRE( glswGetCurrentContext() == nullptr );

        string diagnostics;
        size_t callbackID = Console::BindConsoleOutput( [&]( const Console::OutputEntry& entry )
        {
            if ( entry._type == Console::EntryType::ERR )
            {
                diagnostics.append( entry._text );
            }
        } );
        SCOPE_EXIT
        {
            [[maybe_unused]] const bool unbound = Console::UnbindConsoleOutput( callbackID );
            assert( unbound );
        };

        const auto oldBindings = ShaderProgramTestAccess::Bindings();
        SCOPE_EXIT
        {
            if ( glswGetCurrentContext() != nullptr )
            {
                glswShutdown();
            }
            ShaderProgramTestAccess::ClearAtoms();
            ShaderProgramTestAccess::Bindings() = oldBindings;
        };

        ShaderProgramTestAccess::InitAtomLocations();
        for ( U8 set = 0u; set < to_base( DescriptorSetUsage::COUNT ); ++set )
        {
            for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
            {
                auto& binding = ShaderProgramTestAccess::Bindings()[set][slot];
                binding._glBinding = to_U8( set * MAX_BINDINGS_PER_DESCRIPTOR_SET + slot );
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                if ( set == to_base( DescriptorSetUsage::PER_BATCH ) )
                {
                    binding._type = slot == 0u ? DescriptorSetBindingType::INDIRECT_BUFFER :
                                    slot == 1u ? DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC :
                                                 DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC;
                }
                else if ( set == to_base( DescriptorSetUsage::PER_PASS ) && slot >= 5u )
                {
                    binding._type = slot == 8u ? DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC :
                                                 DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC;
                }
                else if ( set == to_base( DescriptorSetUsage::PER_FRAME ) && slot >= 7u )
                {
                    binding._type = slot < 9u ? DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC :
                                                DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC;
                }
            }
        }

        Configuration config{};
        bool firstAPI = true;
        for ( const RenderAPI api : { RenderAPI::Vulkan, RenderAPI::OpenGL } )
        {
            CAPTURE( api );
            if ( firstAPI )
            {
                REQUIRE( ShaderProgramTestAccess::InitThread( api, config ) );
                firstAPI = false;
            }
            else
            {
                REQUIRE( glswShutdown() == 1 );
                REQUIRE( ShaderProgramTestAccess::InitGLSW( api, config ) );
            }
            const string shaderPath = Paths::Shaders::GLSL::g_GLSLShaderLoc.string() + Paths::g_pathSeparator;
            REQUIRE( glswSetPath( shaderPath.c_str(), ".glsl" ) == 1 );

            ModuleDefines defines;
            defines.emplace_back( Util::StringFormat( "WORK_GROUP_SIZE {}", OCCLUSION_CULL_GROUP_SIZE ) );
            ShaderProgram::LoadData data{};
            data._type = ShaderType::COMPUTE;
            data._sourceFile = "HiZOcclusionCull.glsl";
            data._sourceName = "HiZOcclusionCull.Compute";
            data._shaderName = data._sourceName;
            eastl::set<U64> atoms{};
            diagnostics.clear();
            ShaderProgramTestAccess::Parse( api, defines, data, atoms );
            INFO( diagnostics.c_str() );
            REQUIRE_FALSE( data._sourceCodeGLSL.empty() );
            CHECK( atoms.contains( _ID( "hizDispatchIndex.cmn" ) ) );
            const size_t guard = data._sourceCodeGLSL.find( "if (groupIndex >=" );
            const size_t index = data._sourceCodeGLSL.find( "const uint ident", guard );
            REQUIRE( guard != string::npos );
            CHECK( index != string::npos );

            std::vector<unsigned int> spirv;
            diagnostics.clear();
            const bool compiled = SpirvHelper::GLSLtoSPV( ShaderType::COMPUTE, data._sourceCodeGLSL.c_str(), spirv, api );
            INFO( diagnostics.c_str() );
            REQUIRE( compiled );
            CHECK_FALSE( spirv.empty() );
        }
    }

    TEST_CASE( "OIT composition shader variants match their output and sampler contracts", "[shader][render_smoke][oit]" )
    {
        const NonInteractiveAssertions nonInteractive{};
        REQUIRE( nonInteractive._ready );
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        const ShaderCompilerLifetime compilerLifetime{};
        const ConsoleScope console{};
        REQUIRE( glswGetCurrentContext() == nullptr );

        string diagnostics;
        size_t callbackID = Console::BindConsoleOutput( [&]( const Console::OutputEntry& entry )
        {
            if ( entry._type == Console::EntryType::ERR )
            {
                diagnostics.append( entry._text );
            }
        } );
        SCOPE_EXIT
        {
            [[maybe_unused]] const bool unbound = Console::UnbindConsoleOutput( callbackID );
            assert( unbound );
        };

        const auto oldBindings = ShaderProgramTestAccess::Bindings();
        SCOPE_EXIT
        {
            if ( glswGetCurrentContext() != nullptr )
            {
                glswShutdown();
            }
            ShaderProgramTestAccess::ClearAtoms();
            ShaderProgramTestAccess::Bindings() = oldBindings;
        };

        ShaderProgramTestAccess::InitAtomLocations();
        for ( U8 set = 0u; set < to_base( DescriptorSetUsage::COUNT ); ++set )
        {
            for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
            {
                ShaderProgramTestAccess::Bindings()[set][slot]._glBinding = to_U8( set * MAX_BINDINGS_PER_DESCRIPTOR_SET + slot );
            }
        }

        Configuration config{};
        bool firstAPI = true;
        for ( const RenderAPI api : { RenderAPI::Vulkan, RenderAPI::OpenGL } )
        {
            if ( firstAPI )
            {
                REQUIRE( ShaderProgramTestAccess::InitThread( api, config ) );
                firstAPI = false;
            }
            else
            {
                REQUIRE( glswShutdown() == 1 );
                REQUIRE( ShaderProgramTestAccess::InitGLSW( api, config ) );
            }
            const string shaderPath = Paths::Shaders::GLSL::g_GLSLShaderLoc.string() + Paths::g_pathSeparator;
            REQUIRE( glswSetPath( shaderPath.c_str(), ".glsl" ) == 1 );

            for ( const bool writeNormals : { false, true } )
            {
                for ( const bool useMSAA : { false, true } )
                {
                    CAPTURE( api, writeNormals, useMSAA );
                    ModuleDefines defines;
                    if ( !writeNormals ) defines.emplace_back( "OIT_COLOUR_ONLY" );
                    if ( useMSAA ) defines.emplace_back( "USE_MSAA_TARGET" );

                    ShaderProgram::LoadData data{};
                    data._type = ShaderType::FRAGMENT;
                    data._sourceFile = "OITComposition.glsl";
                    data._sourceName = "OITComposition.Fragment";
                    data._shaderName = data._sourceName;
                    eastl::set<U64> atoms{};
                    diagnostics.clear();
                    ShaderProgramTestAccess::Parse( api, defines, data, atoms );
                    INFO( diagnostics.c_str() );
                    REQUIRE_FALSE( data._sourceCodeGLSL.empty() );
                    CHECK( atoms.contains( _ID( "utility.frag" ) ) );

                    std::vector<unsigned int> spirv;
                    diagnostics.clear();
                    const bool compiled = SpirvHelper::GLSLtoSPV( ShaderType::FRAGMENT, data._sourceCodeGLSL.c_str(), spirv, api );
                    INFO( diagnostics.c_str() );
                    REQUIRE( compiled );

                    if ( api == RenderAPI::Vulkan )
                    {
                        Reflection::Data reflection{};
                        REQUIRE( SpirvHelper::BuildReflectionData( ShaderType::FRAGMENT, spirv, api, reflection ) );
                        CHECK( reflection._fragmentOutputs[to_base( GFXDevice::ScreenTargets::ALBEDO )] );
                        CHECK( reflection._fragmentOutputs[to_base( GFXDevice::ScreenTargets::NORMALS )] == writeNormals );
                        CHECK( reflection._images.size() == ( writeNormals ? 3u : 2u ) );
                        for ( const auto& image : reflection._images )
                        {
                            CHECK( image._bindingSet == to_base( DescriptorSetUsage::PER_DRAW ) );
                            CHECK( image._isMultiSampled == useMSAA );
                        }
                        for ( U8 slot = 0u; slot < ( writeNormals ? 3u : 2u ); ++slot )
                        {
                            CHECK( std::ranges::any_of( reflection._images, [slot]( const Reflection::ImageEntry& image )
                            {
                                return image._bindingSlot == slot;
                            } ) );
                        }
                    }
                }
            }
        }
    }

    TEST_CASE( "Cached loose uniforms preserve stage layout assignments", "[shader][cache][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        const ConsoleScope console{};

        const auto makeUniform = []( const char* name )
        {
            Reflection::UniformDeclaration result{};
            result._name = name;
            result._type = "uint";
            result._typeHash = _ID( result._type.c_str() );
            return result;
        };
        Reflection::UniformsSet vertexUniforms{};
        vertexUniforms.emplace( makeUniform( "INDIRECT_DATA_IDX" ) );
        vertexUniforms.emplace( makeUniform( "dvd_terrainChunkOffset" ) );
        Reflection::UniformsSet fragmentUniforms{};
        fragmentUniforms.emplace( makeUniform( "INDIRECT_DATA_IDX" ) );
        Reflection::UniformsSet thirdUniforms{};
        thirdUniforms.emplace( makeUniform( "thirdUniform" ) );

        ShaderProgram::UniformBlockLayouts blocks{};
        CHECK( ShaderProgramTestAccess::FindUniformBlockIndex( blocks, vertexUniforms ) == 0u );
        blocks[0] = vertexUniforms;
        CHECK( ShaderProgramTestAccess::FindUniformBlockIndex( blocks, fragmentUniforms ) == 1u );
        blocks[1] = fragmentUniforms;
        CHECK( ShaderProgramTestAccess::FindUniformBlockIndex( blocks, vertexUniforms ) == 0u );
        CHECK( ShaderProgramTestAccess::FindUniformBlockIndex( blocks, thirdUniforms ) == Reflection::INVALID_BINDING_INDEX );

        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() /
                                              ( "divide-shader-uniforms-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( tempDir ) );
        const std::filesystem::path cacheFile = tempDir / "vertex.refl";
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove( cacheFile, error );
            assert( !error );
            std::filesystem::remove( tempDir, error );
            assert( !error );
        };

        ShaderProgram::LoadData original{};
        original._uniforms = vertexUniforms;
        original._reflectionData._uniformBlockBindingIndex = 14u;
        const eastl::set<U64> originalAtoms{ _ID( "vegetationData.cmn" ) };
        REQUIRE( Reflection::SaveReflectionData( ResourcePath{ tempDir.string() }, ResourcePath{ "vertex.refl" },
                                                 original._reflectionData, original._uniforms, originalAtoms ) );

        ShaderProgram::LoadData restored{};
        eastl::set<U64> restoredAtoms{};
        REQUIRE( Reflection::LoadReflectionData( ResourcePath{ tempDir.string() }, ResourcePath{ "vertex.refl" },
                                                 restored._reflectionData, restored._uniforms, restoredAtoms ) );
        CHECK( restored._uniforms == vertexUniforms );
        CHECK( restoredAtoms == originalAtoms );

        U8 blockIndex = Reflection::INVALID_BINDING_INDEX;
        blocks = {};
        REQUIRE( ShaderProgramTestAccess::CachedUniformBlockMatches( blocks, restored, blockIndex ) );
        REQUIRE( blockIndex == 0u );
        blocks[blockIndex] = restored._uniforms;
        CHECK( ShaderProgramTestAccess::FindUniformBlockIndex( blocks, fragmentUniforms ) == 1u );

        blocks = {};
        blocks[0] = fragmentUniforms;
        CHECK_FALSE( ShaderProgramTestAccess::CachedUniformBlockMatches( blocks, restored, blockIndex ) );
        CHECK( blockIndex == 1u );

        const ShaderProgram::LoadData noUniforms{};
        CHECK( ShaderProgramTestAccess::CachedUniformBlockMatches( blocks, noUniforms, blockIndex ) );
        CHECK( blockIndex == Reflection::INVALID_BINDING_INDEX );
    }

    TEST_CASE( "Push constant detection preserves declarations and identifier boundaries", "[shader][push_constants][render_smoke]" )
    {
        using State = Reflection::PushConstantsState;
        const auto detect = []( const char* source, const RenderAPI api )
        {
            return ShaderProgramTestAccess::DetectPushConstantsUsage( source, api );
        };

        CHECK( detect( "", RenderAPI::OpenGL ) == State::NOT_USED );
        CHECK( detect( "float unrelated = 0.0;\n/*Engine define: PushData0 PushData1 */\n", RenderAPI::OpenGL ) == State::NOT_USED );
        CHECK( detect( "layout(location = 18) uniform vec4 PushData0;\r\nlayout(binding=19) uniform vec4 PushData1;\n",
                       RenderAPI::OpenGL ) == State::NOT_USED );
        CHECK( detect( "vec4 PushData0;\r\nvec4 PushData1;\n", RenderAPI::Vulkan ) == State::NOT_USED );
        CHECK( detect( "vec4 PushData0;\n", RenderAPI::OpenGL ) == State::USED_SLOT_1 );
        CHECK( detect( "layout(location=18) uniform vec4 PushData0;\n", RenderAPI::Vulkan ) == State::USED_SLOT_1 );
        CHECK( detect( "PushData0[0].x;\n", RenderAPI::OpenGL ) == State::USED_SLOT_1 );
        CHECK( detect( "PushData1[0].x;\n", RenderAPI::Vulkan ) == State::USED_SLOT_2 );
        CHECK( detect( "PushData0[0] + PushData1[0];\n", RenderAPI::OpenGL ) == State::USED_BOTH );
        CHECK( detect( "notPushData0; PushData0suffix; _PushData1; PushData1_;\n", RenderAPI::OpenGL ) == State::NOT_USED );
        CHECK( detect( "/*Engine define: PushData0 */\n(PushData1);\n", RenderAPI::OpenGL ) == State::USED_SLOT_2 );
        CHECK( detect( "PushDa\rta0[0];\r\n", RenderAPI::Vulkan ) == State::USED_SLOT_1 );
    }

    TEST_CASE( "Cached SPIR-V rejects incomplete words", "[shader][cache][render_smoke]" )
    {
        const std::array<SpvWord, 2u> expected{ 0x07230203u, 0x00010600u };
        const std::string bytes{ reinterpret_cast<const char*>( expected.data() ), sizeof( expected ) };
        std::vector<SpvWord> output{};
        std::istringstream complete{ bytes };
        REQUIRE( ShaderProgramTestAccess::ReadCachedSPIRV( complete, output ) );
        CHECK( std::ranges::equal( output, expected ) );

        std::istringstream truncated{ bytes.substr( 0u, bytes.size() - 1u ) };
        CHECK_FALSE( ShaderProgramTestAccess::ReadCachedSPIRV( truncated, output ) );
        CHECK( output.empty() );
        std::istringstream empty{};
        CHECK_FALSE( ShaderProgramTestAccess::ReadCachedSPIRV( empty, output ) );
        CHECK( output.empty() );
    }

    TEST_CASE( "Vegetation culling and draw shaders compile for both rendering backends", "[shader][vegetation][render_smoke]" )
    {
        const NonInteractiveAssertions nonInteractive{};
        REQUIRE( nonInteractive._ready );
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        const ShaderCompilerLifetime compilerLifetime{};
        const ConsoleScope console{};
        REQUIRE( glswGetCurrentContext() == nullptr );

        string diagnostics;
        size_t callbackID = Console::BindConsoleOutput( [&]( const Console::OutputEntry& entry )
        {
            if ( entry._type == Console::EntryType::ERR )
            {
                diagnostics.append( entry._text );
            }
        } );
        SCOPE_EXIT
        {
            [[maybe_unused]] const bool unbound = Console::UnbindConsoleOutput( callbackID );
            assert( unbound );
        };

        const auto oldBindings = ShaderProgramTestAccess::Bindings();
        SCOPE_EXIT
        {
            if ( glswGetCurrentContext() != nullptr )
            {
                glswShutdown();
            }
            ShaderProgramTestAccess::ClearAtoms();
            ShaderProgramTestAccess::Bindings() = oldBindings;
        };

        ShaderProgramTestAccess::InitAtomLocations();
        for ( U8 set = 0u; set < to_base( DescriptorSetUsage::COUNT ); ++set )
        {
            for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
            {
                auto& binding = ShaderProgramTestAccess::Bindings()[set][slot];
                binding._glBinding = to_U8( set * MAX_BINDINGS_PER_DESCRIPTOR_SET + slot );
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                if ( set == to_base( DescriptorSetUsage::PER_DRAW ) && slot >= 12u )
                {
                    binding._type = slot == 12u ? DescriptorSetBindingType::IMAGE :
                                    slot == 13u ? DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC :
                                                  DescriptorSetBindingType::UNIFORM_BUFFER_STATIC;
                }
                else if ( set == to_base( DescriptorSetUsage::PER_BATCH ) )
                {
                    binding._type = slot == 0u ? DescriptorSetBindingType::INDIRECT_BUFFER :
                                    slot == 1u ? DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC :
                                                 DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC;
                }
                else if ( set == to_base( DescriptorSetUsage::PER_PASS ) && slot >= 5u )
                {
                    binding._type = slot == 8u ? DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC :
                                                 DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC;
                }
                else if ( set == to_base( DescriptorSetUsage::PER_FRAME ) && slot >= 7u )
                {
                    binding._type = slot < 9u ? DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC :
                                                DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC;
                }
            }
        }

        struct Variant
        {
            ShaderType _stage;
            const char* _file;
            const char* _source;
            bool _tree;
            bool _prePass;
            bool _display;
            DescriptorSetUsage _bufferSet;
            U8 _bufferSlot;
            bool _alphaDiscard = false;
            bool _shadow = false;
            bool _orthographic = false;
            bool _pbr = false;
            bool _disableIBL = false;
            bool _disableSSAO = false;
            bool _occlusion = false;
            bool _sortedTransparency = false;
        };
        constexpr std::array variants{
            Variant{ ShaderType::COMPUTE, "instanceCountPatch.glsl", "instanceCountPatch.Compute", false, false, false, DescriptorSetUsage::PER_DRAW, 13u },
            Variant{ ShaderType::COMPUTE, "instanceCullVegetation.glsl", "instanceCullVegetation.Compute", false, false, false, DescriptorSetUsage::PER_PASS, 6u },
            Variant{ ShaderType::COMPUTE, "instanceCullVegetation.glsl", "instanceCullVegetation.Compute", true, false, false, DescriptorSetUsage::PER_PASS, 5u },
            Variant{ ._stage = ShaderType::COMPUTE, ._file = "instanceCullVegetation.glsl", ._source = "instanceCullVegetation.Compute",
                     ._tree = false, ._prePass = false, ._display = false, ._bufferSet = DescriptorSetUsage::PER_PASS, ._bufferSlot = 6u, ._occlusion = true },
            Variant{ ._stage = ShaderType::COMPUTE, ._file = "instanceCullVegetation.glsl", ._source = "instanceCullVegetation.Compute",
                     ._tree = true, ._prePass = false, ._display = false, ._bufferSet = DescriptorSetUsage::PER_PASS, ._bufferSlot = 5u, ._occlusion = true },
            Variant{ ShaderType::VERTEX, "grass.glsl", "grass.Vertex", false, true, true, DescriptorSetUsage::PER_DRAW, 13u },
            Variant{ ShaderType::VERTEX, "grass.glsl", "grass.Vertex", false, true, false, DescriptorSetUsage::PER_DRAW, 13u },
            Variant{ ShaderType::VERTEX, "tree.glsl", "tree.Vertex", true, true, false, DescriptorSetUsage::PER_DRAW, 13u },
            Variant{ ShaderType::VERTEX, "tree.glsl", "tree.Vertex", true, false, true, DescriptorSetUsage::PER_DRAW, 13u },
            Variant{ ShaderType::VERTEX, "tree.glsl", "tree.Vertex", true, false, false, DescriptorSetUsage::PER_DRAW, 13u, false, true },
            Variant{ ShaderType::FRAGMENT, "grass.glsl", "grass.Fragment.PrePass", false, true, true, DescriptorSetUsage::COUNT, 0u },
            Variant{ ShaderType::FRAGMENT, "grass.glsl", "grass.Fragment.PrePass", false, true, false, DescriptorSetUsage::COUNT, 0u },
            Variant{ ShaderType::FRAGMENT, "grass.glsl", "grass.Fragment.Colour.OIT", false, false, false, DescriptorSetUsage::COUNT, 0u },
            Variant{ ShaderType::FRAGMENT, "material.glsl", "material.Fragment", false, false, true, DescriptorSetUsage::COUNT, 0u },
            Variant{ ._stage = ShaderType::FRAGMENT, ._file = "material.glsl", ._source = "material.Fragment",
                     ._tree = false, ._prePass = false, ._display = true, ._bufferSet = DescriptorSetUsage::COUNT,
                     ._bufferSlot = 0u, ._sortedTransparency = true },
            Variant{ ShaderType::FRAGMENT, "material.glsl", "material.Fragment", false, false, true, DescriptorSetUsage::COUNT, 0u, true },
            Variant{ ShaderType::FRAGMENT, "material.glsl", "material.Fragment", false, false, true, DescriptorSetUsage::COUNT, 0u, true, false, false, true },
            Variant{ ShaderType::FRAGMENT, "material.glsl", "material.Fragment", false, false, false, DescriptorSetUsage::COUNT, 0u, true, false, false, true },
            Variant{ ShaderType::FRAGMENT, "material.glsl", "material.Fragment", false, false, true, DescriptorSetUsage::COUNT, 0u, true, false, false, true, true },
            Variant{ ShaderType::FRAGMENT, "material.glsl", "material.Fragment", false, false, true, DescriptorSetUsage::COUNT, 0u, true, false, false, true, false, true },
            Variant{ ShaderType::FRAGMENT, "depthPass.glsl", "depthPass.Fragment.PrePass", false, true, true, DescriptorSetUsage::COUNT, 0u, true },
            Variant{ ShaderType::FRAGMENT, "depthPass.glsl", "depthPass.Fragment.PrePass", false, true, false, DescriptorSetUsage::COUNT, 0u, true },
            Variant{ ShaderType::FRAGMENT, "depthPass.glsl", "depthPass.Fragment.Shadow.VSM", false, false, false, DescriptorSetUsage::COUNT, 0u, true, true },
            Variant{ ShaderType::FRAGMENT, "depthPass.glsl", "depthPass.Fragment.Shadow.VSM", false, false, false, DescriptorSetUsage::COUNT, 0u, true, true, true },
            Variant{ ShaderType::FRAGMENT, "grass.glsl", "grass.Fragment.Shadow.VSM", false, false, false, DescriptorSetUsage::COUNT, 0u, false, true },
            Variant{ ShaderType::FRAGMENT, "grass.glsl", "grass.Fragment.Shadow.VSM", false, false, false, DescriptorSetUsage::COUNT, 0u, false, true, true },
            Variant{ ShaderType::FRAGMENT, "particles.glsl", "particles.Fragment.Shadow.VSM", false, false, false, DescriptorSetUsage::COUNT, 0u, false, true },
            Variant{ ShaderType::FRAGMENT, "particles.glsl", "particles.Fragment.Shadow.VSM", false, false, false, DescriptorSetUsage::COUNT, 0u, false, true, true }
        };

        Configuration config{};
        bool firstAPI = true;
        for ( const RenderAPI api : { RenderAPI::Vulkan, RenderAPI::OpenGL } )
        {
            if ( firstAPI )
            {
                REQUIRE( ShaderProgramTestAccess::InitThread( api, config ) );
                firstAPI = false;
            }
            else
            {
                REQUIRE( glswShutdown() == 1 );
                REQUIRE( ShaderProgramTestAccess::InitGLSW( api, config ) );
            }
            const string shaderPath = Paths::Shaders::GLSL::g_GLSLShaderLoc.string() + Paths::g_pathSeparator;
            REQUIRE( glswSetPath( shaderPath.c_str(), ".glsl" ) == 1 );

            for ( const Variant& variant : variants )
            {
                CAPTURE( api, variant._source, variant._tree, variant._prePass, variant._display, variant._alphaDiscard,
                         variant._shadow, variant._orthographic, variant._pbr, variant._disableIBL, variant._disableSSAO,
                         variant._sortedTransparency );
                ModuleDefines defines;
                for ( const char* define : {
                          "MAX_TREE_INSTANCES 64", "MAX_GRASS_INSTANCES 64", "WORK_GROUP_SIZE 64",
                          "ENABLE_TBN", "HAS_POSITION_ATTRIBUTE",
                          "HAS_NORMAL_ATTRIBUTE", "HAS_TEXCOORD_ATTRIBUTE", "USE_UNIT0_TEXTURE",
                          "NODE_DYNAMIC", "OVERRIDE_DATA_IDX" } )
                {
                    defines.emplace_back( define );
                }
                defines.emplace_back( variant._pbr ? "SHADING_MODE_PBR_MR" : "SHADING_MODE_BLINN_PHONG" );
                if ( variant._tree && variant._stage == ShaderType::COMPUTE ) defines.emplace_back( "CULL_TREES" );
                if ( variant._prePass ) defines.emplace_back( "PRE_PASS" );
                if ( variant._display )
                {
                    defines.emplace_back( "MAIN_DISPLAY_PASS" );
                    defines.emplace_back( "HAS_VELOCITY" );
                }
                if ( !variant._prePass && !variant._shadow && !variant._alphaDiscard && variant._stage == ShaderType::FRAGMENT )
                    defines.emplace_back( variant._sortedTransparency ? "TRANSPARENCY_PASS" : "OIT_PASS" );
                if ( variant._alphaDiscard )
                {
                    defines.emplace_back( "HAS_TRANSPARENCY" );
                    defines.emplace_back( "USE_ALPHA_DISCARD" );
                }
                if ( variant._shadow ) defines.emplace_back( "SHADOW_PASS" );
                if ( variant._orthographic ) defines.emplace_back( "ORTHO_PROJECTION" );
                if ( variant._disableIBL ) defines.emplace_back( "NO_ENV_MAPPING" );
                if ( variant._disableSSAO ) defines.emplace_back( "NO_SSAO" );
                if ( variant._occlusion ) defines.emplace_back( "OCCLUSION_PASS" );
                if ( std::string_view{ variant._file } == "particles.glsl" ) defines.emplace_back( "HAS_TEXTURE" );
                const bool standardMaterial = std::string_view{ variant._file } == "material.glsl" ||
                                              std::string_view{ variant._file } == "depthPass.glsl";
                if ( standardMaterial ) defines.emplace_back( "USE_OPACITY_TEXTURE" );

                ShaderProgram::LoadData data{};
                data._type = variant._stage;
                data._sourceFile = variant._file;
                data._sourceName = variant._source;
                data._shaderName = data._sourceName;
                eastl::set<U64> atoms;
                ShaderProgramTestAccess::Parse( api, defines, data, atoms );
                REQUIRE_FALSE( data._sourceCodeGLSL.empty() );
                const bool countPatch = std::string_view{ variant._file } == "instanceCountPatch.glsl";
                if ( variant._stage == ShaderType::FRAGMENT && !variant._prePass && !variant._shadow &&
                     (std::string_view{ variant._file } == "grass.glsl" ||
                      (std::string_view{ variant._file } == "material.glsl" && !variant._alphaDiscard)) )
                {
                    CHECK( atoms.contains( _ID( "transparentCoverage.cmn" ) ) );
                    const size_t entry = data._sourceCodeGLSL.find( "void main" );
                    REQUIRE( entry != string::npos );
                    const size_t rejection = data._sourceCodeGLSL.find( "discard", entry );
                    const size_t normal = data._sourceCodeGLSL.find( "getNormalWV", entry );
                    REQUIRE( rejection != string::npos );
                    REQUIRE( normal != string::npos );
                    CHECK( rejection < normal );
                }
                if ( variant._stage == ShaderType::COMPUTE && !countPatch )
                {
                    CHECK( atoms.contains( _ID( "frustumCulling.cmn" ) ) );
                    CHECK( data._sourceCodeGLSL.find( "IsUnderWater" ) == string::npos );
                }
                if ( countPatch )
                {
                    CHECK( data._sourceCodeGLSL.find( "min(dvd_drawCommands[record.count].instanceCount" ) != string::npos );
                }
                if ( variant._tree && variant._stage == ShaderType::VERTEX )
                {
                    CHECK( data._sourceCodeGLSL.find( "dvd_uniforms.INDIRECT_DATA_IDX" ) != string::npos );
                    CHECK( data._sourceCodeGLSL.find( "dvd_uniforms.dvd_instanceDataIndex" ) != string::npos );
                    CHECK( atoms.contains( _ID("treeWind.cmn") ) );
                    CHECK( data._sourceCodeGLSL.find("const float amplitude") != string::npos );
                    CHECK( data._sourceCodeGLSL.find("dvd_Vertex.y * scale") == string::npos );
                    if (variant._shadow)
                    {
                        CHECK(data._sourceCodeGLSL.find("dvd_enableDeformation") != string::npos);
                    }
                }
                if ( std::string_view{ variant._file } == "grass.glsl" && variant._stage == ShaderType::VERTEX )
                {
                    CHECK( data._sourceCodeGLSL.find( "dvd_uniforms.INDIRECT_DATA_IDX" ) != string::npos );
                    CHECK( data._sourceCodeGLSL.find( "dvd_uniforms.dvd_instanceDataIndex" ) != string::npos );
                }
                if ( variant._stage == ShaderType::FRAGMENT && variant._prePass )
                {
                    CHECK( data._sourceCodeGLSL.find( "discard" ) != string::npos );
                }
                if ( std::string_view{ variant._file } == "material.glsl" && !variant._alphaDiscard )
                {
                    CHECK( data._sourceCodeGLSL.find( "ApplySSR(" ) == string::npos );
                    CHECK( data._sourceCodeGLSL.find( "ApplyIBL(" ) != string::npos );
                    CHECK( data._sourceCodeGLSL.find( "getLightContribution(" ) != string::npos );
                }
                if ( std::string_view{ variant._file } == "material.glsl" &&
                     variant._alphaDiscard && variant._display && !variant._disableIBL )
                {
                    const string& source = data._sourceCodeGLSL;
                    const size_t pixelColour = source.find( "vec4 getPixelColour" );
                    REQUIRE( pixelColour != string::npos );
                    const size_t ibl = source.find( "ApplyIBL(", pixelColour );
                    const size_t ssr = source.find( "ApplySSR(", pixelColour );
                    const size_t direct = source.find( "getLightContribution(", pixelColour );
                    const size_t fog = source.find( "ApplyFog(", direct );
                    REQUIRE( ibl != string::npos );
                    REQUIRE( ssr != string::npos );
                    REQUIRE( direct != string::npos );
                    REQUIRE( fog != string::npos );
                    CHECK( ibl < ssr );
                    CHECK( ssr < direct );
                    CHECK( direct < fog );

                    const size_t ssao = source.find( "texSSAO", pixelColour );
                    if ( variant._disableSSAO )
                    {
                        CHECK( ( ssao == string::npos || ssao > fog ) );
                    }
                    else
                    {
                        REQUIRE( ssao != string::npos );
                        CHECK( ibl < ssao );
                        CHECK( ssao < ssr );
                        const size_t nextSSAO = source.find( "texSSAO", ssao + 1u );
                        CHECK( ( nextSSAO == string::npos || nextSSAO > fog ) );
                    }
                }
                if ( variant._stage == ShaderType::FRAGMENT && (variant._shadow || variant._alphaDiscard) )
                {
                    const size_t main = data._sourceCodeGLSL.rfind( "void main" );
                    REQUIRE( main != string::npos );
                    const size_t discard = data._sourceCodeGLSL.find( "discard", main );
                    REQUIRE( discard != string::npos );
                    if ( variant._shadow )
                    {
                        const size_t moments = data._sourceCodeGLSL.find( "computeMoments", main );
                        REQUIRE( moments != string::npos );
                        CHECK( moments < discard );
                    }
                    else if ( std::string_view{ variant._file } == "material.glsl" )
                    {
                        const size_t normal = data._sourceCodeGLSL.find( "getNormalWV", main );
                        const size_t material = data._sourceCodeGLSL.find( "initMaterialProperties", main );
                        const size_t gradients = data._sourceCodeGLSL.find( "gradients", main );
                        REQUIRE( normal != string::npos );
                        REQUIRE( material != string::npos );
                        REQUIRE( gradients != string::npos );
                        CHECK( normal < discard );
                        CHECK( material < discard );
                        CHECK( gradients < discard );
                    }
                    else if ( variant._prePass )
                    {
                        const size_t gbuffer = data._sourceCodeGLSL.find( "writeGBuffer", main );
                        REQUIRE( gbuffer != string::npos );
                        CHECK( discard < gbuffer );
                    }
                }
                if ( standardMaterial && variant._alphaDiscard )
                {
                    const string& source = data._sourceCodeGLSL;
                    const size_t alpha = source.find( "float getAlpha" );
                    REQUIRE( alpha != string::npos );
                    const size_t alphaEnd = source.find( "vec4 getAlbedo", alpha );
                    const size_t opacity = source.find( "texOpacityMap", alpha );
                    const size_t albedo = source.find( "texDiffuse0", alpha );
                    REQUIRE( alphaEnd != string::npos );
                    REQUIRE( opacity != string::npos );
                    REQUIRE( albedo != string::npos );
                    CHECK( opacity < albedo );
                    CHECK( albedo < alphaEnd );
                    const size_t baseAlphaReturn = source.find( "return", albedo );
                    REQUIRE( baseAlphaReturn != string::npos );
                    CHECK( baseAlphaReturn < alphaEnd );
                }

                std::vector<unsigned int> spirv;
                diagnostics.clear();
                const bool compiled = SpirvHelper::GLSLtoSPV( variant._stage, data._sourceCodeGLSL.c_str(), spirv, api );
                INFO( diagnostics.c_str() );
                REQUIRE( compiled );

                if ( standardMaterial && variant._alphaDiscard )
                {
                    Reflection::Data reflection{};
                    REQUIRE( SpirvHelper::BuildReflectionData( variant._stage, spirv, api, reflection ) );
                    for ( const U8 slot : { U8{ 0u }, U8{ 1u } } )
                    {
                        CAPTURE( slot );
                        CHECK( std::ranges::any_of( reflection._images, [slot]( const Reflection::ImageEntry& image )
                        {
                            return image._bindingSet == to_base( DescriptorSetUsage::PER_DRAW ) &&
                                   image._bindingSlot == slot &&
                                   image._textureType == TextureType::TEXTURE_2D_ARRAY &&
                                   image._combinedImageSampler;
                        } ) );
                    }
                }

                if ( variant._bufferSet != DescriptorSetUsage::COUNT && ( api == RenderAPI::Vulkan || countPatch ) )
                {
                    Reflection::Data reflection{};
                    REQUIRE( SpirvHelper::BuildReflectionData( variant._stage, spirv, api, reflection ) );
                    CHECK( std::ranges::any_of( reflection._buffers, [&]( const Reflection::BufferEntry& entry )
                    {
                        return entry._bindingSet == to_base( variant._bufferSet ) && entry._bindingSlot == variant._bufferSlot;
                    } ) );
                    if ( variant._stage == ShaderType::COMPUTE && !countPatch )
                    {
                        for ( const U8 slot : { U8{14u}, U8{15u} } )
                        {
                            CHECK( std::ranges::any_of( reflection._buffers, [slot]( const Reflection::BufferEntry& entry )
                            {
                                return entry._bindingSet == to_base(DescriptorSetUsage::PER_PASS) && entry._bindingSlot == slot;
                            } ) );
                        }
                    }
                    if ( countPatch )
                    {
                        CHECK( std::ranges::any_of( reflection._buffers, []( const Reflection::BufferEntry& entry )
                        {
                            return entry._bindingSet == to_base( DescriptorSetUsage::PER_BATCH ) && entry._bindingSlot == 2u;
                        } ) );
                    }
                    if ( variant._occlusion )
                    {
                        CHECK( std::ranges::any_of( reflection._images, []( const Reflection::ImageEntry& image )
                        {
                            return image._bindingSet == to_base(DescriptorSetUsage::PER_DRAW) && image._bindingSlot == 0u;
                        } ) );
                    }
                }
            }

            for ( const U8 variant : { U8{ 0u }, U8{ 1u }, U8{ 2u } } )
            {
                for ( const bool overrideDataIndex : { false, true } )
                {
                    CAPTURE( api, variant, overrideDataIndex );
                    ModuleDefines defines;
                    for ( const char* define : {
                              "MAX_GRASS_INSTANCES 64", "ENABLE_TBN", "HAS_POSITION_ATTRIBUTE",
                              "HAS_NORMAL_ATTRIBUTE", "HAS_TEXCOORD_ATTRIBUTE", "USE_UNIT0_TEXTURE",
                              "NODE_DYNAMIC", "SHADING_MODE_BLINN_PHONG" } )
                    {
                        defines.emplace_back( define );
                    }
                    if ( overrideDataIndex ) defines.emplace_back( "OVERRIDE_DATA_IDX" );
                    if ( variant == 0u ) defines.emplace_back( "OIT_PASS" );
                    if ( variant == 1u ) defines.emplace_back( "PRE_PASS" );
                    if ( variant < 2u )
                    {
                        defines.emplace_back( "MAIN_DISPLAY_PASS" );
                        defines.emplace_back( "HAS_VELOCITY" );
                    }
                    else
                    {
                        defines.emplace_back( "SHADOW_PASS" );
                        defines.emplace_back( "ORTHO_PROJECTION" );
                    }

                    std::array<ShaderProgram::LoadData, 2u> stages{};
                    stages[0]._type = ShaderType::VERTEX;
                    stages[0]._sourceName = "grass.Vertex";
                    stages[1]._type = ShaderType::FRAGMENT;
                    stages[1]._sourceName = variant == 0u ? "grass.Fragment.Colour.OIT" :
                                            variant == 1u ? "grass.Fragment.PrePass" : "grass.Fragment.Shadow.VSM";
                    ShaderProgram::UniformBlockLayouts uniformBlocks{};
                    eastl::set<U64> atoms{};
                    for ( ShaderProgram::LoadData& stage : stages )
                    {
                        stage._sourceFile = "grass.glsl";
                        stage._shaderName = stage._sourceName;
                        ShaderProgramTestAccess::Parse( api, defines, stage, atoms, uniformBlocks );
                        REQUIRE_FALSE( stage._sourceCodeGLSL.empty() );
                    }
                    CHECK( stages[0]._reflectionData._uniformBlockBindingIndex == 14u );
                    CHECK( stages[1]._reflectionData._uniformBlockBindingIndex ==
                           ( overrideDataIndex ? 15u : Reflection::INVALID_BINDING_INDEX ) );

                    TBuiltInResource resources{};
                    SpirvHelper::InitResources( resources );
                    const EShMessages messages = static_cast<EShMessages>(
                        EShMsgSpvRules | ( api == RenderAPI::Vulkan ? EShMsgVulkanRules : 0u ) );
                    std::array<const char*, 2u> sourcePointers{};
                    std::array<std::unique_ptr<glslang::TShader>, 2u> shaders{};
                    glslang::TProgram program;
                    for ( size_t i = 0u; i < stages.size(); ++i )
                    {
                        const EShLanguage language = i == 0u ? EShLangVertex : EShLangFragment;
                        shaders[i] = std::make_unique<glslang::TShader>( language );
                        glslang::TShader& shader = *shaders[i];
                        sourcePointers[i] = stages[i]._sourceCodeGLSL.c_str();
                        shader.setStrings( &sourcePointers[i], 1 );
                        if ( api == RenderAPI::Vulkan )
                        {
                            shader.setEnvInput( glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100 );
                            shader.setEnvClient( glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4 );
                            shader.setEnvTarget( glslang::EShTargetSpv, glslang::EShTargetSpv_1_6 );
                        }
                        else
                        {
                            shader.setEnvInput( glslang::EShSourceGlsl, language, glslang::EShClientOpenGL, 100 );
                            shader.setEnvClient( glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450 );
                            shader.setEnvTarget( glslang::EShTargetSpv, glslang::EShTargetSpv_1_0 );
                        }
                        const bool parsed = shader.parse( &resources, 100, false, messages );
                        INFO( shader.getInfoLog() );
                        REQUIRE( parsed );
                        program.addShader( &shader );
                    }
                    const bool linked = program.link( messages );
                    INFO( program.getInfoLog() );
                    REQUIRE( linked );
                }
            }
        }
    }
}
