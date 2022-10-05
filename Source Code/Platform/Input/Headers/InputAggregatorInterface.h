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
#ifndef _INPUT_AGGREGATOR_INIT_H_
#define _INPUT_AGGREGATOR_INIT_H_

#include "Input.h"

#include <sdl/include/SDL_keycode.h>

namespace Divide {

class DisplayWindow;
namespace Input {

namespace Attorney {
    class MouseEventKernel;
};

struct InputEvent {
    explicit InputEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    U8 _deviceIndex = 0;
    DisplayWindow* _sourceWindow = nullptr;
};

struct MouseButtonEvent final : InputEvent {
    friend class Attorney::MouseEventKernel;

    explicit MouseButtonEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    PROPERTY_RW(bool, pressed, false);
    PROPERTY_RW(MouseButton, button, MouseButton::MB_Left);
    PROPERTY_RW(U8, numCliks, 0u);
    PROPERTY_RW(vec2<I32>, absPosition, vec2<I32>(-1));

protected:
    bool _remapped = false;
};

struct MouseMoveEvent final : InputEvent {
    friend class Attorney::MouseEventKernel;

    explicit MouseMoveEvent(DisplayWindow* sourceWindow, U8 deviceIndex, MouseState stateIn, bool wheelEvent) noexcept;

    [[nodiscard]] MouseAxis X() const noexcept;
    [[nodiscard]] MouseAxis Y() const noexcept;

    [[nodiscard]] I32 WheelV() const noexcept;
    [[nodiscard]] I32 WheelH() const noexcept;

    [[nodiscard]] vec2<I32> relativePos() const noexcept;
    [[nodiscard]] vec2<I32> absolutePos() const noexcept;
    [[nodiscard]] bool remapped() const noexcept;

    [[nodiscard]] const MouseState& state() const noexcept;
    
    [[nodiscard]] bool wheelEvent() const noexcept;

protected:
    void absolutePos(vec2<I32> newPos) noexcept;

 private:
    MouseState _stateIn;
    bool _remapped = false;
    const bool _wheelEvent = false;
};
namespace Attorney {
    class MouseEventKernel {
        private:
            static void absolutePos(MouseMoveEvent& evt, const vec2<I32> newPos) noexcept {
                evt.absolutePos(newPos);
                evt._remapped = true;
            }

            static void absolutePos(MouseButtonEvent& evt, const vec2<I32> pos) noexcept {
                evt.absPosition(pos);
                evt._remapped = true;
            }
        friend class Kernel;
    };
} //Attorney

struct JoystickEvent final : InputEvent {
    explicit JoystickEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    JoystickElement _element;
};

struct UTF8Event final : InputEvent {
    explicit UTF8Event(DisplayWindow* sourceWindow, U8 deviceIndex, const char* text) noexcept;

    const char* _text = nullptr;
};

struct KeyEvent final : InputEvent {

    explicit KeyEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    KeyCode _key{ KeyCode::KC_UNASSIGNED };
    bool _pressed{ false };
    bool _isRepeat{ false };
    U16 _modMask{0u};
    //Native data:
    SDL_Scancode scancode;      /**< SDL physical key code - see ::SDL_Scancode for details */
    SDL_Keycode sym;            /**< SDL virtual key code - see ::SDL_Keycode for details */
};

class InputAggregatorInterface {
   public:
    virtual ~InputAggregatorInterface() = default;
    /// Keyboard: return true if input was consumed
    virtual bool onKeyDown(const KeyEvent &arg) = 0;
    virtual bool onKeyUp(const KeyEvent &arg) = 0;
    /// Mouse: return true if input was consumed
    virtual bool mouseMoved(const MouseMoveEvent &arg) = 0;
    virtual bool mouseButtonPressed(const MouseButtonEvent& arg) = 0;
    virtual bool mouseButtonReleased(const MouseButtonEvent& arg) = 0;

    /// Joystick or Gamepad: return true if input was consumed
    virtual bool joystickButtonPressed(const JoystickEvent &arg) = 0;
    virtual bool joystickButtonReleased(const JoystickEvent &arg) = 0;
    virtual bool joystickAxisMoved(const JoystickEvent &arg) = 0;
    virtual bool joystickPovMoved(const JoystickEvent &arg) = 0;
    virtual bool joystickBallMoved(const JoystickEvent &arg) = 0;
    virtual bool joystickAddRemove(const JoystickEvent &arg) = 0;
    virtual bool joystickRemap(const JoystickEvent &arg) = 0;

    virtual bool onUTF8(const UTF8Event& arg) = 0;
};

};  // namespace Input
};  // namespace Divide

#endif
