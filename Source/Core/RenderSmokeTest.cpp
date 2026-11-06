// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/RenderSmokeTest.h"
#include "Headers/SmokeOutput.h"
#include "Headers/SmokeRuntimeDialogs.h"

#include "Geometry/Material/Headers/Material.h"

#include <cstdarg>
#include <cinttypes>
#include <mutex>

#if defined(IS_WINDOWS_BUILD)
#include <share.h>
#endif

namespace Divide::RenderSmokeTest
{
    std::FILE* OpenSharedOutput( const std::filesystem::path& path ) noexcept
    {
#if defined(IS_WINDOWS_BUILD)
        return _wfsopen( path.c_str(), L"wb", _SH_DENYNO );
#else
        return std::fopen( path.c_str(), "wb" );
#endif
    }

    bool WritePipelineTrace( std::FILE* output, const char* program, const I64 guid, const bool graphics, const bool wireframe,
                             const std::span<const PipelineTraceStage> stages ) noexcept
    {
        if ( output == nullptr || program == nullptr || *program == '\0' || stages.empty() || ( wireframe && !graphics ) )
        {
            return false;
        }
        for ( const PipelineTraceStage& stage : stages )
        {
            if ( stage._name == nullptr || *stage._name == '\0' )
            {
                return false;
            }
        }

        const char* kind = wireframe ? "wireframe" : ( graphics ? "graphics" : "compute" );
        if ( std::fprintf( output, "PIPELINE_CREATE kind=%s program=%s guid=%" PRId64 " stages=%zu\n",
                           kind, program, guid, stages.size() ) < 0 )
        {
            return false;
        }
        for ( const PipelineTraceStage& stage : stages )
        {
            if ( std::fprintf( output, "PIPELINE_STAGE mask=0x%x generation=%u module=0x%" PRIx64 " name=%s\n",
                               stage._stage, stage._generation, stage._module, stage._name ) < 0 )
            {
                return false;
            }
        }
        return std::fflush( output ) == 0;
    }

    namespace
    {
        constexpr U32 MAX_TARGET_PRESENTS = 1000000u;

        struct Runtime
        {
            Options _options{};
            Progress _progress{ 300u };
            FirstFailureLatch _failureLatch{};
            std::mutex _eventMutex{};
            std::FILE* _events{ nullptr };
            bool _enabled{ false };
            bool _normalStop{ false };
        };

        Runtime& State() noexcept
        {
            static Runtime state{};
            return state;
        }

        [[noreturn]] void FailOutput() noexcept
        {
            std::fprintf( stderr, "Render smoke test could not write its diagnostic output.\n" );
            std::fflush( stderr );
            std::_Exit( IO_FAILURE_EXIT );
        }

        void Emit( const char* format, ... ) noexcept
        {
            Runtime& state = State();
            std::lock_guard lock( state._eventMutex );
            if ( state._events == nullptr )
            {
                FailOutput();
            }

            va_list args;
            va_start( args, format );
            const int written = std::vfprintf( state._events, format, args );
            va_end( args );
            if ( written < 0 || std::fputc( '\n', state._events ) == EOF || std::fflush( state._events ) != 0 )
            {
                FailOutput();
            }
        }

        [[nodiscard]] bool ParseCount( const std::string_view text, U32& countOut ) noexcept
        {
            if ( text.empty() )
            {
                return false;
            }

            U32 count = 0u;
            for ( const char digit : text )
            {
                if ( digit < '0' || digit > '9' )
                {
                    return false;
                }

                const U32 value = to_U32( digit - '0' );
                if ( count > ( MAX_TARGET_PRESENTS - value ) / 10u )
                {
                    return false;
                }
                count = count * 10u + value;
            }

            if ( count == 0u || count > MAX_TARGET_PRESENTS )
            {
                return false;
            }

            countOut = count;
            return true;
        }

        [[nodiscard]] bool IsValidRunID( const std::string_view id ) noexcept
        {
            if ( id.empty() || id.size() > 64u || id == "." || id == ".." )
            {
                return false;
            }

            for ( const char character : id )
            {
                if ( !( character >= 'a' && character <= 'z' ) &&
                     !( character >= 'A' && character <= 'Z' ) &&
                     !( character >= '0' && character <= '9' ) &&
                     character != '-' && character != '_' && character != '.' )
                {
                    return false;
                }
            }
            return true;
        }
    }

