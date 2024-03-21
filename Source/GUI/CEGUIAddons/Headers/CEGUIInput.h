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
#ifndef DVD_CEGUI_INPUT_H_
#define DVD_CEGUI_INPUT_H_

#include "Platform/Input/Headers/AutoKeyRepeat.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"

namespace Divide {

class GUI;
struct Configuration;

/// This class defines AutoRepeatKey::repeatKey(...) as CEGUI key inputs
class CEGUIInput final : public Input::InputAggregatorInterface,
                         public Input::AutoRepeatKey {
   public:
    explicit CEGUIInput(GUI& parent) noexcept;
    void init(const Configuration& config) noexcept;

    /// Key pressed: return true if input was consumed
    bool onKeyDown(const Input::KeyEvent& key) override;
    /// Key released: return true if input was consumed
    bool onKeyUp(const Input::KeyEvent& key) override;
    /// Joystick axis change: return true if input was consumed
    bool joystickAxisMoved(const Input::JoystickEvent& arg) override;
    /// Joystick direction change: return true if input was consumed
    bool joystickPovMoved(const Input::JoystickEvent& arg) override;
    /// Joystick button pressed: return true if input was consumed
    bool joystickButtonPressed(const Input::JoystickEvent& arg) override;
    /// Joystick button released: return true if input was consumed
    bool joystickButtonReleased(const Input::JoystickEvent& arg) override;
    bool joystickBallMoved(const Input::JoystickEvent& arg) override;
    bool joystickAddRemove(const Input::JoystickEvent& arg) override;
    bool joystickRemap(const Input::JoystickEvent &arg) override;
    /// Mouse moved: return true if input was consumed
    bool mouseMoved(const Input::MouseMoveEvent& arg) override;
    /// Mouse button pressed: return true if input was consumed
    bool mouseButtonPressed(const Input::MouseButtonEvent& arg) override;
    /// Mouse button released: return true if input was consumed
    bool mouseButtonReleased(const Input::MouseButtonEvent& arg) override;

    bool onTextEvent(const Input::TextEvent& arg) override;

   protected:
    GUI& _parent;
    bool _enabled{ false };
    /// Called on key events: return true if the input was consumed
    bool injectKey(bool pressed, const Input::KeyEvent& evt);
    void repeatKey(const Input::KeyEvent& evt) override;
};

};  // namespace Divide

#endif //DVD_CEGUI_INPUT_H_
