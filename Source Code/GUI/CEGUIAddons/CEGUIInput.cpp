#include "stdafx.h"

#include "Headers/CEGUIInput.h"

#include <CEGUI/CEGUI.h>

#include "GUI/Headers/GUI.h"

namespace Divide {

CEGUIInput::CEGUIInput(GUI& parent)
    : _parent(parent)
{
}

// return true if the input was consumed
bool CEGUIInput::injectKey(const bool pressed, const Input::KeyEvent& inKey) {
    
    bool consumed = false;
    if (pressed) {
        if (_parent.getCEGUIContext().injectKeyDown((CEGUI::Key::Scan)inKey._key)) {
            if (inKey._text != nullptr) {
                _parent.getCEGUIContext().injectChar((CEGUI::Key::Scan)inKey._text[0]);
            }
            begin(inKey);
            consumed = true;
        }
    } else {
        if (_parent.getCEGUIContext().injectKeyUp((CEGUI::Key::Scan)inKey._key)) {
            end(inKey);
            consumed = true;
        }
    }
    return consumed;
}

void CEGUIInput::repeatKey(I32 inKey, const U32 Char) {
    // Now remember the key is still down, so we need to simulate the key being
    // released, and then repressed immediatly
    _parent.getCEGUIContext().injectKeyUp((CEGUI::Key::Scan)inKey);    // Key UP
    _parent.getCEGUIContext().injectKeyDown((CEGUI::Key::Scan)inKey);  // Key Down
    _parent.getCEGUIContext().injectChar(Char);  // What that key means
}

// Return true if input was consumed
bool CEGUIInput::onKeyDown(const Input::KeyEvent& key) {
    return injectKey(true, key);
}

// Return true if input was consumed
bool CEGUIInput::onKeyUp(const Input::KeyEvent& key) {
    return injectKey(false, key);
}

// Return true if input was consumed
bool CEGUIInput::mouseMoved(const Input::MouseMoveEvent& arg) {
    if (arg.wheelEvent()) {
        return _parent.getCEGUIContext().injectMouseWheelChange(to_F32(arg.WheelV()));
    }

    const vec2<F32> mousePos(to_F32(arg.X().abs), to_F32(arg.Y().abs));
    return _parent.getCEGUIContext().injectMousePosition(mousePos.x, mousePos.y);
}

// Return true if input was consumed
bool CEGUIInput::mouseButtonPressed(const Input::MouseButtonEvent& arg) {
    bool consumed = false;
    switch (arg.button()) {
        case Input::MouseButton::MB_Left: {
            consumed = _parent.getCEGUIContext().injectMouseButtonDown(CEGUI::LeftButton);
        } break;
        case Input::MouseButton::MB_Middle: {
            consumed = _parent.getCEGUIContext().injectMouseButtonDown(CEGUI::MiddleButton);
        } break;
        case Input::MouseButton::MB_Right: {
            consumed = _parent.getCEGUIContext().injectMouseButtonDown(CEGUI::RightButton);
        } break;
        case Input::MouseButton::MB_Button3: {
            consumed = _parent.getCEGUIContext().injectMouseButtonDown(CEGUI::X1Button);
        } break;
        case Input::MouseButton::MB_Button4: {
            consumed = _parent.getCEGUIContext().injectMouseButtonDown(CEGUI::X2Button);
        } break;

        case Input::MouseButton::MB_Button5:
        case Input::MouseButton::MB_Button6:
        case Input::MouseButton::MB_Button7:
        case Input::MouseButton::COUNT: break;
    };

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::mouseButtonReleased(const Input::MouseButtonEvent& arg) {
    bool consumed = false;
    switch (arg.button()) {
        case Input::MouseButton::MB_Left: {
            consumed = _parent.getCEGUIContext().injectMouseButtonUp(CEGUI::LeftButton);
        } break;
        case Input::MouseButton::MB_Middle: {
            consumed = _parent.getCEGUIContext().injectMouseButtonUp(CEGUI::MiddleButton);
        } break;
        case Input::MouseButton::MB_Right: {
            consumed = _parent.getCEGUIContext().injectMouseButtonUp(CEGUI::RightButton);
        } break;
        case Input::MouseButton::MB_Button3: {
            consumed = _parent.getCEGUIContext().injectMouseButtonUp(CEGUI::X1Button);
        } break;
        case Input::MouseButton::MB_Button4: {
            consumed = _parent.getCEGUIContext().injectMouseButtonUp(CEGUI::X2Button);
        } break;
        default: break;
    };

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickAxisMoved(const Input::JoystickEvent& arg) {
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickPovMoved(const Input::JoystickEvent& arg) {
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickButtonPressed(const Input::JoystickEvent& arg) {
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickButtonReleased(const Input::JoystickEvent& arg) {
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickBallMoved(const Input::JoystickEvent& arg) {
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickAddRemove(const Input::JoystickEvent& arg) {
    const bool consumed = false;

    return consumed;
}

bool CEGUIInput::joystickRemap(const Input::JoystickEvent &arg) {
    const bool consumed = false;

    return consumed;
}

bool CEGUIInput::onUTF8([[maybe_unused]] const Input::UTF8Event& arg) {
    return false;
}
};