    ParseResult ParseOptions( const std::span<const std::string_view> arguments, Options& optionsOut, std::string& errorOut )
    {
        Options options{};
        bool mode = false;
        bool sawOption = false;
        bool hasOutput = false;
        bool hasRunID = false;
        bool hasFrames = false;
        bool hasExpectedGPU = false;
        bool hasProject = false;
        bool hasScene = false;
        bool hasMaterialView = false;
        bool incompatible = false;

        for ( const std::string_view argument : arguments )
        {
            const bool frameworkMode = argument.starts_with( "--frameworkSmokeTest=" );
            if ( frameworkMode || argument == "--frameworkSmokeTest" ||
                 argument == "--vulkanSmokeTest" || argument == "--openGLSmokeTest" )
            {
                if ( mode )
                {
                    errorOut = "Only one render smoke backend can be selected";
                    return ParseResult::ERROR;
                }
                if ( argument == "--frameworkSmokeTest" )
                {
                    errorOut = "--frameworkSmokeTest requires =Vulkan or =OpenGL";
                    return ParseResult::ERROR;
                }

                const std::string_view backend = frameworkMode
                    ? argument.substr( std::string_view{ "--frameworkSmokeTest=" }.size() )
                    : ( argument == "--vulkanSmokeTest" ? "Vulkan" : "OpenGL" );
                if ( backend != "Vulkan" && backend != "OpenGL" )
                {
                    errorOut = "Invalid --frameworkSmokeTest backend";
                    return ParseResult::ERROR;
                }
                mode = true;
                options._backend = backend == "Vulkan" ? Backend::VULKAN : Backend::OPENGL;
            }
            else if ( argument.starts_with( "--smokeOutput=" ) )
            {
                sawOption = true;
                if ( hasOutput )
                {
                    errorOut = "Duplicate --smokeOutput option";
                    return ParseResult::ERROR;
                }
                options._outputDirectory = argument.substr( std::string_view{ "--smokeOutput=" }.size() );
                hasOutput = true;
            }
            else if ( argument.starts_with( "--smokeRun=" ) )
            {
                sawOption = true;
                if ( hasRunID )
                {
                    errorOut = "Duplicate --smokeRun option";
                    return ParseResult::ERROR;
                }
                options._runID = argument.substr( std::string_view{ "--smokeRun=" }.size() );
                hasRunID = true;
            }
            else if ( argument.starts_with( "--smokeFrames=" ) )
            {
                sawOption = true;
                if ( hasFrames || !ParseCount( argument.substr( std::string_view{ "--smokeFrames=" }.size() ), options._targetPresents ) )
                {
                    errorOut = "Invalid or duplicate --smokeFrames option";
                    return ParseResult::ERROR;
                }
                hasFrames = true;
            }
            else if ( argument.starts_with( "--smokeExpectedGPU=" ) )
            {
                sawOption = true;
                const std::string_view uuid = argument.substr( std::string_view{ "--smokeExpectedGPU=" }.size() );
                if ( hasExpectedGPU || !IsValidGPUUUID( uuid ) )
                {
                    errorOut = "Invalid or duplicate --smokeExpectedGPU option";
                    return ParseResult::ERROR;
                }
                options._expectedGPU = uuid;
                hasExpectedGPU = true;
            }
            else if ( argument.starts_with( "--smokeProject=" ) )
            {
                sawOption = true;
                const std::string_view name = argument.substr( std::string_view{ "--smokeProject=" }.size() );
                if ( hasProject || !IsValidContentName( name ) )
                {
                    errorOut = "Invalid or duplicate --smokeProject option";
                    return ParseResult::ERROR;
                }
                options._project = name;
                hasProject = true;
            }
            else if ( argument.starts_with( "--smokeScene=" ) )
            {
                sawOption = true;
                const std::string_view name = argument.substr( std::string_view{ "--smokeScene=" }.size() );
                if ( hasScene || !IsValidContentName( name ) )
                {
                    errorOut = "Invalid or duplicate --smokeScene option";
                    return ParseResult::ERROR;
                }
                options._scene = name;
                hasScene = true;
            }
            else if ( argument.starts_with( "--smokeMaterialView=" ) )
            {
                sawOption = true;
                const std::string_view name = argument.substr( std::string_view{ "--smokeMaterialView=" }.size() );
                if ( hasMaterialView || TypeUtil::StringToMaterialDebugFlag( name ) == MaterialDebugFlag::COUNT )
                {
                    errorOut = "Invalid or duplicate --smokeMaterialView option";
                    return ParseResult::ERROR;
                }
                options._materialView = name;
                hasMaterialView = true;
            }
            else if ( argument == "--smokeDebugger" )
            {
                sawOption = true;
                if ( options._debugger )
                {
                    errorOut = "Duplicate --smokeDebugger option";
                    return ParseResult::ERROR;
                }
                options._debugger = true;
            }
            else if ( argument == "--smokeWindowCapture" )
            {
                sawOption = true;
                if ( options._windowCapture )
                {
                    errorOut = "Duplicate --smokeWindowCapture option";
                    return ParseResult::ERROR;
                }
                options._windowCapture = true;
            }
            else if ( argument == "--smokeSupervisorGate" )
            {
                sawOption = true;
                if ( options._supervisorGate )
                {
                    errorOut = "Duplicate --smokeSupervisorGate option";
                    return ParseResult::ERROR;
                }
                options._supervisorGate = true;
            }
            else if ( argument == "--smokeRenderDocCapture" )
            {
                sawOption = true;
                if ( options._renderDocCapture )
                {
                    errorOut = "Duplicate --smokeRenderDocCapture option";
                    return ParseResult::ERROR;
                }
                options._renderDocCapture = true;
            }
            else if ( argument == "--smokePipelineTrace" )
            {
                sawOption = true;
                if ( options._pipelineTrace )
                {
                    errorOut = "Duplicate --smokePipelineTrace option";
                    return ParseResult::ERROR;
                }
                options._pipelineTrace = true;
            }
            else if ( argument == "--smokeEditorPaused" )
            {
                sawOption = true;
                if ( options._pausedEditor )
                {
                    errorOut = "Duplicate --smokeEditorPaused option";
                    return ParseResult::ERROR;
                }
                options._pausedEditor = true;
            }
            else if ( argument == "--smokePerfMode" )
            {
                sawOption = true;
                if ( options._perfMode )
                {
                    errorOut = "Duplicate --smokePerfMode option";
                    return ParseResult::ERROR;
                }
                options._perfMode = true;
            }
            else if ( argument == "--smokeGpuScopeTimings" )
            {
                sawOption = true;
                if ( options._gpuScopeTimings )
                {
                    errorOut = "Duplicate --smokeGpuScopeTimings option";
                    return ParseResult::ERROR;
                }
                options._gpuScopeTimings = true;
            }
            else if ( argument == "--disableRenderAPIDebugging" || argument == "--disableEditor" )
            {
                incompatible = true;
            }
        }

        if ( !mode )
        {
            if ( sawOption )
            {
                errorOut = "Smoke options require a render smoke backend";
                return ParseResult::ERROR;
            }
            return ParseResult::DISABLED;
        }

        if ( hasScene && !hasProject )
        {
            errorOut = "--smokeScene requires --smokeProject";
            return ParseResult::ERROR;
        }

        if ( options._renderDocCapture && ( options._backend != Backend::VULKAN || !options._supervisorGate ) )
        {
            errorOut = "--smokeRenderDocCapture requires Vulkan and the supervisor gate";
            return ParseResult::ERROR;
        }

        if ( options._pipelineTrace && options._backend != Backend::VULKAN )
        {
            errorOut = "--smokePipelineTrace requires Vulkan";
            return ParseResult::ERROR;
        }

        if ( options._perfMode && ( options._pipelineTrace || options._renderDocCapture || options._debugger ) )
        {
            errorOut = "--smokePerfMode cannot be combined with pipeline tracing, RenderDoc capture or the debugger";
            return ParseResult::ERROR;
        }

        if ( options._gpuScopeTimings && !options._perfMode )
        {
            errorOut = "--smokeGpuScopeTimings requires --smokePerfMode";
            return ParseResult::ERROR;
        }

        if ( incompatible || !hasOutput || options._outputDirectory.empty() || !hasRunID || !IsValidRunID( options._runID ) ||
             ( options._backend == Backend::OPENGL ) != hasExpectedGPU )
        {
            errorOut = "The render smoke test requires an output directory, a valid run ID, API debugging, the editor, "
                       "and an expected GPU UUID for OpenGL";
            return ParseResult::ERROR;
        }

        optionsOut = std::move( options );
        errorOut.clear();
        return ParseResult::ENABLED;
    }

