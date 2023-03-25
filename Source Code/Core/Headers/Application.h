/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _CORE_APPLICATION_H_
#define _CORE_APPLICATION_H_

#include "WindowManager.h"
#include "Core/Time/Headers/ApplicationTimer.h"

namespace Divide {

struct Task;
struct Configuration;
struct SizeChangeParams;
struct DisplayManager;

class Kernel;
  
namespace Attorney
{
    class ApplicationKernel;

    class DisplayManagerApplication;
    class DisplayManagerRenderingAPI;
    class DisplayManagerWindowManager;
};

/// Class that provides an interface between our framework and the OS (start/stop, display support, main loop, start/stop/restart, etc)
class Application final : public SDLEventListener
{
    friend class Attorney::ApplicationKernel;

public:
      enum class StepResult : U32
      {
          OK = 0,
          RESTART,
          RESTART_CLEAR_CACHE,
          STOP,
          STOP_CLEAR_CACHE,
          ERROR,
          COUNT
      };

public:
     Application() noexcept;
     ~Application();

    [[nodiscard]] ErrorCode  start(const string& entryPoint, I32 argc, char** argv);
                  void       stop( const StepResult stepResult );
    [[nodiscard]] StepResult step();

    inline void RequestShutdown( bool clearCache ) noexcept;
    inline void CancelShutdown() noexcept;

    inline void RequestRestart( bool clearCache ) noexcept;
    inline void CancelRestart() noexcept;

    [[nodiscard]] inline bool ShutdownRequested() const noexcept;
    [[nodiscard]] inline bool RestartRequested() const noexcept;

    [[nodiscard]] inline WindowManager& windowManager() noexcept;
    [[nodiscard]] inline const WindowManager& windowManager() const noexcept;

    [[nodiscard]] bool onWindowSizeChange(const SizeChangeParams& params) const;
    [[nodiscard]] bool onResolutionChange(const SizeChangeParams& params) const;

    [[nodiscard]] inline ErrorCode errorCode() const noexcept;

    PROPERTY_RW( U8, maxMSAASampleCount, 0u );
    PROPERTY_R(Time::ApplicationTimer, timer);
    PROPERTY_R(std::atomic_bool, mainLoopPaused);
    PROPERTY_R( std::atomic_bool, mainLoopActive );
    PROPERTY_R(std::atomic_bool, freezeRendering);

    [[nodiscard]] Time::ApplicationTimer& timer() noexcept;
    void mainLoopPaused(const bool state) noexcept;
    void mainLoopActive(const bool state) noexcept;
    void freezeRendering(const bool state) noexcept;

private:
    [[nodiscard]] ErrorCode setRenderingAPI( const RenderAPI api );
    [[nodiscard]] bool onSDLEvent( SDL_Event event ) noexcept override;


private:
    WindowManager _windowManager;

    Kernel* _kernel{ nullptr };
    std::atomic_bool _requestShutdown{ false };
    std::atomic_bool _requestRestart{ false };
    std::atomic_bool _stepLoop{ false };

    ErrorCode _errorCode{ ErrorCode::NO_ERR };
    bool _clearCacheOnExit{ false };
};

FWD_DECLARE_MANAGED_CLASS(Application);

struct DisplayManager
{
    struct OutputDisplayProperties
    {
        string _formatName{};
        vec2<U16> _resolution{ 1u };
        U8 _bitsPerPixel{ 8u };
        U8 _maxRefreshRate{ 24u }; //< As returned by SDL_GetPixelFormatName
    };

    friend class Attorney::DisplayManagerWindowManager;
    friend class Attorney::DisplayManagerRenderingAPI;
    friend class Attorney::DisplayManagerApplication;

    static constexpr U8 g_maxDisplayOutputs = 4u;

    using OutputDisplayPropertiesContainer = vector<OutputDisplayProperties>;

    [[nodiscard]] static const OutputDisplayPropertiesContainer& GetDisplayModes( const size_t displayIndex ) noexcept;
    [[nodiscard]] static U8 ActiveDisplayCount() noexcept;
    [[nodiscard]] static U8 MaxMSAASamples() noexcept;

private:
    [[nodiscard]] static void MaxMSAASamples( const U8 maxSampleCount ) noexcept;
    static void SetActiveDisplayCount( const U8 displayCount );
    static void RegisterDisplayMode( const U8 displayIndex, const OutputDisplayProperties& mode );

    static void Reset() noexcept;

private:
    static U8 s_activeDisplayCount;
    static U8 s_maxMSAASAmples;
    static std::array<OutputDisplayPropertiesContainer, g_maxDisplayOutputs> s_supportedDisplayModes;
};

bool operator==( const DisplayManager::OutputDisplayProperties& lhs, const DisplayManager::OutputDisplayProperties& rhs ) noexcept;

namespace Attorney
{
    class ApplicationKernel
    {
        [[nodiscard]] static ErrorCode SetRenderingAPI( Application& app, const RenderAPI api )
        {
            return app.setRenderingAPI(api);
        }

        friend class Kernel;
    };

    class DisplayManagerWindowManager
    {
        static void SetActiveDisplayCount( const U8 displayCount )
        {
            DisplayManager::SetActiveDisplayCount(displayCount);
        }

        static void RegisterDisplayMode( const U8 displayIndex, const DisplayManager::OutputDisplayProperties& mode )
        {
            DisplayManager::RegisterDisplayMode(displayIndex, mode);
        }

        friend class WindowManager;
    };

    class DisplayManagerRenderingAPI
    {
        static void MaxMSAASamples( const U8 maxSampleCount ) noexcept
        {
            DisplayManager::MaxMSAASamples(maxSampleCount);
        }

        friend class GL_API;
        friend class VK_API;
    };

    class DisplayManagerApplication
    {
        static void Reset() noexcept
        {
            DisplayManager::Reset();
        }

        friend class Application;
    };
};
};  // namespace Divide

#endif  //_CORE_APPLICATION_H_

#include "Application.inl"
