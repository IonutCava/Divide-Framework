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
#ifndef _CORE_KERNEL_H_
#define _CORE_KERNEL_H_

#include "PlatformContext.h"
#include "Core/Headers/Application.h"
#include "Managers/Headers/FrameListenerManager.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"

namespace Divide {

class Scene;
class Editor;
class PXDevice;
class GFXDevice;
class SFXDevice;
class Application;
class SceneManager;
class ResourceCache;
class DebugInterface;
class PlatformContext;
class SceneRenderState;
class RenderPassManager;

namespace Input {
    class InputInterface;
};

enum class RenderStage : U8;

struct FrameEvent;

/// Application update rate
constexpr U32 TICKS_PER_SECOND = Config::TARGET_FRAME_RATE / Config::TICK_DIVISOR;
constexpr U64 FIXED_UPDATE_RATE_US = Time::SecondsToMicroseconds(1) / TICKS_PER_SECOND;
constexpr U64 MAX_FRAME_TIME_US = Time::MillisecondsToMicroseconds(250);

struct LoopTimingData {
    PROPERTY_R(U64, currentTimeUS, 0ULL);
    PROPERTY_R(U64, previousTimeUS, 0ULL);
    PROPERTY_R(U64, currentTimeDeltaUS, 0ULL);
    PROPERTY_RW(U64, accumulator, 0ULL);

    PROPERTY_RW(U8, updateLoops, 0u);
    PROPERTY_R(bool, freezeLoopTime, false);  //Pause scene processing
    PROPERTY_R(bool, forceRunPhysics, false); //Simulate physics even if the scene is paused
    PROPERTY_R(U64, currentTimeFrozenUS, 0ULL);

    /// Real app delta time between frames. Can't be paused (e.g. used by editor)
    [[nodiscard]] inline U64 appTimeDeltaUS() const noexcept {
        return currentTimeDeltaUS();
    }

    /// Simulated app delta time between frames. Can be paused. (e.g. used by physics)
    [[nodiscard]] inline U64 realTimeDeltaUS() const noexcept {
        return _freezeLoopTime ? 0ULL : appTimeDeltaUS();
    }

    /// Framerate independent delta time between frames. Can be paused. (e.g. used by scene updates)
    [[nodiscard]] inline U64 fixedTimeStep() const noexcept {
        return _freezeLoopTime ? 0ULL : FIXED_UPDATE_RATE_US;
    }

    [[nodiscard]] inline F32 alpha() const noexcept {
        const F32 diff = Time::MicrosecondsToMilliseconds<F32>(_accumulator)        /
                         Time::MicrosecondsToMilliseconds<F32>(FIXED_UPDATE_RATE_US);
        return _freezeLoopTime ? 1.f : CLAMPED_01(diff);
    }

    void update(const U64 elapsedTimeUS) noexcept {
        if (_currentTimeUS == 0u) {
            _currentTimeUS = elapsedTimeUS;
        }

        _updateLoops = 0u;
        _previousTimeUS = _currentTimeUS;
        _currentTimeUS = elapsedTimeUS;
        _currentTimeDeltaUS = _currentTimeUS - _previousTimeUS;

        // In case we break in the debugger
        if (_currentTimeDeltaUS > MAX_FRAME_TIME_US) {
            _currentTimeDeltaUS = MAX_FRAME_TIME_US;
        }

        _accumulator += _currentTimeDeltaUS;
    }

