#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#include "Platform/Headers/PlatformDataTypes.h"

#include <atomic>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace Divide
{
    enum class MaterialDebugFlag : U8;
}

namespace Divide::RenderSmokeTest
{
    enum class ParseResult : U8
    {
        DISABLED,
        ENABLED,
        ERROR
    };

    enum class Backend : U8
    {
        VULKAN,
        OPENGL
    };

    [[nodiscard]] constexpr bool InitialSimulationPaused( const bool smokeEnabled, const bool normalPaused,
                                                          const bool pausedEditorRequested ) noexcept
    {
        return smokeEnabled ? pausedEditorRequested : normalPaused;
    }

    struct Options
    {
        std::string _outputDirectory{};
        std::string _runID{};
        std::string _expectedGPU{};
        std::string _project{};
        std::string _scene{};
        std::string _materialView{};
        U32 _targetPresents{ 300u };
        Backend _backend{ Backend::VULKAN };
        bool _debugger{ false };
        bool _windowCapture{ false };
        bool _supervisorGate{ false };
        bool _renderDocCapture{ false };
        bool _pausedEditor{ false };
        bool _pipelineTrace{ false };
    };

    struct PipelineTraceStage
    {
        const char* _name{ nullptr };
        U64 _module{ 0u };
        U32 _stage{ 0u };
        U32 _generation{ 0u };
    };

    [[nodiscard]] bool WritePipelineTrace( std::FILE* output, const char* program, I64 guid, bool graphics, bool wireframe,
                                           std::span<const PipelineTraceStage> stages ) noexcept;

