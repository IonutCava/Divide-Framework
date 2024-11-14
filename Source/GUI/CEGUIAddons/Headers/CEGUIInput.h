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
    bool onKeyDownInternal(Input::KeyEvent& argInOut) override;
    /// Key released: return true if input was consumed
    bool onKeyUpInternal(Input::KeyEvent& argInOut) override;
    /// Joystick axis change: return true if input was consumed
    bool joystickAxisMovedInternal(Input::JoystickEvent& argInOut) override;
    /// Joystick direction change: return true if input was consumed
    bool joystickPovMovedInternal(Input::JoystickEvent& argInOut) override;
    /// Joystick button pressed: return true if input was consumed
    bool joystickButtonPressedInternal(Input::JoystickEvent& argInOut) override;
    /// Joystick button released: return true if input was consumed
    bool joystickButtonReleasedInternal(Input::JoystickEvent& argInOut) override;
    bool joystickBallMovedInternal(Input::JoystickEvent& argInOut) override;
    bool joystickAddRemoveInternal(Input::JoystickEvent& argInOut) override;
    bool joystickRemapInternal(Input::JoystickEvent & argInOut) override;
    /// Mouse moved: return true if input was consumed
    bool mouseMovedInternal(Input::MouseMoveEvent& argInOut) override;
    /// Mouse button pressed: return true if input was consumed
    bool mouseButtonPressedInternal(Input::MouseButtonEvent& argInOut) override;
    /// Mouse button released: return true if input was consumed
    bool mouseButtonReleasedInternal(Input::MouseButtonEvent& argInOut) override;

    bool onTextEventInternal(Input::TextEvent& argInOut) override;

   protected:
    GUI& _parent;
    bool _enabled{ false };
    /// Called on key events: return true if the input was consumed
    bool injectKey(bool pressed, const Input::KeyEvent& evt);
    void repeatKey(const Input::KeyEvent& evt) override;
};

};  // namespace Divide

#endif //DVD_CEGUI_INPUT_H_