    // return true on change
    bool freezeTime(const bool state) noexcept {
        if (_freezeLoopTime != state) {
            _freezeLoopTime = state;
            _currentTimeFrozenUS = _currentTimeUS;
            return true;
        }
        return false;
    }
};

namespace Attorney {
    class KernelApplication;
    class KernelWindowManager;
    class KernelDebugInterface;
};

namespace Time {
    class ProfileTimer;
};

/// The kernel is the main system that connects all of our various systems: windows, gfx, sfx, input, physics, timing, etc
class Kernel final : public Input::InputAggregatorInterface,
                     NonCopyable
{
    friend class Attorney::KernelApplication;
    friend class Attorney::KernelWindowManager;
    friend class Attorney::KernelDebugInterface;

   public:
    Kernel(I32 argc, char** argv, Application& parentApp);
    ~Kernel();

    /// Our main application rendering loop.
    /// Call input requests, physics calculations, pre-rendering,
    /// rendering,post-rendering etc
    void onLoop();
    /// Called after a swap-buffer call and before a clear-buffer call.
    /// In a GPU-bound application, the CPU will wait on the GPU to finish
    /// processing the frame
    /// so this should keep it busy (old-GLUT heritage)
    void idle(bool fast);
    
    /// Key pressed
    bool onKeyDown(const Input::KeyEvent& key) override;
    /// Key released
    bool onKeyUp(const Input::KeyEvent& key) override;
    /// Joystick axis change
    bool joystickAxisMoved(const Input::JoystickEvent& arg) override;
    /// Joystick direction change
    bool joystickPovMoved(const Input::JoystickEvent& arg) override;
    /// Joystick button pressed
    bool joystickButtonPressed(const Input::JoystickEvent& arg) override;
    /// Joystick button released
    bool joystickButtonReleased(const Input::JoystickEvent& arg) override;
    bool joystickBallMoved(const Input::JoystickEvent& arg) override;
    bool joystickAddRemove(const Input::JoystickEvent& arg) override;
    bool joystickRemap(const Input::JoystickEvent &arg) override;
    /// Mouse moved
    bool mouseMoved(const Input::MouseMoveEvent& arg) override;
    /// Mouse button pressed
    bool mouseButtonPressed(const Input::MouseButtonEvent& arg) override;
    /// Mouse button released
    bool mouseButtonReleased(const Input::MouseButtonEvent& arg) override;

    bool onUTF8(const Input::UTF8Event& arg) override;

    PROPERTY_RW(LoopTimingData, timingData);
    PROPERTY_RW(bool, keepAlive, true);
    POINTER_R(ResourceCache, resourceCache, nullptr);
    POINTER_R(SceneManager, sceneManager, nullptr)
    POINTER_R(RenderPassManager, renderPassManager, nullptr);

    PROPERTY_R_IW(size_t, totalThreadCount, 0u);
    PROPERTY_R(FrameListenerManager, frameListenerMgr);

    FrameListenerManager& frameListenerMgr() noexcept { return _frameListenerMgr; }

    PROPERTY_R(PlatformContext, platformContext);
    PlatformContext& platformContext() noexcept { return _platformContext; }
   private:
    ErrorCode initialize(const string& entryPoint);
    void warmup();
    void shutdown();
    void startSplashScreen();
    void stopSplashScreen();
    bool mainLoopScene(FrameEvent& evt);
    bool presentToScreen(FrameEvent& evt);
    /// Update all engine components that depend on the current screen size. Returns true if the rendering viewport and the window viewport have differnt aspect ratios
    bool onSizeChange(const SizeChangeParams& params);
    vec2<I32> remapMouseCoords(const vec2<I32>& absPositionIn, bool& remappedOut) const noexcept;

   private:
    enum class InputConsumerType : U8 {
        Editor,
        GUI,
        Scene,
        COUNT
    };

    std::array<InputAggregatorInterface*, to_base(InputConsumerType::COUNT)> _inputConsumers{};

    vector<Rect<I32>> _editorViewports{};
    vector<Rect<I32>> _targetViewports{};

    std::atomic_bool _splashScreenUpdating{};
    Task* _splashTask = nullptr;

    Time::ProfileTimer& _appLoopTimer;
    Time::ProfileTimer& _frameTimer;
    Time::ProfileTimer& _appIdleTimer;
    Time::ProfileTimer& _appScenePass;
    Time::ProfileTimer& _physicsUpdateTimer;
    Time::ProfileTimer& _physicsProcessTimer;
    Time::ProfileTimer& _sceneUpdateTimer;
    Time::ProfileTimer& _sceneUpdateLoopTimer;
    Time::ProfileTimer& _cameraMgrTimer;
    Time::ProfileTimer& _flushToScreenTimer;
    Time::ProfileTimer& _preRenderTimer;
    Time::ProfileTimer& _postRenderTimer;
    vector<Time::ProfileTimer*> _renderTimer{};

    // Command line arguments
    I32 _argc;
    char** _argv;

    Rect<I32> _prevViewport = { -1, -1, -1, -1 };
    U8 _prevPlayerCount = 0u;
};

namespace Attorney {
    class KernelApplication {
        static ErrorCode initialize(Kernel* kernel, const string& entryPoint) {
            return kernel->initialize(entryPoint);
        }

        static void shutdown(Kernel* kernel) {
            kernel->shutdown();
        }

        static bool onSizeChange(Kernel* kernel, const SizeChangeParams& params) {
            return kernel->onSizeChange(params);
        }

        static void warmup(Kernel* kernel) {
            kernel->warmup();
        }

        static void onLoop(Kernel* kernel) {
            kernel->onLoop();
        }

        friend class Divide::Application;
        friend class ApplicationTask;
    };

    class KernelDebugInterface {
        static const LoopTimingData& timingData(const Kernel& kernel) noexcept {
            return kernel._timingData;
        }

        friend class Divide::DebugInterface;
    };
};

};  // namespace Divide

#endif  //_CORE_KERNEL_H_