    [[nodiscard]] constexpr bool IsValidContentName( const std::string_view name ) noexcept
    {
        if ( name.empty() || name.size() > 64u || name.front() == ' ' || name.back() == ' ' )
        {
            return false;
        }
        for ( const char character : name )
        {
            if ( !( character >= 'a' && character <= 'z' ) &&
                 !( character >= 'A' && character <= 'Z' ) &&
                 !( character >= '0' && character <= '9' ) &&
                 character != '-' && character != '_' && character != ' ' )
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] ParseResult ParseOptions( std::span<const std::string_view> arguments, Options& optionsOut, std::string& errorOut );

    [[nodiscard]] constexpr I32 HexDigit( const char character ) noexcept
    {
        if ( character >= '0' && character <= '9' ) { return character - '0'; }
        if ( character >= 'a' && character <= 'f' ) { return character - 'a' + 10; }
        if ( character >= 'A' && character <= 'F' ) { return character - 'A' + 10; }
        return -1;
    }

    [[nodiscard]] constexpr bool IsValidGPUUUID( const std::string_view uuid ) noexcept
    {
        if ( uuid.size() != 32u ) { return false; }
        for ( const char character : uuid )
        {
            if ( HexDigit( character ) < 0 ) { return false; }
        }
        return true;
    }

    [[nodiscard]] constexpr bool MatchesGPUUUID( const std::span<const U8> bytes, const std::string_view uuid ) noexcept
    {
        if ( bytes.size() != 16u || !IsValidGPUUUID( uuid ) ) { return false; }
        for ( size_t i = 0u; i < bytes.size(); ++i )
        {
            if ( to_U32( HexDigit( uuid[i * 2u] ) ) != ( to_U32( bytes[i] ) >> 4u ) ||
                 to_U32( HexDigit( uuid[i * 2u + 1u] ) ) != ( to_U32( bytes[i] ) & 15u ) )
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool RGBA8ReadbackSize( const U32 width, const U32 height, size_t& sizeOut ) noexcept
    {
        if ( width == 0u || height == 0u || width > U16_MAX || height > U16_MAX )
        {
            return false;
        }
        const size_t rowSize = to_size( width ) * 4u;
        if ( to_size( height ) > std::numeric_limits<size_t>::max() / rowSize )
        {
            return false;
        }
        sizeOut = rowSize * to_size( height );
        return true;
    }

    enum class SupervisorGateResult : U8
    {
        RELEASED,
        TIMED_OUT,
        INVALID
    };

    [[nodiscard]] SupervisorGateResult WaitForSupervisorGate( const std::string& outputDirectory, U32 timeoutMilliseconds ) noexcept;

    [[nodiscard]] constexpr bool IsAllowedValidationAdvisory( const bool warning,
                                                              const bool error,
                                                              const std::string_view messageID,
                                                              const std::string_view message ) noexcept
    {
        return warning && !error &&
               messageID == "BestPractices-specialuse-extension" &&
               message.starts_with( "vkCreateDevice(): Attempting to enable extension VK_EXT_custom_border_color," );
    }

    [[nodiscard]] constexpr bool IsFatalValidationSeverity( const bool warning,
                                                           const bool error,
                                                           const std::string_view messageID = {},
                                                           const std::string_view message = {} ) noexcept
    {
        return error || ( warning && !IsAllowedValidationAdvisory( warning, error, messageID, message ) );
    }

    [[nodiscard]] constexpr bool IsQueueSubmitDeviceLossResult( const bool submitActive,
                                                               const std::string_view messageID,
                                                               const std::string_view message ) noexcept
    {
        return submitActive &&
               messageID == "BestPractices-Error-Result" &&
               message.starts_with( "vkQueueSubmit(): Returned error VK_ERROR_DEVICE_LOST." );
    }

    [[nodiscard]] constexpr bool IsEligiblePresent( const bool sceneReady,
                                                    const bool sceneFrame,
                                                    const bool mainWindow,
                                                    const bool successful,
                                                    const bool recreating ) noexcept
    {
        return sceneReady && sceneFrame && mainWindow && successful && !recreating;
    }

    [[nodiscard]] constexpr bool IsSuccessfulResult( const bool engineClean, const bool normalStop, const bool ready ) noexcept
    {
        return engineClean && normalStop && ready;
    }

    class FirstFailureLatch
    {
    public:
        [[nodiscard]] bool tryClaim() noexcept
        {
            bool expected = false;
            return _claimed.compare_exchange_strong( expected, true, std::memory_order_acq_rel );
        }

    private:
        std::atomic_bool _claimed{ false };
    };

    class Progress
    {
    public:
        explicit Progress( U32 targetPresents, bool requireWindowCapture = false ) noexcept;

        void reset( U32 targetPresents, bool requireWindowCapture = false ) noexcept;
        void markSceneReady() noexcept;
        void beginFrame() noexcept;
        void markSceneFrame() noexcept;
        [[nodiscard]] bool recordPresent( bool mainWindow, bool successful, bool recreating ) noexcept;
        [[nodiscard]] bool tryBeginCapture() noexcept;
        void finishCapture( bool successful ) noexcept;
        [[nodiscard]] bool awaitingWindowCapture() const noexcept;
        void finishWindowCapture( bool successful ) noexcept;
        void fail() noexcept;

        [[nodiscard]] U32 presentCount() const noexcept;
        [[nodiscard]] bool readyToStop() const noexcept;

    private:
        U32 _targetPresents;
        std::atomic<U32> _presentCount{ 0u };
        std::atomic_bool _sceneReady{ false };
        std::atomic_bool _sceneFrame{ false };
        std::atomic_bool _captureStarted{ false };
        std::atomic_bool _captureComplete{ false };
        std::atomic_bool _windowCaptureComplete{ false };
        std::atomic_bool _failed{ false };
        bool _requireWindowCapture;
    };

    [[nodiscard]] bool Configure( I32 argc, char** argv );
    [[nodiscard]] bool Enabled() noexcept;
    [[nodiscard]] Backend SelectedBackend() noexcept;
    [[nodiscard]] bool PausedEditorRequested() noexcept;
    [[nodiscard]] bool WindowCaptureRequested() noexcept;
    [[nodiscard]] bool RenderDocCaptureRequested() noexcept;
    [[nodiscard]] bool PipelineTraceRequested() noexcept;
    [[nodiscard]] const std::string& OutputDirectory() noexcept;
    [[nodiscard]] const std::string& ExpectedGPU() noexcept;
    [[nodiscard]] const std::string& Project() noexcept;
    [[nodiscard]] const std::string& Scene() noexcept;
    [[nodiscard]] MaterialDebugFlag MaterialView() noexcept;
    void ReportGPU( const char* uuid, const char* name ) noexcept;
    void ReportDeviceFaultSupport( bool enabled, bool vendorBinary ) noexcept;
    void ReportValidationAdvisory( const char* messageID, const char* message ) noexcept;
    void ReportVulkanToolEnumerationSkipped() noexcept;
    void ReportPipelineTrace( const char* program, I64 guid, bool graphics, bool wireframe,
                              std::span<const PipelineTraceStage> stages ) noexcept;
    [[nodiscard]] bool WriteDeviceFaultBinary( std::span<const U8> bytes ) noexcept;
    void MarkSceneReady() noexcept;
    void BeginFrame() noexcept;
    void MarkSceneFrame() noexcept;
    void RecordPresent( bool mainWindow, bool successful, bool recreating ) noexcept;
    [[nodiscard]] bool TryBeginCapture() noexcept;
    void FinishCapture( bool successful ) noexcept;
    void FinishWindowCapture( bool successful ) noexcept;
    [[nodiscard]] bool ReadyToStop() noexcept;
    [[nodiscard]] U32 PresentCount() noexcept;
    void RecordAppStop( bool normalStop ) noexcept;
    [[nodiscard]] bool Complete( bool engineClean ) noexcept;

    [[nodiscard]] std::FILE* BeginFailure( const char* kind ) noexcept;
    [[noreturn]] void EndFailure( std::FILE* output, int exitCode ) noexcept;
    [[noreturn]] void Fail( const char* kind, const char* detail, int exitCode ) noexcept;

    constexpr int VALIDATION_FAILURE_EXIT = 86;
    constexpr int DEVICE_LOST_EXIT = 87;
    constexpr int CAPTURE_FAILURE_EXIT = 88;
    constexpr int IO_FAILURE_EXIT = 89;
}
