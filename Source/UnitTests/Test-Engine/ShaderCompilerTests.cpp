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
#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Shaders/Headers/GLSLToSPIRV.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Shaders/glsw/Headers/glsw.h"

#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

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
    static ResourcePath& CommonAtomLocation() { return ShaderProgram::shaderAtomLocationPrefix[to_base( ShaderType::COUNT )]; }

    static void ExpandIncludes( string& source, eastl::set<U64>& atoms )
    {
        ShaderProgram::PreprocessIncludes( "shader test", source, atoms );
    }

    static void Parse( const RenderAPI api, const ModuleDefines& defines, ShaderProgram::LoadData& data,
                       eastl::set<U64>& atoms, Reflection::UniformsSet& previousUniforms, U8& blockIndex )
    {
        ShaderProgram::loadAndParseGLSL( "shader test", api, defines, data, previousUniforms, blockIndex, atoms );
    }

    static void Parse( const RenderAPI api, const ModuleDefines& defines, ShaderProgram::LoadData& data,
                       eastl::set<U64>& atoms )
    {
        Reflection::UniformsSet previousUniforms{};
        U8 blockIndex = 0u;
        Parse( api, defines, data, atoms, previousUniforms, blockIndex );
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

TEST_CASE( "Shader thread preprocessing survives repeated initialization", "[shader]" )
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

TEST_CASE( "Shader diagnostics accept full-length source text", "[shader]" )
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

TEST_CASE( "Shader include preprocessing handles nested and invalid inputs", "[shader][includes]" )
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

}
}
