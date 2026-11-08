// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/RenderSmokeTest.h"
#include "Core/Headers/SmokeOutput.h"
#include "Core/Headers/SmokeRuntimeDialogs.h"
#include "Core/Headers/VulkanRecoveryPolicy.h"

#if defined(IS_WINDOWS_BUILD)
#include <share.h>
#endif

namespace Divide
{
    TEST_CASE( "Smoke runs can diagnose paused editor startup without changing ordinary startup", "[render_smoke][runtime]" )
    {
        CHECK_FALSE( RenderSmokeTest::InitialSimulationPaused( true, true, false ) );
        CHECK_FALSE( RenderSmokeTest::InitialSimulationPaused( true, false, false ) );
        CHECK( RenderSmokeTest::InitialSimulationPaused( true, true, true ) );
        CHECK( RenderSmokeTest::InitialSimulationPaused( true, false, true ) );
        CHECK( RenderSmokeTest::InitialSimulationPaused( false, true, false ) );
        CHECK_FALSE( RenderSmokeTest::InitialSimulationPaused( false, false, false ) );
        CHECK( RenderSmokeTest::InitialSimulationPaused( false, true, true ) );
        CHECK_FALSE( RenderSmokeTest::InitialSimulationPaused( false, false, true ) );
    }

#if defined(IS_WINDOWS_BUILD) && defined(_MSC_VER)
    TEST_CASE( "Smoke mode redirects MSVC abort reports without changing assertion reports", "[render_smoke][runtime]" )
    {
        const int previousErrorMode = _set_error_mode( _REPORT_ERRMODE );
        const unsigned int previousAbortBehavior = _set_abort_behavior( 0, 0 );
#if defined(_DEBUG)
        const int previousReportMode = _CrtSetReportMode( _CRT_ERROR, _CRTDBG_REPORT_MODE );
        const _HFILE previousReportFile = _CrtSetReportFile( _CRT_ERROR, _CRTDBG_REPORT_FILE );
        const int previousAssertMode = _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_REPORT_MODE );
#endif
        SCOPE_EXIT
        {
            _set_error_mode( previousErrorMode );
            _set_abort_behavior( previousAbortBehavior, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
#if defined(_DEBUG)
            _CrtSetReportFile( _CRT_ERROR, previousReportFile );
            _CrtSetReportMode( _CRT_ERROR, previousReportMode );
#endif
        };

        REQUIRE( RenderSmokeTest::ConfigureNonInteractiveCRT() );
        CHECK( _set_error_mode( _REPORT_ERRMODE ) == _OUT_TO_STDERR );
        CHECK( ( _set_abort_behavior( 0, 0 ) & _CALL_REPORTFAULT ) == 0u );
        CHECK( ( _set_abort_behavior( 0, 0 ) & _WRITE_ABORT_MSG ) == ( previousAbortBehavior & _WRITE_ABORT_MSG ) );
#if defined(_DEBUG)
        CHECK( _CrtSetReportMode( _CRT_ERROR, _CRTDBG_REPORT_MODE ) == ( _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG ) );
        CHECK( _CrtSetReportFile( _CRT_ERROR, _CRTDBG_REPORT_FILE ) == GetStdHandle( STD_ERROR_HANDLE ) );
        CHECK( _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_REPORT_MODE ) == previousAssertMode );
#endif
    }
#endif

    TEST_CASE( "Smoke diagnostics remain readable while they are being written", "[render_smoke][output]" )
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                           ( "divide-smoke-output-" + std::to_string( suffix ) + ".log" );
        std::FILE* writer = RenderSmokeTest::OpenSharedOutput( path );
        REQUIRE( writer != nullptr );
        SCOPE_EXIT
        {
            std::fclose( writer );
            std::error_code error;
            std::filesystem::remove( path, error );
        };

        REQUIRE( std::fputs( "GPU uuid=example\n", writer ) >= 0 );
        REQUIRE( std::fflush( writer ) == 0 );
#if defined(IS_WINDOWS_BUILD)
        std::FILE* reader = _wfsopen( path.c_str(), L"rb", _SH_DENYNO );
#else
        std::FILE* reader = std::fopen( path.c_str(), "rb" );
