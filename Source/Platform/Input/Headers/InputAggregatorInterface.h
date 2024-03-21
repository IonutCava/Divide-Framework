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
#ifndef DVD_INPUT_AGGREGATOR_INIT_H_
#define DVD_INPUT_AGGREGATOR_INIT_H_

#include "Input.h"

#include <SDL2/SDL_keycode.h>

namespace Divide {

class Kernel;
class DisplayWindow;
namespace Input 
{

class InputHandler;

namespace Attorney {
    class MouseEventKernel;
    class MouseEventInputHandler;
};

struct InputEvent 
{
    explicit InputEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    U8 _deviceIndex = 0;
    DisplayWindow* _sourceWindow = nullptr;
};

struct MouseEvent : public InputEvent
{
    friend class Attorney::MouseEventKernel;
    friend class Attorney::MouseEventInputHandler;


    explicit MouseEvent( DisplayWindow* sourceWindow, U8 deviceIndex ) noexcept;

    [[nodiscard]] inline const MouseState& state() const noexcept { return _state; }

    PROPERTY_RW(bool, inScenePreviewRect, false);

protected:
    MouseState _state;
};

struct MouseButtonEvent final : MouseEvent
{
    friend class Attorney::MouseEventKernel;

    explicit MouseButtonEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    PROPERTY_RW(bool, pressed, false);
    PROPERTY_RW(MouseButton, button, MouseButton::MB_Left);
    PROPERTY_RW(U8, numCliks, 0u);
};

struct MouseMoveEvent final : MouseEvent
{
    friend class Attorney::MouseEventKernel;

    explicit MouseMoveEvent(DisplayWindow* sourceWindow, U8 deviceIndex, bool wheelEvent) noexcept;

    const bool _wheelEvent { false };
};
namespace Attorney {
    class MouseEventKernel {
        private:
            static MouseState& state(MouseEvent& evt) noexcept 
            {
                return evt._state;
            }

            friend class Divide::Kernel;
    };
    
    class MouseEventInputHandler {
        private:
            static MouseState& state(MouseEvent& evt) noexcept 
            {
                return evt._state;
            }

            friend class Input::InputHandler;
    };
} //Attorney

struct JoystickEvent final : InputEvent {
    explicit JoystickEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    JoystickElement _element;
};

struct TextEvent final : InputEvent {
    explicit TextEvent(DisplayWindow* sourceWindow, U8 deviceIndex, const char* text) noexcept;

    Str<256> _text{};
};

struct KeyEvent final : InputEvent {

    explicit KeyEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    KeyCode _key{ KeyCode::KC_UNASSIGNED };
    bool _pressed{ false };
    bool _isRepeat{ false };
    U16 _modMask{0u};
    //Native data:
    SDL_Scancode scancode{};    /**< SDL physical key code - see ::SDL_Scancode for details */
    SDL_Keycode sym{};          /**< SDL virtual key code - see ::SDL_Keycode for details */
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

    virtual bool onTextEvent(const TextEvent& arg) = 0;
};

};  // namespace Input
};  // namespace Divide

#endif //DVD_INPUT_AGGREGATOR_INIT_H_
