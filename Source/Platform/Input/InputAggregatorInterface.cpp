

#include "Headers/InputAggregatorInterface.h"
#include "Platform/Headers/DisplayWindow.h"
#include "Core/Headers/StringHelper.h"

namespace Divide {
namespace Input {

MouseButton mouseButtonByName(const string& buttonName) {
    if (Util::CompareIgnoreCase("MB_" + buttonName, "MB_Left")) {
        return MouseButton::MB_Left;
    } else if (Util::CompareIgnoreCase("MB_" + buttonName, "MB_Right")) {
        return MouseButton::MB_Right;
    } else if (Util::CompareIgnoreCase("MB_" + buttonName, "MB_Middle")) {
        return MouseButton::MB_Middle;
    } else if (Util::CompareIgnoreCase("MB_" + buttonName, "MB_Button3")) {
        return MouseButton::MB_Button3;
    } else if (Util::CompareIgnoreCase("MB_" + buttonName, "MB_Button4")) {
        return MouseButton::MB_Button4;
    } else if (Util::CompareIgnoreCase("MB_" + buttonName, "MB_Button5")) {
        return MouseButton::MB_Button5;
    } else if (Util::CompareIgnoreCase("MB_" + buttonName, "MB_Button6")) {
        return MouseButton::MB_Button6;
    }

    return MouseButton::MB_Button7;
}

JoystickElement joystickElementByName(const string& elementName) {
    JoystickElement ret = {};

    if (Util::CompareIgnoreCase(elementName, "POV")) {
        ret._type = JoystickElementType::POV_MOVE;
    } else if (Util::CompareIgnoreCase(elementName, "AXIS")) {
        ret._type = JoystickElementType::AXIS_MOVE;
    } else if (Util::CompareIgnoreCase(elementName, "BALL")) {
        ret._type = JoystickElementType::BALL_MOVE;
    }
    
    if (ret._type != JoystickElementType::COUNT) {
        return ret;
    }

    // Else, we have a button
    ret._type = JoystickElementType::BUTTON_PRESS;

    vector<string> buttonElements = Util::Split<vector<string>, string>(elementName.c_str(), '_');
    assert(buttonElements.size() == 2 && "Invalid joystick element name!");
    assert(Util::CompareIgnoreCase(buttonElements[0], "BUTTON"));
    ret._elementIndex = Util::ConvertData<U32, string>(buttonElements[1]);

    return ret;
}

InputEvent::InputEvent(DisplayWindow* sourceWindow, const U8 deviceIndex) noexcept
    : _deviceIndex(deviceIndex),
      _sourceWindow(sourceWindow)
{
}

MouseEvent::MouseEvent( DisplayWindow* sourceWindow, U8 deviceIndex ) noexcept
    : InputEvent(sourceWindow, deviceIndex)
{
}

MouseButtonEvent::MouseButtonEvent(DisplayWindow* sourceWindow, const U8 deviceIndex) noexcept
   : MouseEvent(sourceWindow, deviceIndex)
{
}

MouseMoveEvent::MouseMoveEvent(DisplayWindow* sourceWindow, const U8 deviceIndex, const bool wheelEvent) noexcept
    : MouseEvent( sourceWindow, deviceIndex )
    ,  _wheelEvent(wheelEvent)
{
}

JoystickEvent::JoystickEvent(DisplayWindow* sourceWindow, const U8 deviceIndex) noexcept
    : InputEvent(sourceWindow, deviceIndex)
{
}

KeyEvent::KeyEvent(DisplayWindow* sourceWindow, const U8 deviceIndex) noexcept
    : InputEvent(sourceWindow, deviceIndex)
{
}

TextEvent::TextEvent(DisplayWindow* sourceWindow, const U8 deviceIndex, const char* text) noexcept
    : InputEvent(sourceWindow, deviceIndex)
    , _text(text)
{
}

}; //namespace Input
}; //namespace Divide