#endif
        REQUIRE( reader != nullptr );
        SCOPE_EXIT
        {
            std::fclose( reader );
        };
        std::array<char, 64> line{};
        REQUIRE( std::fgets( line.data(), to_I32( line.size() ), reader ) != nullptr );
        CHECK( std::string_view{ line.data() } == "GPU uuid=example\n" );
    }

    TEST_CASE( "Pipeline traces preserve program and shader inputs before creation", "[render_smoke][pipeline_trace]" )
    {
        using namespace RenderSmokeTest;

        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                           ( "divide-pipeline-trace-" + std::to_string( suffix ) + ".log" );
        std::FILE* writer = OpenSharedOutput( path );
        REQUIRE( writer != nullptr );
        SCOPE_EXIT
        {
            std::fclose( writer );
            std::error_code error;
            std::filesystem::remove( path, error );
        };

        const std::array stages{
            PipelineTraceStage{ ._name = "terrain.vert", ._module = 0x1520000000152u, ._stage = 1u, ._generation = 2u },
            PipelineTraceStage{ ._name = "terrain.frag", ._module = 0x3ed00000003edu, ._stage = 16u, ._generation = 3u }
        };
        CHECK_FALSE( WritePipelineTrace( nullptr, "terrain", 42, true, false, stages ) );
        CHECK_FALSE( WritePipelineTrace( writer, nullptr, 42, true, false, stages ) );
        CHECK_FALSE( WritePipelineTrace( writer, "", 42, true, false, stages ) );
        CHECK_FALSE( WritePipelineTrace( writer, "terrain", 42, true, false, {} ) );
        CHECK_FALSE( WritePipelineTrace( writer, "terrain", 42, false, true, stages ) );
        const std::array invalidStages{ PipelineTraceStage{} };
        CHECK_FALSE( WritePipelineTrace( writer, "terrain", 42, true, false, invalidStages ) );
        CHECK( std::ftell( writer ) == 0L );

        REQUIRE( WritePipelineTrace( writer, "terrain", 42, true, false, stages ) );
        REQUIRE( WritePipelineTrace( writer, "terrain", 42, true, true, stages ) );
        REQUIRE( WritePipelineTrace( writer, "compute", -1, false, false, std::span{ stages }.first<1>() ) );

#if defined(IS_WINDOWS_BUILD)
        std::FILE* reader = _wfsopen( path.c_str(), L"rb", _SH_DENYNO );
#else
        std::FILE* reader = std::fopen( path.c_str(), "rb" );
#endif
        REQUIRE( reader != nullptr );
        SCOPE_EXIT
        {
            std::fclose( reader );
        };

        const std::array expected{
            std::string_view{ "PIPELINE_CREATE kind=graphics program=terrain guid=42 stages=2\n" },
            std::string_view{ "PIPELINE_STAGE mask=0x1 generation=2 module=0x1520000000152 name=terrain.vert\n" },
            std::string_view{ "PIPELINE_STAGE mask=0x10 generation=3 module=0x3ed00000003ed name=terrain.frag\n" },
            std::string_view{ "PIPELINE_CREATE kind=wireframe program=terrain guid=42 stages=2\n" },
            std::string_view{ "PIPELINE_STAGE mask=0x1 generation=2 module=0x1520000000152 name=terrain.vert\n" },
            std::string_view{ "PIPELINE_STAGE mask=0x10 generation=3 module=0x3ed00000003ed name=terrain.frag\n" },
            std::string_view{ "PIPELINE_CREATE kind=compute program=compute guid=-1 stages=1\n" },
            std::string_view{ "PIPELINE_STAGE mask=0x1 generation=2 module=0x1520000000152 name=terrain.vert\n" }
        };
        std::array<char, 256> line{};
        for ( const std::string_view expectedLine : expected )
        {
            REQUIRE( std::fgets( line.data(), to_I32( line.size() ), reader ) != nullptr );
            CHECK( std::string_view{ line.data() } == expectedLine );
        }
        CHECK( std::fgets( line.data(), to_I32( line.size() ), reader ) == nullptr );
    }

    TEST_CASE( "Pipeline tracing is opt-in and Vulkan-only", "[render_smoke][pipeline_trace]" )
    {
        using namespace RenderSmokeTest;

        Options options;
        std::string error;
        const std::array valid{ std::string_view{ "--vulkanSmokeTest" },
                                std::string_view{ "--smokeOutput=C:\\smoke" },
                                std::string_view{ "--smokeRun=attempt" },
                                std::string_view{ "--smokePipelineTrace" } };
        REQUIRE( ParseOptions( valid, options, error ) == ParseResult::ENABLED );
        CHECK( options._pipelineTrace );

        const std::array normal{ std::string_view{ "--vulkanSmokeTest" },
                                 std::string_view{ "--smokeOutput=C:\\smoke" },
                                 std::string_view{ "--smokeRun=attempt" } };
        REQUIRE( ParseOptions( normal, options, error ) == ParseResult::ENABLED );
        CHECK_FALSE( options._pipelineTrace );

        const std::array noMode{ std::string_view{ "--smokePipelineTrace" } };
        CHECK( ParseOptions( noMode, options, error ) == ParseResult::ERROR );
        const std::array duplicate{ std::string_view{ "--vulkanSmokeTest" },
                                    std::string_view{ "--smokePipelineTrace" },
                                    std::string_view{ "--smokePipelineTrace" } };
        CHECK( ParseOptions( duplicate, options, error ) == ParseResult::ERROR );
        const std::array openGL{ std::string_view{ "--openGLSmokeTest" },
                                 std::string_view{ "--smokeOutput=C:\\smoke" },
                                 std::string_view{ "--smokeRun=attempt" },
                                 std::string_view{ "--smokeExpectedGPU=00000000030000000000000000000000" },
                                 std::string_view{ "--smokePipelineTrace" } };
        CHECK( ParseOptions( openGL, options, error ) == ParseResult::ERROR );
    }

    TEST_CASE( "Render smoke options select a named project and scene", "[render_smoke][options]" )
    {
        using namespace RenderSmokeTest;

        Options options;
        std::string error;
        const std::array valid{ std::string_view{ "--vulkanSmokeTest" },
                                std::string_view{ "--smokeOutput=C:\\smoke" },
                                std::string_view{ "--smokeRun=attempt-01" },
                                std::string_view{ "--smokeProject=Rendering Experiments" },
                                std::string_view{ "--smokeScene=WarScene" } };
        REQUIRE( ParseOptions( valid, options, error ) == ParseResult::ENABLED );
        CHECK( options._project == "Rendering Experiments" );
        CHECK( options._scene == "WarScene" );

        const std::array projectOnly{ std::string_view{ "--vulkanSmokeTest" },
                                      std::string_view{ "--smokeOutput=C:\\smoke" },
                                      std::string_view{ "--smokeRun=attempt-01" },
                                      std::string_view{ "--smokeProject=Games" } };
        REQUIRE( ParseOptions( projectOnly, options, error ) == ParseResult::ENABLED );
        CHECK( options._project == "Games" );
        CHECK( options._scene.empty() );

        const std::array sceneOnly{ std::string_view{ "--vulkanSmokeTest" },
                                    std::string_view{ "--smokeOutput=C:\\smoke" },
                                    std::string_view{ "--smokeRun=attempt-01" },
                                    std::string_view{ "--smokeScene=WarScene" } };
        CHECK( ParseOptions( sceneOnly, options, error ) == ParseResult::ERROR );

        const std::array traversal{ std::string_view{ "--vulkanSmokeTest" },
                                    std::string_view{ "--smokeOutput=C:\\smoke" },
                                    std::string_view{ "--smokeRun=attempt-01" },
                                    std::string_view{ "--smokeProject=..\\Games" } };
        CHECK( ParseOptions( traversal, options, error ) == ParseResult::ERROR );

        const std::array duplicate{ std::string_view{ "--vulkanSmokeTest" },
                                    std::string_view{ "--smokeOutput=C:\\smoke" },
                                    std::string_view{ "--smokeRun=attempt-01" },
                                    std::string_view{ "--smokeProject=Games" },
                                    std::string_view{ "--smokeProject=Default" } };
        CHECK( ParseOptions( duplicate, options, error ) == ParseResult::ERROR );

        CHECK( IsValidContentName( "WarScene" ) );
        CHECK( IsValidContentName( "Rendering Experiments" ) );
        CHECK_FALSE( IsValidContentName( "" ) );
        CHECK_FALSE( IsValidContentName( " Games" ) );
        CHECK_FALSE( IsValidContentName( "Games/WarScene" ) );
        CHECK_FALSE( IsValidContentName( ".." ) );
    }

    TEST_CASE( "Framework smoke options require an isolated output and valid inputs", "[render_smoke][options]" )
    {
        using namespace RenderSmokeTest;

        Options options;
        std::string error;
        const std::array normal{ std::string_view{ "--disableEditor" } };
        CHECK( ParseOptions( normal, options, error ) == ParseResult::DISABLED );

        const std::array valid{ std::string_view{ "--vulkanSmokeTest" },
                                std::string_view{ "--smokeOutput=C:\\smoke" },
                                std::string_view{ "--smokeRun=attempt-01" },
                                std::string_view{ "--smokeFrames=300" },
                                std::string_view{ "--smokeDebugger" },
                                std::string_view{ "--smokeWindowCapture" },
                                std::string_view{ "--smokeSupervisorGate" } };
        REQUIRE( ParseOptions( valid, options, error ) == ParseResult::ENABLED );
        CHECK( options._outputDirectory == "C:\\smoke" );
        CHECK( options._runID == "attempt-01" );
        CHECK( options._targetPresents == 300u );
        CHECK( options._backend == Backend::VULKAN );
        CHECK( options._expectedGPU.empty() );
        CHECK( options._debugger );
        CHECK( options._windowCapture );
        CHECK( options._supervisorGate );
        CHECK_FALSE( options._renderDocCapture );
        CHECK_FALSE( options._pausedEditor );

        const std::array pausedEditor{ std::string_view{ "--vulkanSmokeTest" },
                                       std::string_view{ "--smokeOutput=C:\\smoke" },
                                       std::string_view{ "--smokeRun=attempt-01" },
                                       std::string_view{ "--smokeEditorPaused" } };
        REQUIRE( ParseOptions( pausedEditor, options, error ) == ParseResult::ENABLED );
        CHECK( options._pausedEditor );
        const std::array pausedWithoutMode{ std::string_view{ "--smokeEditorPaused" } };
        CHECK( ParseOptions( pausedWithoutMode, options, error ) == ParseResult::ERROR );
        const std::array duplicatePaused{ std::string_view{ "--vulkanSmokeTest" },
                                          std::string_view{ "--smokeEditorPaused" },
                                          std::string_view{ "--smokeEditorPaused" } };
        CHECK( ParseOptions( duplicatePaused, options, error ) == ParseResult::ERROR );

        const std::array perfMode{ std::string_view{ "--vulkanSmokeTest" },
                                   std::string_view{ "--smokeOutput=C:\\smoke" },
                                   std::string_view{ "--smokeRun=attempt-01" },
                                   std::string_view{ "--smokePerfMode" } };
        REQUIRE( ParseOptions( perfMode, options, error ) == ParseResult::ENABLED );
        CHECK( options._perfMode );
        CHECK_FALSE( options._serialViewRecording );
        const std::array serialViews{ std::string_view{ "--vulkanSmokeTest" },
                                      std::string_view{ "--smokeOutput=C:\\smoke" },
                                      std::string_view{ "--smokeRun=attempt-01" },
                                      std::string_view{ "--smokeSerialViewRecording" } };
        REQUIRE( ParseOptions( serialViews, options, error ) == ParseResult::ENABLED );
        CHECK( options._serialViewRecording );
        const std::array serialWithoutMode{ std::string_view{ "--smokeSerialViewRecording" } };
        CHECK( ParseOptions( serialWithoutMode, options, error ) == ParseResult::ERROR );
        const std::array duplicateSerial{ std::string_view{ "--vulkanSmokeTest" },
                                          std::string_view{ "--smokeSerialViewRecording" },
                                          std::string_view{ "--smokeSerialViewRecording" } };
        CHECK( ParseOptions( duplicateSerial, options, error ) == ParseResult::ERROR );
        const std::array perfWithoutMode{ std::string_view{ "--smokePerfMode" } };
        CHECK( ParseOptions( perfWithoutMode, options, error ) == ParseResult::ERROR );
        const std::array duplicatePerf{ std::string_view{ "--vulkanSmokeTest" },
                                        std::string_view{ "--smokeOutput=C:\\smoke" },
                                        std::string_view{ "--smokeRun=attempt-01" },
                                        std::string_view{ "--smokePerfMode" },
                                        std::string_view{ "--smokePerfMode" } };
        CHECK( ParseOptions( duplicatePerf, options, error ) == ParseResult::ERROR );
        const std::array perfWithDebugger{ std::string_view{ "--vulkanSmokeTest" },
                                           std::string_view{ "--smokeOutput=C:\\smoke" },
                                           std::string_view{ "--smokeRun=attempt-01" },
                                           std::string_view{ "--smokeDebugger" },
                                           std::string_view{ "--smokePerfMode" } };
        CHECK( ParseOptions( perfWithDebugger, options, error ) == ParseResult::ERROR );
        const std::array perfWithTrace{ std::string_view{ "--vulkanSmokeTest" },
                                        std::string_view{ "--smokeOutput=C:\\smoke" },
                                        std::string_view{ "--smokeRun=attempt-01" },
                                        std::string_view{ "--smokePipelineTrace" },
                                        std::string_view{ "--smokePerfMode" } };
        CHECK( ParseOptions( perfWithTrace, options, error ) == ParseResult::ERROR );
        const std::array perfScopes{ std::string_view{ "--vulkanSmokeTest" },
                                     std::string_view{ "--smokeOutput=C:\\smoke" },
                                     std::string_view{ "--smokeRun=attempt-01" },
                                     std::string_view{ "--smokePerfMode" },
                                     std::string_view{ "--smokeGpuScopeTimings" } };
        REQUIRE( ParseOptions( perfScopes, options, error ) == ParseResult::ENABLED );
        CHECK( options._gpuScopeTimings );
        const std::array scopesWithoutPerf{ std::string_view{ "--vulkanSmokeTest" },
                                            std::string_view{ "--smokeOutput=C:\\smoke" },
                                            std::string_view{ "--smokeRun=attempt-01" },
                                            std::string_view{ "--smokeGpuScopeTimings" } };
        CHECK( ParseOptions( scopesWithoutPerf, options, error ) == ParseResult::ERROR );

        const std::array renderDoc{ std::string_view{ "--vulkanSmokeTest" },
                                    std::string_view{ "--smokeOutput=C:\\smoke" },
                                    std::string_view{ "--smokeRun=attempt-01" },
                                    std::string_view{ "--smokeSupervisorGate" },
                                    std::string_view{ "--smokeRenderDocCapture" } };
        REQUIRE( ParseOptions( renderDoc, options, error ) == ParseResult::ENABLED );
        CHECK( options._renderDocCapture );

        const std::array renderDocWithoutMode{ std::string_view{ "--smokeRenderDocCapture" } };
        CHECK( ParseOptions( renderDocWithoutMode, options, error ) == ParseResult::ERROR );
        const std::array renderDocWithoutGate{ std::string_view{ "--vulkanSmokeTest" },
                                               std::string_view{ "--smokeRenderDocCapture" } };
        CHECK( ParseOptions( renderDocWithoutGate, options, error ) == ParseResult::ERROR );
        const std::array duplicateRenderDoc{ std::string_view{ "--vulkanSmokeTest" },
                                            std::string_view{ "--smokeRenderDocCapture" },
                                            std::string_view{ "--smokeRenderDocCapture" } };
        CHECK( ParseOptions( duplicateRenderDoc, options, error ) == ParseResult::ERROR );
        const std::array glRenderDoc{ std::string_view{ "--openGLSmokeTest" },
                                     std::string_view{ "--smokeOutput=C:\\smoke" },
                                     std::string_view{ "--smokeRun=attempt-01" },
                                     std::string_view{ "--smokeExpectedGPU=00000000030000000000000000000000" },
                                     std::string_view{ "--smokeSupervisorGate" },
                                     std::string_view{ "--smokeRenderDocCapture" } };
        CHECK( ParseOptions( glRenderDoc, options, error ) == ParseResult::ERROR );

        const std::array missingMode{ std::string_view{ "--smokeFrames=5" } };
        CHECK( ParseOptions( missingMode, options, error ) == ParseResult::ERROR );
        const std::array windowWithoutMode{ std::string_view{ "--smokeWindowCapture" } };
        CHECK( ParseOptions( windowWithoutMode, options, error ) == ParseResult::ERROR );
        const std::array duplicateWindow{ std::string_view{ "--vulkanSmokeTest" }, std::string_view{ "--smokeWindowCapture" },
                                          std::string_view{ "--smokeWindowCapture" } };
        CHECK( ParseOptions( duplicateWindow, options, error ) == ParseResult::ERROR );
        const std::array gateWithoutMode{ std::string_view{ "--smokeSupervisorGate" } };
        CHECK( ParseOptions( gateWithoutMode, options, error ) == ParseResult::ERROR );
        const std::array duplicateGate{ std::string_view{ "--vulkanSmokeTest" }, std::string_view{ "--smokeSupervisorGate" },
                                        std::string_view{ "--smokeSupervisorGate" } };
        CHECK( ParseOptions( duplicateGate, options, error ) == ParseResult::ERROR );
        const std::array zeroFrames{ std::string_view{ "--vulkanSmokeTest" }, std::string_view{ "--smokeFrames=0" } };
        CHECK( ParseOptions( zeroFrames, options, error ) == ParseResult::ERROR );
        const std::array tooManyFrames{ std::string_view{ "--vulkanSmokeTest" }, std::string_view{ "--smokeFrames=1000001" } };
        CHECK( ParseOptions( tooManyFrames, options, error ) == ParseResult::ERROR );
        const std::array badID{ std::string_view{ "--vulkanSmokeTest" }, std::string_view{ "--smokeOutput=C:\\smoke" }, std::string_view{ "--smokeRun=../bad" } };
        CHECK( ParseOptions( badID, options, error ) == ParseResult::ERROR );
        const std::array disabledValidation{ std::string_view{ "--vulkanSmokeTest" }, std::string_view{ "--smokeOutput=C:\\smoke" },
                                              std::string_view{ "--smokeRun=attempt" }, std::string_view{ "--disableRenderAPIDebugging" } };
        CHECK( ParseOptions( disabledValidation, options, error ) == ParseResult::ERROR );

        const std::array glValid{ std::string_view{ "--openGLSmokeTest" }, std::string_view{ "--smokeOutput=C:\\smoke" },
                                  std::string_view{ "--smokeRun=attempt" },
                                  std::string_view{ "--smokeExpectedGPU=00000000030000000000000000000000" } };
        REQUIRE( ParseOptions( glValid, options, error ) == ParseResult::ENABLED );
        CHECK( options._backend == Backend::OPENGL );
        CHECK( options._expectedGPU == "00000000030000000000000000000000" );
        const std::array glWithoutGPU{ std::string_view{ "--openGLSmokeTest" }, std::string_view{ "--smokeOutput=C:\\smoke" },
                                       std::string_view{ "--smokeRun=attempt" } };
        CHECK( ParseOptions( glWithoutGPU, options, error ) == ParseResult::ERROR );
        const std::array invalidGPU{ std::string_view{ "--openGLSmokeTest" }, std::string_view{ "--smokeExpectedGPU=invalid" } };
        CHECK( ParseOptions( invalidGPU, options, error ) == ParseResult::ERROR );
        const std::array conflictingBackends{ std::string_view{ "--vulkanSmokeTest" }, std::string_view{ "--openGLSmokeTest" } };
        CHECK( ParseOptions( conflictingBackends, options, error ) == ParseResult::ERROR );
        const std::array vulkanWithGPU{ std::string_view{ "--vulkanSmokeTest" }, std::string_view{ "--smokeOutput=C:\\smoke" },
                                       std::string_view{ "--smokeRun=attempt" },
                                       std::string_view{ "--smokeExpectedGPU=00000000030000000000000000000000" } };
        CHECK( ParseOptions( vulkanWithGPU, options, error ) == ParseResult::ERROR );
    }

    TEST_CASE( "Framework smoke mode selects one supported backend", "[render_smoke][options]" )
    {
        using namespace RenderSmokeTest;

        Options options;
        std::string error;
        const std::array vulkan{ std::string_view{ "--frameworkSmokeTest=Vulkan" },
                                 std::string_view{ "--smokeOutput=C:\\smoke" },
                                 std::string_view{ "--smokeRun=attempt" } };
        REQUIRE( ParseOptions( vulkan, options, error ) == ParseResult::ENABLED );
        CHECK( options._backend == Backend::VULKAN );

        const std::array gl{ std::string_view{ "--frameworkSmokeTest=OpenGL" },
                            std::string_view{ "--smokeOutput=C:\\smoke" },
                            std::string_view{ "--smokeRun=attempt" },
                            std::string_view{ "--smokeExpectedGPU=00000000030000000000000000000000" } };
        REQUIRE( ParseOptions( gl, options, error ) == ParseResult::ENABLED );
        CHECK( options._backend == Backend::OPENGL );

        const std::array glWithoutGPU{ std::string_view{ "--frameworkSmokeTest=OpenGL" },
                                      std::string_view{ "--smokeOutput=C:\\smoke" },
                                      std::string_view{ "--smokeRun=attempt" } };
        CHECK( ParseOptions( glWithoutGPU, options, error ) == ParseResult::ERROR );
        const std::array missingBackend{ std::string_view{ "--frameworkSmokeTest" } };
        CHECK( ParseOptions( missingBackend, options, error ) == ParseResult::ERROR );
        const std::array invalidBackend{ std::string_view{ "--frameworkSmokeTest=Metal" } };
        CHECK( ParseOptions( invalidBackend, options, error ) == ParseResult::ERROR );
        const std::array emptyBackend{ std::string_view{ "--frameworkSmokeTest=" } };
        CHECK( ParseOptions( emptyBackend, options, error ) == ParseResult::ERROR );
        const std::array duplicateBackend{ std::string_view{ "--frameworkSmokeTest=Vulkan" },
                                           std::string_view{ "--frameworkSmokeTest=Vulkan" } };
        CHECK( ParseOptions( duplicateBackend, options, error ) == ParseResult::ERROR );
        const std::array conflictingAlias{ std::string_view{ "--frameworkSmokeTest=OpenGL" },
                                          std::string_view{ "--vulkanSmokeTest" } };
        CHECK( ParseOptions( conflictingAlias, options, error ) == ParseResult::ERROR );
    }

    TEST_CASE( "Render smoke options validate the requested material debug view", "[render_smoke][options]" )
    {
        using namespace RenderSmokeTest;

        Options options;
        std::string error;
        const std::array valid{ std::string_view{ "--vulkanSmokeTest" },
                                std::string_view{ "--smokeOutput=C:\\smoke" },
                                std::string_view{ "--smokeRun=attempt" },
                                std::string_view{ "--smokeMaterialView=SHADOW_MAPS" } };
        REQUIRE( ParseOptions( valid, options, error ) == ParseResult::ENABLED );
        CHECK( options._materialView == "SHADOW_MAPS" );

        const std::array invalid{ std::string_view{ "--vulkanSmokeTest" },
                                  std::string_view{ "--smokeMaterialView=UNKNOWN" } };
        CHECK( ParseOptions( invalid, options, error ) == ParseResult::ERROR );
        const std::array empty{ std::string_view{ "--vulkanSmokeTest" },
                                std::string_view{ "--smokeMaterialView=" } };
        CHECK( ParseOptions( empty, options, error ) == ParseResult::ERROR );
        const std::array duplicate{ std::string_view{ "--vulkanSmokeTest" },
                                    std::string_view{ "--smokeMaterialView=SHADOW_MAPS" },
                                    std::string_view{ "--smokeMaterialView=CSM_SPLITS" } };
        CHECK( ParseOptions( duplicate, options, error ) == ParseResult::ERROR );
        const std::array missingMode{ std::string_view{ "--smokeMaterialView=SHADOW_MAPS" } };
        CHECK( ParseOptions( missingMode, options, error ) == ParseResult::ERROR );
    }

    TEST_CASE( "OpenGL smoke selection verifies the Vulkan device UUID", "[render_smoke][gpu_identity]" )
    {
        using namespace RenderSmokeTest;
        constexpr std::array<U8, 16> uuid{{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f }};
        CHECK( IsValidGPUUUID( "000102030405060708090a0b0c0d0e0f" ) );
        CHECK( MatchesGPUUUID( uuid, "000102030405060708090A0B0C0D0E0F" ) );
        CHECK_FALSE( MatchesGPUUUID( uuid, "000102030405060708090a0b0c0d0e00" ) );
        CHECK_FALSE( MatchesGPUUUID( std::span{ uuid }.first( 15u ), "000102030405060708090a0b0c0d0e0f" ) );
        CHECK_FALSE( IsValidGPUUUID( "000102030405060708090a0b0c0d0e0g" ) );
        CHECK_FALSE( IsValidGPUUUID( "00010203" ) );
    }

    TEST_CASE( "Smoke framebuffer readbacks reject invalid dimensions", "[render_smoke][readback]" )
    {
        size_t bytes = 77u;
        CHECK( RenderSmokeTest::RGBA8ReadbackSize( 1920u, 1080u, bytes ) );
        CHECK( bytes == 1920u * 1080u * 4u );
        CHECK_FALSE( RenderSmokeTest::RGBA8ReadbackSize( 0u, 1080u, bytes ) );
        CHECK_FALSE( RenderSmokeTest::RGBA8ReadbackSize( 1920u, 0u, bytes ) );
        CHECK_FALSE( RenderSmokeTest::RGBA8ReadbackSize( 65536u, 1u, bytes ) );
        CHECK_FALSE( RenderSmokeTest::RGBA8ReadbackSize( 1u, 65536u, bytes ) );
        CHECK( bytes == 1920u * 1080u * 4u );
    }

    TEST_CASE( "The smoke supervisor gate requires a regular acknowledgement file", "[render_smoke][supervisor]" )
    {
        using namespace RenderSmokeTest;

        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-smoke-gate-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        const std::filesystem::path gate = directory / "supervisor.ready";
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove( gate, error );
            std::filesystem::remove( directory, error );
        };

        CHECK( WaitForSupervisorGate( directory.string(), 0u ) == SupervisorGateResult::TIMED_OUT );
        REQUIRE( std::filesystem::create_directory( gate ) );
        CHECK( WaitForSupervisorGate( directory.string(), 0u ) == SupervisorGateResult::INVALID );
        REQUIRE( std::filesystem::remove( gate ) );
        std::FILE* output = OpenSharedOutput( gate );
        REQUIRE( output != nullptr );
        REQUIRE( std::fclose( output ) == 0 );
        CHECK( WaitForSupervisorGate( directory.string(), 0u ) == SupervisorGateResult::RELEASED );

        REQUIRE( std::filesystem::remove( gate ) );
        std::atomic_bool signalled{ false };
        std::thread supervisor( [&]
        {
            std::this_thread::sleep_for( std::chrono::milliseconds{ 50 } );
            std::FILE* signal = OpenSharedOutput( gate );
            if ( signal != nullptr )
            {
                signalled.store( std::fclose( signal ) == 0 );
            }
        } );
        const SupervisorGateResult result = WaitForSupervisorGate( directory.string(), 2000u );
        supervisor.join();
        CHECK( signalled.load() );
        CHECK( result == SupervisorGateResult::RELEASED );
    }

    TEST_CASE( "Vulkan smoke validation exempts only the approved custom-border advisory", "[render_smoke][validation]" )
    {
        using RenderSmokeTest::IsFatalValidationSeverity;
        constexpr std::string_view advisoryID = "BestPractices-specialuse-extension";
        constexpr std::string_view advisory = "vkCreateDevice(): Attempting to enable extension VK_EXT_custom_border_color, but this extension is intended for D3D emulation.";
        CHECK_FALSE( IsFatalValidationSeverity( false, false ) );
        CHECK( IsFatalValidationSeverity( true, false ) );
        CHECK( IsFatalValidationSeverity( false, true ) );
        CHECK( IsFatalValidationSeverity( true, true ) );
        CHECK_FALSE( IsFatalValidationSeverity( true, false, advisoryID, advisory ) );
        CHECK( IsFatalValidationSeverity( false, true, advisoryID, advisory ) );
        CHECK( IsFatalValidationSeverity( true, true, advisoryID, advisory ) );
        CHECK( IsFatalValidationSeverity( true, false, "another-warning", advisory ) );
        CHECK( IsFatalValidationSeverity( true, false, advisoryID, "Attempting to enable extension VK_EXT_other," ) );
        CHECK( IsFatalValidationSeverity( true, false, advisoryID, "Attempting to enable extension VK_EXT_custom_border_color_other," ) );
        CHECK( IsFatalValidationSeverity( true, false, advisoryID, "Unrelated warning: vkCreateDevice(): Attempting to enable extension VK_EXT_custom_border_color," ) );
        CHECK( IsFatalValidationSeverity( true, false, advisoryID, "" ) );
    }

    TEST_CASE( "Only the first concurrent Vulkan failure owns diagnostic capture", "[render_smoke][validation]" )
    {
        RenderSmokeTest::FirstFailureLatch latch;
        std::atomic<U32> claims{ 0u };
        std::array<std::thread, 8> threads;
        for ( std::thread& thread : threads )
        {
            thread = std::thread( [&]
            {
                if ( latch.tryClaim() )
                {
                    claims.fetch_add( 1u );
                }
            } );
        }
        for ( std::thread& thread : threads )
        {
            thread.join();
        }
        CHECK( claims.load() == 1u );
        CHECK_FALSE( latch.tryClaim() );
    }

    TEST_CASE( "Only a queue-submit device-loss result defers validation termination", "[render_smoke][device_fault]" )
    {
        using RenderSmokeTest::IsQueueSubmitDeviceLossResult;
        constexpr std::string_view id = "BestPractices-Error-Result";
        constexpr std::string_view message = "vkQueueSubmit(): Returned error VK_ERROR_DEVICE_LOST. Details";
        CHECK( IsQueueSubmitDeviceLossResult( true, id, message ) );
        CHECK_FALSE( IsQueueSubmitDeviceLossResult( false, id, message ) );
        CHECK_FALSE( IsQueueSubmitDeviceLossResult( true, "different-id", message ) );
        CHECK_FALSE( IsQueueSubmitDeviceLossResult( true, id, "vkQueuePresentKHR(): Returned error VK_ERROR_DEVICE_LOST." ) );
        CHECK_FALSE( IsQueueSubmitDeviceLossResult( true, id, "vkQueueSubmit(): Returned error VK_ERROR_OUT_OF_HOST_MEMORY." ) );
        CHECK_FALSE( IsQueueSubmitDeviceLossResult( true, id, "Earlier warning: vkQueueSubmit(): Returned error VK_ERROR_DEVICE_LOST." ) );
    }

    TEST_CASE( "Vulkan smoke completion requires scene presents and a completed capture", "[render_smoke][progress]" )
    {
        RenderSmokeTest::Progress progress{ 2u };
        progress.beginFrame();
        progress.markSceneFrame();
        CHECK_FALSE( progress.recordPresent( true, true, false ) );
        CHECK( progress.presentCount() == 0u );
        progress.markSceneReady();
        CHECK_FALSE( progress.recordPresent( true, true, false ) );
        progress.markSceneFrame();
        CHECK_FALSE( progress.recordPresent( false, true, false ) );
        CHECK_FALSE( progress.recordPresent( true, false, false ) );
        progress.markSceneFrame();
        CHECK_FALSE( progress.recordPresent( true, true, true ) );
        progress.markSceneFrame();
        REQUIRE( progress.recordPresent( true, true, false ) );
        CHECK( progress.presentCount() == 1u );
        CHECK_FALSE( progress.tryBeginCapture() );
        progress.markSceneFrame();
        progress.beginFrame();
        CHECK_FALSE( progress.recordPresent( true, true, false ) );
        progress.markSceneFrame();
        REQUIRE( progress.recordPresent( true, true, false ) );
        CHECK_FALSE( progress.readyToStop() );
        REQUIRE( progress.tryBeginCapture() );
        CHECK_FALSE( progress.tryBeginCapture() );
        progress.finishCapture( true );
        CHECK( progress.readyToStop() );
        progress.fail();
        CHECK_FALSE( progress.readyToStop() );

        progress.reset( 1u );
        progress.markSceneReady();
        progress.markSceneFrame();
        REQUIRE( progress.recordPresent( true, true, false ) );
        REQUIRE( progress.tryBeginCapture() );
        progress.finishCapture( false );
        CHECK_FALSE( progress.readyToStop() );
    }

    TEST_CASE( "Requested editor captures gate a successful smoke shutdown", "[render_smoke][progress]" )
    {
        RenderSmokeTest::Progress progress{ 1u, true };
        CHECK_FALSE( progress.awaitingWindowCapture() );
        progress.markSceneReady();
        progress.markSceneFrame();
        REQUIRE( progress.recordPresent( true, true, false ) );
        REQUIRE( progress.tryBeginCapture() );
        progress.finishCapture( true );
        CHECK( progress.awaitingWindowCapture() );
        CHECK_FALSE( progress.readyToStop() );
        progress.finishWindowCapture( true );
        CHECK_FALSE( progress.awaitingWindowCapture() );
        CHECK( progress.readyToStop() );

        progress.reset( 1u, true );
        progress.markSceneReady();
        progress.markSceneFrame();
        REQUIRE( progress.recordPresent( true, true, false ) );
        REQUIRE( progress.tryBeginCapture() );
        progress.finishCapture( true );
        progress.finishWindowCapture( false );
        CHECK_FALSE( progress.awaitingWindowCapture() );
        CHECK_FALSE( progress.readyToStop() );

        progress.reset( 1u, true );
        progress.finishWindowCapture( true );
        CHECK_FALSE( progress.readyToStop() );
    }

    TEST_CASE( "A recovered GPU must match and complete a fresh-device submission", "[render_smoke][recovery]" )
    {
        using RenderSmokeTest::RecoveryProgress;

        RecoveryProgress recovered;
        REQUIRE( recovered.matchDevice( true ) );
        REQUIRE( recovered.createDevice( true ) );
        REQUIRE( recovered.submit( true ) );
        CHECK_FALSE( recovered.recovered() );
        REQUIRE( recovered.completeFence( true ) );
        CHECK( recovered.recovered() );

        RecoveryProgress wrongGPU;
        CHECK_FALSE( wrongGPU.matchDevice( false ) );
        CHECK_FALSE( wrongGPU.createDevice( true ) );
        CHECK_FALSE( wrongGPU.recovered() );

        RecoveryProgress wrongOrder;
        CHECK_FALSE( wrongOrder.completeFence( true ) );
        CHECK_FALSE( wrongOrder.matchDevice( true ) );
        CHECK_FALSE( wrongOrder.recovered() );

        RecoveryProgress lostDevice;
        REQUIRE( lostDevice.matchDevice( true ) );
        CHECK_FALSE( lostDevice.createDevice( false ) );
        CHECK_FALSE( lostDevice.recovered() );

        RecoveryProgress stalledSubmission;
        REQUIRE( stalledSubmission.matchDevice( true ) );
        REQUIRE( stalledSubmission.createDevice( true ) );
        REQUIRE( stalledSubmission.submit( true ) );
        CHECK_FALSE( stalledSubmission.completeFence( false ) );
        CHECK_FALSE( stalledSubmission.recovered() );
    }

    TEST_CASE( "A smoke result requires clean shutdown and completed progress", "[render_smoke][result]" )
    {
        using RenderSmokeTest::IsSuccessfulResult;
        CHECK( IsSuccessfulResult( true, true, true ) );
        CHECK_FALSE( IsSuccessfulResult( false, true, true ) );
        CHECK_FALSE( IsSuccessfulResult( true, false, true ) );
        CHECK_FALSE( IsSuccessfulResult( true, true, false ) );
    }
}
