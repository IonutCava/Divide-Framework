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

namespace Divide
{

class GUI;
class Editor;
class Kernel;
class DisplayWindow;
class ProjectManager;

namespace Input 
{

class InputHandler;

namespace Attorney
{
    class MouseEventConsumer;
    class MouseEventInputHandler;
};

struct InputEvent 
{
    explicit InputEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    DisplayWindow* _sourceWindow = nullptr;
    U8 _deviceIndex = 0;
    bool _simulationPaused = false;
};

struct MouseEvent : public InputEvent
{
    friend class Attorney::MouseEventConsumer;
    friend class Attorney::MouseEventInputHandler;


    explicit MouseEvent( DisplayWindow* sourceWindow, U8 deviceIndex ) noexcept;

    [[nodiscard]] inline const MouseState& state() const noexcept { return _state; }

protected:
    MouseState _state;
};

struct MouseButtonEvent final : MouseEvent
{
    friend class Attorney::MouseEventConsumer;

    explicit MouseButtonEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    PROPERTY_RW(bool, pressed, false);
    PROPERTY_RW(MouseButton, button, MouseButton::MB_Left);
    PROPERTY_RW(U8, numCliks, 0u);
};

struct MouseMoveEvent final : MouseEvent
{
    friend class Attorney::MouseEventConsumer;

    explicit MouseMoveEvent(DisplayWindow* sourceWindow, U8 deviceIndex, bool wheelEvent) noexcept;

    const bool _wheelEvent { false };
};

namespace Attorney
{
    class MouseEventConsumer
    {
        static void setAbsolutePosition(MouseEvent& evt, const int2& absPosition) noexcept
        {
            evt._state.X.abs = absPosition.x;
            evt._state.Y.abs = absPosition.y;
        }

        friend class Divide::GUI;
        friend class Divide::Editor;
        friend class Divide::Kernel;
        friend class Divide::ProjectManager;
    };
    
    class MouseEventInputHandler
    {
        static MouseState& state(MouseEvent& evt) noexcept 
        {
            return evt._state;
        }

        friend class Input::InputHandler;
    };
} //Attorney

struct JoystickEvent final : InputEvent
{
    explicit JoystickEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    JoystickElement _element;
};

struct TextEvent final : InputEvent
{
    explicit TextEvent(DisplayWindow* sourceWindow, U8 deviceIndex, const char* text) noexcept;

    Str<256> _text{};
};

struct KeyEvent final : InputEvent
{
    explicit KeyEvent(DisplayWindow* sourceWindow, U8 deviceIndex) noexcept;

    KeyCode _key{ KeyCode::KC_UNASSIGNED };
    bool _pressed{ false };
    bool _isRepeat{ false };
    U16 _modMask{0u};
    //Native data:
    SDL_Scancode scancode{};    /**< SDL physical key code - see ::SDL_Scancode for details */
    SDL_Keycode sym{};          /**< SDL virtual key code - see ::SDL_Keycode for details */
};

struct InputAggregatorInterface
{
    virtual ~InputAggregatorInterface() = default;
    /// The input event may get modified once it is processed.
    /// E.g.: The editor may remap mouse position to match the scene preview window
    ///       or add a key modifier if one is enforced by some setting
    
    /// Keyboard: return true if input was consumed
    virtual bool onKeyDown(KeyEvent& argInOut);
    virtual bool onKeyUp(KeyEvent& argInOut);

    /// Mouse: return true if input was consumed. 
    virtual bool mouseMoved(MouseMoveEvent& argInOut);
    virtual bool mouseButtonPressed(MouseButtonEvent& argInOut);
    virtual bool mouseButtonReleased(MouseButtonEvent& argInOut);

    /// Joystick or Gamepad: return true if input was consumed
    virtual bool joystickButtonPressed(JoystickEvent& argInOut);
    virtual bool joystickButtonReleased(JoystickEvent& argInOut);
    virtual bool joystickAxisMoved(JoystickEvent& argInOut);
    virtual bool joystickPovMoved(JoystickEvent& argInOut);
    virtual bool joystickBallMoved(JoystickEvent& argInOut);
    virtual bool joystickAddRemove(JoystickEvent& argInOut);
    virtual bool joystickRemap(JoystickEvent& argInOut);
    virtual bool onTextEvent(TextEvent& argInOut);

protected:
    /// Keyboard: return true if input was consumed
    virtual bool onKeyDownInternal(KeyEvent &argInOut) = 0;
    virtual bool onKeyUpInternal(KeyEvent &argInOut) = 0;

    /// Mouse: return true if input was consumed. 
    virtual bool mouseMovedInternal(MouseMoveEvent &argInOut) = 0;
    virtual bool mouseButtonPressedInternal(MouseButtonEvent& argInOut) = 0;
    virtual bool mouseButtonReleasedInternal(MouseButtonEvent& argInOut) = 0;

    /// Joystick or Gamepad: return true if input was consumed
    virtual bool joystickButtonPressedInternal(JoystickEvent &argInOut) = 0;
    virtual bool joystickButtonReleasedInternal(JoystickEvent &argInOut) = 0;
    virtual bool joystickAxisMovedInternal(JoystickEvent &argInOut) = 0;
    virtual bool joystickPovMovedInternal(JoystickEvent &argInOut) = 0;
    virtual bool joystickBallMovedInternal(JoystickEvent &argInOut) = 0;
    virtual bool joystickAddRemoveInternal(JoystickEvent &argInOut) = 0;
    virtual bool joystickRemapInternal(JoystickEvent &argInOut) = 0;
    virtual bool onTextEventInternal(TextEvent& argInOut) = 0;

    PROPERTY_RW(bool, processInput, true);
};

};  // namespace Input
};  // namespace Divide

#endif //DVD_INPUT_AGGREGATOR_INIT_H_
