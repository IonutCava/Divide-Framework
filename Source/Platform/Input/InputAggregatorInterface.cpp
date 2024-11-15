

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
    ret._elementIndex = to_U8(charToInt(buttonElements[1].c_str(), 0));

    return ret;
}

InputEvent::InputEvent(DisplayWindow* sourceWindow, const U8 deviceIndex) noexcept
    : _sourceWindow(sourceWindow)
    , _deviceIndex(deviceIndex)
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

bool InputAggregatorInterface::onKeyDown(KeyEvent& argInOut)
{
    if ( processInput() )
    {
        return onKeyDownInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onKeyUp(KeyEvent& argInOut)
{
    if (processInput())
    {
        return onKeyUpInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::mouseMoved(MouseMoveEvent& argInOut)
{
    if (processInput())
    {
        return mouseMovedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::mouseButtonPressed(MouseButtonEvent& argInOut)
{
    if (processInput())
    {
        return mouseButtonPressedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::mouseButtonReleased(MouseButtonEvent& argInOut)
{
    if (processInput())
    {
        return mouseButtonReleasedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::joystickButtonPressed(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return joystickButtonPressedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::joystickButtonReleased(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return joystickButtonReleasedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::joystickAxisMoved(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return joystickAxisMovedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::joystickPovMoved(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return joystickPovMovedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::joystickBallMoved(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return joystickBallMovedInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::joystickAddRemove(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return joystickAddRemoveInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::joystickRemap(JoystickEvent& argInOut)
{
    if (processInput())
    {
        return joystickRemapInternal(argInOut);
    }

    return false;
}

bool InputAggregatorInterface::onTextEvent(TextEvent& argInOut)
{
    if (processInput())
    {
        return onTextEventInternal(argInOut);
    }

    return false;
}

} //namespace Input
} //namespace Divide
