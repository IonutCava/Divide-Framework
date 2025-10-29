

#include "Headers/InputAggregatorInterface.h"
#include "Platform/Headers/DisplayWindow.h"

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

namespace
{
    I32 charToInt(const char* val, const I32 defaultValue)
    {
        try
        {
            return std::stoi(val);
        }
        catch (const std::invalid_argument&)
        {
        }
        catch (const std::out_of_range&)
        {
        }

        return defaultValue;
    }
}


std::pair<JoystickElementType, U8> joystickElementByName(const string& elementName) {
    std::pair<JoystickElementType, U8> ret = {};

         if (Util::CompareIgnoreCase(elementName, "POV"))  return { JoystickElementType::POV_MOVE,  0u };
    else if (Util::CompareIgnoreCase(elementName, "AXIS")) return { JoystickElementType::AXIS_MOVE, 0u };
    else if (Util::CompareIgnoreCase(elementName, "BALL")) return { JoystickElementType::BALL_MOVE, 0u };
    

    // Else, we have a button
    ret.first = JoystickElementType::BUTTON_PRESS;

    vector<string> buttonElements = Util::Split<vector<string>, string>(elementName.c_str(), '_');
    assert(buttonElements.size() == 2 && "Invalid joystick element name!");
    assert(Util::CompareIgnoreCase(buttonElements[0], "BUTTON"));
    ret.second = to_U8(charToInt(buttonElements[1].c_str(), 0));

    return ret;
}

InputEvent::InputEvent(DisplayWindow* sourceWindow, const Input::InputDeviceType deviceType, const Input::InputEventType eventType, const U32 deviceIndex) noexcept
    : _sourceWindow(sourceWindow)
    , _deviceIndex(deviceIndex)
    , _deviceType(deviceType)
    , _eventType(eventType)
{
}

MouseEvent::MouseEvent( DisplayWindow* sourceWindow, const U32 deviceIndex, const Input::InputEventType eventType) noexcept
    : InputEvent(sourceWindow, Input::InputDeviceType::MOUSE, eventType, deviceIndex)
{
}

MouseButtonEvent::MouseButtonEvent(DisplayWindow* sourceWindow, const U32 deviceIndex) noexcept
   : MouseEvent(sourceWindow, deviceIndex, Input::InputEventType::DEVICE_INPUT )
{
}

MouseMoveEvent::MouseMoveEvent(DisplayWindow* sourceWindow, const U32 deviceIndex, const bool wheelEvent) noexcept
    : MouseEvent( sourceWindow, deviceIndex, Input::InputEventType::DEVICE_INPUT )
    ,  _wheelEvent(wheelEvent)
{
}

JoystickEvent::JoystickEvent(DisplayWindow* sourceWindow, const U32 deviceIndex, const bool isJoystick) noexcept
    : InputEvent(sourceWindow, isJoystick ? Input::InputDeviceType::JOYSTICK : Input::InputDeviceType::GAMEPAD, Input::InputEventType::DEVICE_INPUT, deviceIndex)
{
}

KeyEvent::KeyEvent(DisplayWindow* sourceWindow, const U32 deviceIndex) noexcept
    : InputEvent(sourceWindow, Input::InputDeviceType::KEYBOARD, Input::InputEventType::DEVICE_INPUT, deviceIndex)
{
}

TextInputEvent::TextInputEvent(DisplayWindow* sourceWindow, const U32 deviceIndex, const char* utf8Text) noexcept
    : InputEvent(sourceWindow, Input::InputDeviceType::KEYBOARD, Input::InputEventType::DEVICE_INPUT, deviceIndex)
    , _utf8Text(utf8Text)
{
}

TextEditEvent::TextEditEvent(DisplayWindow* sourceWindow, U32 deviceIndex, const char* utf8Text, I32 startPos, I32 length) noexcept
    : InputEvent(sourceWindow, Input::InputDeviceType::KEYBOARD, Input::InputEventType::DEVICE_INPUT, deviceIndex)
    , _utf8Text(utf8Text)
    , _startPos(startPos)
    , _length(length)
{
}

bool InputAggregatorInterface::onKey(KeyEvent& argInOut)
{
    if ( processInput() )
    {
        return onKeyInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onMouseMoved(MouseMoveEvent& argInOut)
{
    if (processInput())
    {
        return onMouseMovedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onMouseButton(MouseButtonEvent& argInOut)
{
    if (processInput())
    {
        return onMouseButtonInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onJoystickButton(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return onJoystickButtonInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onJoystickAxisMoved(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return onJoystickAxisMovedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onJoystickPovMoved(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return onJoystickPovMovedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onJoystickBallMoved(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return onJoystickBallMovedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onJoystickRemap(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return onJoystickRemapInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onTextInput(TextInputEvent& argInOut)
{
    if (processInput())
    {
        return onTextInputInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onTextEdit(TextEditEvent& argInOut)
{
    if (processInput())
    {
        return onTextEditInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onDeviceAddOrRemove(InputEvent& argInOut)
{
    if (processInput())
    {
        return onDeviceAddOrRemoveInternal(argInOut);
    }

    return false;
}

} //namespace Input
} //namespace Divide