    SupervisorGateResult WaitForSupervisorGate( const std::string& outputDirectory, const U32 timeoutMilliseconds ) noexcept
    {
        const std::filesystem::path gate = std::filesystem::path{ outputDirectory } / "supervisor.ready";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{ timeoutMilliseconds };
        do
        {
            std::error_code error;
            const bool exists = std::filesystem::exists( gate, error );
            if ( error )
            {
                return SupervisorGateResult::INVALID;
            }
            if ( exists )
            {
                return std::filesystem::is_regular_file( gate, error ) && !error
                     ? SupervisorGateResult::RELEASED
                     : SupervisorGateResult::INVALID;
            }
            if ( std::chrono::steady_clock::now() >= deadline )
            {
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds{ 25 } );
        } while ( true );
        return SupervisorGateResult::TIMED_OUT;
    }

    Progress::Progress( const U32 targetPresents, const bool requireWindowCapture ) noexcept
        : _targetPresents( targetPresents )
        , _requireWindowCapture( requireWindowCapture )
    {
    }

    void Progress::reset( const U32 targetPresents, const bool requireWindowCapture ) noexcept
    {
        _targetPresents = targetPresents;
        _requireWindowCapture = requireWindowCapture;
        _presentCount.store( 0u );
        _sceneReady.store( false );
        _sceneFrame.store( false );
        _captureStarted.store( false );
        _captureComplete.store( false );
        _windowCaptureComplete.store( false );
        _failed.store( false );
    }

