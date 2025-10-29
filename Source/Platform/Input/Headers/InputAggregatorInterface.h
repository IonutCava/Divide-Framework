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

#include <SDL3/SDL_keycode.h>

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
    constexpr static U32 INVALID_DEVICE_INDEX = U32_MAX;

    explicit InputEvent(DisplayWindow* sourceWindow, Input::InputDeviceType deviceType, Input::InputEventType eventType, U32 deviceIndex) noexcept;

    DisplayWindow* _sourceWindow = nullptr;
    U32 _deviceIndex = INVALID_DEVICE_INDEX;
    Input::InputDeviceType _deviceType = Input::InputDeviceType::COUNT;
    Input::InputEventType _eventType = Input::InputEventType::COUNT;

    bool _simulationPaused = false;
};

struct MouseEvent : public InputEvent
{
    friend class Attorney::MouseEventConsumer;
    friend class Attorney::MouseEventInputHandler;

    explicit MouseEvent( DisplayWindow* sourceWindow, U32 deviceIndex, Input::InputEventType eventType) noexcept;

    [[nodiscard]] inline const MouseState& state() const noexcept { return _state; }

protected:
    MouseState _state;
};

struct MouseButtonEvent final : MouseEvent
{
    friend class Attorney::MouseEventConsumer;

    explicit MouseButtonEvent(DisplayWindow* sourceWindow, U32 deviceIndex) noexcept;

    PROPERTY_RW(InputState, pressedState, InputState::COUNT);
    PROPERTY_RW(MouseButton, button, MouseButton::MB_Left);
    PROPERTY_RW(U8, numCliks, 0u);

    MouseAxis X{};
    MouseAxis Y{};
};

struct MouseMoveEvent final : MouseEvent
{
    friend class Attorney::MouseEventConsumer;

    explicit MouseMoveEvent(DisplayWindow* sourceWindow, U32 deviceIndex, bool wheelEvent) noexcept;

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
    explicit JoystickEvent(DisplayWindow* sourceWindow, U32 deviceIndex, bool isJoystick) noexcept;

    JoystickElementType _type{ JoystickElementType::COUNT };
    InputState _state{ InputState::COUNT };
    U8 _elementIndex{ 0u };
    I32 _max{ 0 };
    union
    {
        U32 _povMask{ 0u };
        I16 _axisMovement[2];
        I16 _ballRelMovement[2];
    };
};

struct TextInputEvent final : InputEvent
{
    explicit TextInputEvent(DisplayWindow* sourceWindow, U32 deviceIndex, const char* utf8Text) noexcept;

    const char* _utf8Text{nullptr};   /**< The input text, UTF-8 encoded */
};

struct TextEditEvent final : InputEvent
{
    explicit TextEditEvent(DisplayWindow* sourceWindow, U32 deviceIndex, const char* utf8Text, I32 startPos, I32 length) noexcept;

    const char* _utf8Text{nullptr};   /**< The input text, UTF-8 encoded */
    I32 _startPos{0}, _length{0};
};

struct KeyEvent final : InputEvent
{
    explicit KeyEvent(DisplayWindow* sourceWindow, U32 deviceIndex) noexcept;

    KeyCode _key{ KeyCode::KC_UNASSIGNED };
    InputState _state{ InputState::COUNT };
    bool _isRepeat{ false };
    U16 _modMask{0u};
    //Native data:
    SDL_Scancode _sdlScancode{};    /**< SDL physical key code - see ::SDL_Scancode for details */
    SDL_Keycode _sdlKey{};          /**< SDL virtual key code - see ::SDL_Keycode for details */
};

struct InputAggregatorInterface
{
    virtual ~InputAggregatorInterface() = default;
    /// The input event may get modified once it is processed.
    /// E.g.: The editor may remap mouse position to match the scene preview window
    ///       or add a key modifier if one is enforced by some setting
    
    /// Keyboard: return true if input was consumed
    virtual bool onKey(KeyEvent& argInOut);

    /// Mouse: return true if input was consumed. 
    virtual bool onMouseMoved(MouseMoveEvent& argInOut);
    virtual bool onMouseButton(MouseButtonEvent& argInOut);

    /// Joystick or Gamepad: return true if input was consumed
    virtual bool onJoystickButton(JoystickEvent& argInOut);
    virtual bool onJoystickAxisMoved(JoystickEvent& argInOut);
    virtual bool onJoystickPovMoved(JoystickEvent& argInOut);
    virtual bool onJoystickBallMoved(JoystickEvent& argInOut);
    virtual bool onJoystickRemap(JoystickEvent& argInOut);
    virtual bool onTextInput(TextInputEvent& argInOut);
    virtual bool onTextEdit(TextEditEvent& argInOut);
    virtual bool onDeviceAddOrRemove(InputEvent& argInOut);

protected:
    /// Keyboard: return true if input was consumed
    virtual bool onKeyInternal(KeyEvent &argInOut) = 0;

    /// Mouse: return true if input was consumed. 
    virtual bool onMouseMovedInternal(MouseMoveEvent &argInOut) = 0;
    virtual bool onMouseButtonInternal(MouseButtonEvent& argInOut) = 0;

    /// Joystick or Gamepad: return true if input was consumed
    virtual bool onJoystickButtonInternal(JoystickEvent &argInOut) = 0;
    virtual bool onJoystickAxisMovedInternal(JoystickEvent &argInOut) = 0;
    virtual bool onJoystickPovMovedInternal(JoystickEvent &argInOut) = 0;
    virtual bool onJoystickBallMovedInternal(JoystickEvent &argInOut) = 0;
    virtual bool onJoystickRemapInternal(JoystickEvent &argInOut) = 0;
    virtual bool onTextInputInternal(TextInputEvent& argInOut) = 0;
    virtual bool onTextEditInternal(TextEditEvent& argInOut) = 0;

    /// Global add or remove device
    virtual bool onDeviceAddOrRemoveInternal(InputEvent& argInOut) = 0;

    PROPERTY_RW(bool, processInput, true);
};

};  // namespace Input
};  // namespace Divide

#endif //DVD_INPUT_AGGREGATOR_INIT_H_