    void Progress::markSceneReady() noexcept
    {
        _sceneReady.store( true, std::memory_order_release );
    }

    void Progress::beginFrame() noexcept
    {
        _sceneFrame.store( false, std::memory_order_release );
    }

    void Progress::markSceneFrame() noexcept
    {
        if ( _sceneReady.load( std::memory_order_acquire ) )
        {
            _sceneFrame.store( true, std::memory_order_release );
        }
    }

    bool Progress::recordPresent( const bool mainWindow, const bool successful, const bool recreating ) noexcept
    {
        if ( !mainWindow )
        {
            return false;
        }
        const bool sceneFrame = _sceneFrame.exchange( false, std::memory_order_acq_rel );
        if ( _failed.load( std::memory_order_acquire ) ||
             !IsEligiblePresent( _sceneReady.load( std::memory_order_acquire ), sceneFrame, mainWindow, successful, recreating ) )
        {
            return false;
        }
        _presentCount.fetch_add( 1u, std::memory_order_relaxed );
        return true;
    }

    bool Progress::tryBeginCapture() noexcept
    {
        if ( _failed.load( std::memory_order_acquire ) ||
             !_sceneReady.load( std::memory_order_acquire ) ||
             presentCount() < _targetPresents )
        {
            return false;
        }

        bool expected = false;
        return _captureStarted.compare_exchange_strong( expected, true, std::memory_order_acq_rel );
    }

    void Progress::finishCapture( const bool successful ) noexcept
    {
        if ( successful && _captureStarted.load( std::memory_order_acquire ) )
        {
            _captureComplete.store( true, std::memory_order_release );
        }
        else
        {
            fail();
        }
    }

    bool Progress::awaitingWindowCapture() const noexcept
    {
        return _requireWindowCapture &&
               !_failed.load( std::memory_order_acquire ) &&
               _captureComplete.load( std::memory_order_acquire ) &&
               !_windowCaptureComplete.load( std::memory_order_acquire );
    }

    void Progress::finishWindowCapture( const bool successful ) noexcept
    {
        if ( successful && awaitingWindowCapture() )
        {
            _windowCaptureComplete.store( true, std::memory_order_release );
        }
        else
        {
            fail();
        }
    }

    void Progress::fail() noexcept
    {
        _failed.store( true, std::memory_order_release );
    }

    U32 Progress::presentCount() const noexcept
    {
        return _presentCount.load( std::memory_order_relaxed );
    }

    bool Progress::readyToStop() const noexcept
    {
        return !_failed.load( std::memory_order_acquire ) &&
               _sceneReady.load( std::memory_order_acquire ) &&
               presentCount() >= _targetPresents &&
               _captureComplete.load( std::memory_order_acquire ) &&
               ( !_requireWindowCapture || _windowCaptureComplete.load( std::memory_order_acquire ) );
    }

    bool Configure( const I32 argc, char** argv )
    {
        if ( argc < 0 || ( argc > 0 && argv == nullptr ) )
        {
            std::fprintf( stderr, "Invalid command-line arguments for render smoke test.\n" );
            return false;
        }

        std::vector<std::string_view> arguments;
        arguments.reserve( to_size( argc ) );
        for ( I32 i = 0; i < argc; ++i )
        {
            arguments.emplace_back( argv[i] != nullptr ? argv[i] : "" );
        }

        Runtime& state = State();
        std::string error;
        const ParseResult result = ParseOptions( arguments, state._options, error );
        if ( result == ParseResult::DISABLED )
        {
            return true;
        }
        if ( result == ParseResult::ERROR )
        {
            std::fprintf( stderr, "Render smoke test: %s\n", error.c_str() );
            return false;
        }

        if ( !ConfigureNonInteractiveCRT() )
        {
            std::fprintf( stderr, "Render smoke test could not configure noninteractive CRT error reporting.\n" );
            return false;
        }

        const std::filesystem::path directory{ state._options._outputDirectory };
        std::error_code errorCode;
        if ( !directory.is_absolute() || !std::filesystem::is_directory( directory, errorCode ) || errorCode )
        {
            std::fprintf( stderr, "Render smoke test output must be an existing absolute directory.\n" );
            return false;
        }

        const std::filesystem::path eventsPath = directory / "events.log";
        if ( std::filesystem::exists( eventsPath, errorCode ) || errorCode )
        {
            std::fprintf( stderr, "Render smoke test output already contains an events log or cannot be inspected.\n" );
            return false;
        }

        state._events = OpenSharedOutput( eventsPath );
        if ( state._events == nullptr )
        {
            std::fprintf( stderr, "Render smoke test could not create its events log.\n" );
            return false;
        }

        state._progress.reset( state._options._targetPresents, state._options._windowCapture );
        state._enabled = true;
        Emit( "START run=%s target=%u backend=%s project=%s scene=%s", state._options._runID.c_str(), state._options._targetPresents,
              state._options._backend == Backend::VULKAN ? "Vulkan" : "OpenGL",
              state._options._project.empty() ? "<default>" : state._options._project.c_str(),
              state._options._scene.empty() ? "<first>" : state._options._scene.c_str() );
        if ( state._options._supervisorGate )
        {
#if defined(IS_WINDOWS_BUILD)
            Emit( "SUPERVISOR_WAIT pid=%u", to_U32( GetCurrentProcessId() ) );
            const SupervisorGateResult gateResult = WaitForSupervisorGate( state._options._outputDirectory, 20'000u );
            if ( gateResult != SupervisorGateResult::RELEASED )
            {
                Fail( "supervisor", gateResult == SupervisorGateResult::TIMED_OUT
                                    ? "Timed out waiting for process ownership verification"
                                    : "Invalid process ownership gate",
                      IO_FAILURE_EXIT );
            }
            Emit( "SUPERVISOR_RELEASED" );
#else
            Fail( "supervisor", "The process ownership gate is available only on Windows", IO_FAILURE_EXIT );
#endif
        }
        return true;
    }

    bool Enabled() noexcept
    {
        return State()._enabled;
    }

    Backend SelectedBackend() noexcept
    {
        return State()._options._backend;
    }

    bool PausedEditorRequested() noexcept
    {
        return Enabled() && State()._options._pausedEditor;
    }

    bool WindowCaptureRequested() noexcept
    {
        return Enabled() && State()._options._windowCapture;
    }

    bool RenderDocCaptureRequested() noexcept
    {
        return Enabled() && State()._options._renderDocCapture;
    }

    bool PipelineTraceRequested() noexcept
    {
        return Enabled() && State()._options._pipelineTrace;
    }

    bool PerfModeRequested() noexcept
    {
        return Enabled() && State()._options._perfMode;
    }

    bool GPUScopeTimingsRequested() noexcept
    {
        return PerfModeRequested() && State()._options._gpuScopeTimings;
    }

    const std::string& OutputDirectory() noexcept
    {
        return State()._options._outputDirectory;
    }

    const std::string& ExpectedGPU() noexcept
    {
        return State()._options._expectedGPU;
    }

    const std::string& Project() noexcept
    {
        return State()._options._project;
    }

    const std::string& Scene() noexcept
    {
        return State()._options._scene;
    }

    MaterialDebugFlag MaterialView() noexcept
    {
        return TypeUtil::StringToMaterialDebugFlag( State()._options._materialView );
    }

    void ReportGPU( const char* uuid, const char* name ) noexcept
    {
        if ( Enabled() )
        {
            Emit( "GPU uuid=%s name=%s", uuid, name );
        }
    }

    void ReportDeviceFaultSupport( const bool enabled, const bool vendorBinary ) noexcept
    {
        if ( Enabled() )
        {
            Emit( "DEVICE_FAULT enabled=%u vendor_binary=%u", enabled ? 1u : 0u, vendorBinary ? 1u : 0u );
        }
    }

    void ReportValidationAdvisory( const char* messageID, const char* message ) noexcept
    {
        if ( Enabled() )
        {
            Emit( "VALIDATION_ADVISORY severity=warning message_id=%s message=%s", messageID, message );
            if ( std::fprintf( stderr, "Vulkan smoke advisory [%s]: %s\n", messageID, message ) < 0 ||
                 std::fflush( stderr ) != 0 )
            {
                FailOutput();
            }
        }
    }

    void ReportVulkanToolEnumerationSkipped() noexcept
    {
        if ( RenderDocCaptureRequested() )
        {
            Emit( "VULKAN_TOOL_ENUMERATION status=skipped reason=renderdoc_capture" );
        }
    }

    void ReportPipelineTrace( const char* program, const I64 guid, const bool graphics, const bool wireframe,
                              const std::span<const PipelineTraceStage> stages ) noexcept
    {
        if ( PipelineTraceRequested() )
        {
            Runtime& state = State();
            std::lock_guard lock( state._eventMutex );
            if ( !WritePipelineTrace( state._events, program, guid, graphics, wireframe, stages ) )
            {
                FailOutput();
            }
        }
    }

    bool WriteDeviceFaultBinary( const std::span<const U8> bytes ) noexcept
    {
        if ( !Enabled() || bytes.empty() )
        {
            return false;
        }

        const std::filesystem::path path = std::filesystem::path{ State()._options._outputDirectory } / "device-fault.bin";
        std::FILE* output = OpenSharedOutput( path );
        if ( output == nullptr )
        {
            return false;
        }
        const bool written = std::fwrite( bytes.data(), 1u, bytes.size(), output ) == bytes.size();
        const bool closed = std::fclose( output ) == 0;
        return written && closed;
    }

    void MarkSceneReady() noexcept
    {
        if ( Enabled() )
        {
            State()._progress.markSceneReady();
            Emit( "SCENE_READY" );
        }
    }

    void BeginFrame() noexcept
    {
        if ( Enabled() )
        {
            State()._progress.beginFrame();
        }
    }

    void MarkSceneFrame() noexcept
    {
        if ( Enabled() )
        {
            State()._progress.markSceneFrame();
        }
    }

    void RecordPresent( const bool mainWindow, const bool successful, const bool recreating ) noexcept
    {
        Runtime& state = State();
        if ( !state._enabled || !state._progress.recordPresent( mainWindow, successful, recreating ) )
        {
            return;
        }

        Emit( "PRESENT count=%u", state._progress.presentCount() );
    }

    bool TryBeginCapture() noexcept
    {
        Runtime& state = State();
        if ( state._enabled && state._progress.tryBeginCapture() )
        {
            Emit( "CAPTURE_START" );
            return true;
        }
        return false;
    }

    void FinishCapture( const bool successful ) noexcept
    {
        if ( Enabled() )
        {
            State()._progress.finishCapture( successful );
            Emit( "CAPTURE_%s", successful ? "COMPLETE" : "FAILED" );
        }
    }

    void FinishWindowCapture( const bool successful ) noexcept
    {
        if ( WindowCaptureRequested() )
        {
            State()._progress.finishWindowCapture( successful );
            Emit( "WINDOW_CAPTURE_%s presents=%u", successful ? "COMPLETE" : "FAILED", State()._progress.presentCount() );
        }
    }

    bool ReadyToStop() noexcept
    {
        return Enabled() && State()._progress.readyToStop();
    }

    U32 PresentCount() noexcept
    {
        return State()._progress.presentCount();
    }

    void RecordAppStop( const bool normalStop ) noexcept
    {
        if ( Enabled() )
        {
            State()._normalStop = normalStop;
            Emit( "APP_STOP normal=%u", normalStop ? 1u : 0u );
        }
    }

    bool Complete( const bool engineClean ) noexcept
    {
        Runtime& state = State();
        if ( !state._enabled )
        {
            return engineClean;
        }

        const bool success = IsSuccessfulResult( engineClean, state._normalStop, state._progress.readyToStop() );
        Emit( "RESULT status=%s presents=%u", success ? "success" : "failed", state._progress.presentCount() );

        const std::filesystem::path resultPath = std::filesystem::path{ state._options._outputDirectory } / "result.txt";
        std::FILE* output = OpenSharedOutput( resultPath );
        if ( output == nullptr )
        {
            std::fprintf( stderr, "Render smoke test could not create its result record.\n" );
            return false;
        }

        const bool written = std::fprintf( output, "run_id=%s\nstatus=%s\nbackend=%s\npresents=%u\ntarget=%u\ncapture=scene.png\nwindow_capture=%s\n",
                                           state._options._runID.c_str(),
                                           success ? "success" : "failed",
                                           state._options._backend == Backend::VULKAN ? "Vulkan" : "OpenGL",
                                           state._progress.presentCount(),
                                           state._options._targetPresents,
                                           state._options._windowCapture ? "editor.png" : "not_requested" ) >= 0;
        const bool closed = std::fclose( output ) == 0;
        const bool eventsClosed = std::fclose( state._events ) == 0;
        state._events = nullptr;
        if ( !written || !closed || !eventsClosed )
        {
            std::fprintf( stderr, "Render smoke test could not finish its result record.\n" );
            return false;
        }
        return success;
    }

    std::FILE* BeginFailure( const char* kind ) noexcept
    {
        Runtime& state = State();
        if ( !state._failureLatch.tryClaim() )
        {
            for ( ;; )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            }
        }

        state._progress.fail();
        const std::filesystem::path failurePath = std::filesystem::path{ state._options._outputDirectory } / "failure.txt";
        std::FILE* output = OpenSharedOutput( failurePath );
        if ( output == nullptr ||
             std::fprintf( output, "kind=%s\nrun_id=%s\npresents=%u\n", kind, state._options._runID.c_str(), state._progress.presentCount() ) < 0 ||
             std::fflush( output ) != 0 )
        {
            FailOutput();
        }
        return output;
    }

    void EndFailure( std::FILE* output, const int exitCode ) noexcept
    {
        if ( std::ferror( output ) != 0 || std::fflush( output ) != 0 || std::fclose( output ) != 0 )
        {
            FailOutput();
        }

#if defined(IS_WINDOWS_BUILD)
        if ( State()._options._debugger )
        {
            DebugBreak( true );
        }
#endif
        std::_Exit( exitCode );
    }

    void Fail( const char* kind, const char* detail, const int exitCode ) noexcept
    {
        std::FILE* output = BeginFailure( kind );
        if ( std::fprintf( output, "detail=%s\n", detail != nullptr ? detail : "" ) < 0 )
        {
            FailOutput();
        }
        EndFailure( output, exitCode );
    }
}
