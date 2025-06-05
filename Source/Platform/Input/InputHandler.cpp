

#include "Headers/InputHandler.h"
#include "Headers/InputAggregatorInterface.h"

#include "Core/Headers/Application.h"

namespace Divide {
namespace Input {

InputHandler::InputHandler(InputAggregatorInterface& eventListener, Application& app) noexcept
    : SDLEventListener("InputHandler")
    , _app(app)
    , _eventListener(eventListener)
{
    //Note: We only pass input events to a single listeners. Listeners should forward events where needed instead of doing a loop
    //over all, because where and how we pass input events is very context sensitive: does the ProjectManager consume the input or does it pass
    //it along to the scene? Does the editor consume the input or does it pass if along to the gizmo? Should both editor and gizmo consume an input? etc
}

namespace {
    Uint32 GetEventWindowID(const SDL_Event event) noexcept {
        switch (event.type) {
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            case SDL_EVENT_WINDOW_HIT_TEST:
            case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            case SDL_EVENT_WINDOW_OCCLUDED:
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            case SDL_EVENT_WINDOW_DESTROYED:
            case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:       return event.window.windowID;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:                         return event.key.windowID;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:                return event.button.windowID;
            case SDL_EVENT_MOUSE_MOTION:                   return event.motion.windowID;
            case SDL_EVENT_MOUSE_WHEEL:                    return event.wheel.windowID;
            case SDL_EVENT_TEXT_INPUT:                     return event.text.windowID;
            default: break;
        }

        return 0u;
    }
};

bool InputHandler::onSDLEvent(SDL_Event event)
{
    // Find the window that sent the event
    DisplayWindow* eventWindow = _app.windowManager().getWindowByID(GetEventWindowID(event));
    if (eventWindow == nullptr)
    {
        return false;
    }

     switch (event.type)
     {
        case SDL_EVENT_TEXT_EDITING:
        {
            TextEditEvent arg{ eventWindow, 0, event.edit.text, event.edit.start, event.edit.length };
            return _eventListener.onTextEdit(arg);
        }
        case SDL_EVENT_TEXT_INPUT:
        {
            TextInputEvent arg{eventWindow, 0, event.text.text};
            return _eventListener.onTextInput(arg);
        }
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_KEY_DOWN:
        {
            KeyEvent arg(eventWindow, 0);
            arg._key = KeyCodeFromSDLKey(event.key.key);
            arg._state = event.type == SDL_EVENT_KEY_DOWN ? InputState::PRESSED : InputState::RELEASED;
            arg._isRepeat = event.key.repeat;
            arg._sdlScancode = event.key.scancode;
            arg._sdlKey = event.key.key;

            if ((event.key.mod & SDL_KMOD_LSHIFT) != 0)
            {
                arg._modMask |= to_base(KeyModifier::LSHIFT);
            }
            if ((event.key.mod & SDL_KMOD_RSHIFT) != 0)
            {
                arg._modMask |= to_base(KeyModifier::RSHIFT);
            }
            if ((event.key.mod & SDL_KMOD_LCTRL) != 0)
            {
                arg._modMask |= to_base(KeyModifier::LCTRL);
            }
            if ((event.key.mod & SDL_KMOD_RCTRL) != 0)
            {
                arg._modMask |= to_base(KeyModifier::RCTRL);
            }
            if ((event.key.mod & SDL_KMOD_LALT) != 0)
            {
                arg._modMask |= to_base(KeyModifier::LALT);
            }
            if ((event.key.mod & SDL_KMOD_RALT) != 0)
            {
                arg._modMask |= to_base(KeyModifier::RALT);
            }
            if ((event.key.mod & SDL_KMOD_LGUI) != 0)
            {
                arg._modMask |= to_base(KeyModifier::LGUI);
            }
            if ((event.key.mod & SDL_KMOD_RGUI) != 0)
            {
                arg._modMask |= to_base(KeyModifier::RGUI);
            }
            if ((event.key.mod & SDL_KMOD_NUM) != 0)
            {
                arg._modMask |= to_base(KeyModifier::NUM);
            }
            if ((event.key.mod & SDL_KMOD_CAPS) != 0)
            {
                arg._modMask |= to_base(KeyModifier::CAPS);
            }
            if ((event.key.mod & SDL_KMOD_MODE) != 0)
            {
                arg._modMask |= to_base(KeyModifier::MODE);
            }
            
            return _eventListener.onKey(arg);
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            MouseButtonEvent arg(eventWindow, event.button.which);
            arg.pressedState(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputState::PRESSED : InputState::RELEASED);
            switch (event.button.button)
            {
                case SDL_BUTTON_LEFT:   arg.button(MouseButton::MB_Left);    break;
                case SDL_BUTTON_RIGHT:  arg.button(MouseButton::MB_Right);   break;
                case SDL_BUTTON_MIDDLE: arg.button(MouseButton::MB_Middle);  break;
                case SDL_BUTTON_X1:     arg.button(MouseButton::MB_Button3); break;
                case SDL_BUTTON_X2:     arg.button(MouseButton::MB_Button4); break;
                case SDL_BUTTON_X2 + 1: arg.button(MouseButton::MB_Button5); break;
                case SDL_BUTTON_X2 + 2: arg.button(MouseButton::MB_Button6); break;
                case SDL_BUTTON_X2 + 3: arg.button(MouseButton::MB_Button7); break;
                default: break;
            }

            arg.numCliks(to_U8(event.button.clicks));
            auto& state = Attorney::MouseEventInputHandler::state(arg);
            state.X.abs = event.button.x;
            state.Y.abs = event.button.y;

            return _eventListener.onMouseButton(arg);
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            const bool flipped = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED;

            MouseMoveEvent arg(eventWindow, event.wheel.which, true);
            auto& state = Attorney::MouseEventInputHandler::state(arg);
            state.Wheel.xAmount = event.wheel.x * (flipped ? -1 : 1);        /**< The amount scrolled horizontally, positive to the right and negative to the left */
            state.Wheel.yAmount = event.wheel.y * (flipped ? -1 : 1);        /**< The amount scrolled vertically, positive away from the user and negative toward the user */
            state.Wheel.xTicks = event.wheel.integer_x * (flipped ? -1 : 1); /**< The amount scrolled horizontally, accumulated to whole scroll "ticks" (added in 3.2.12) */
            state.Wheel.yTicks = event.wheel.integer_y * (flipped ? -1 : 1); /**< The amount scrolled vertically, accumulated to whole scroll "ticks" (added in 3.2.12) */

            return _eventListener.onMouseMoved(arg);
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            MouseMoveEvent arg(eventWindow, event.motion.which, false);
            auto& state = Attorney::MouseEventInputHandler::state( arg );
            state.X.abs = event.motion.x;     /**< X coordinate, relative to window */
            state.Y.abs = event.motion.y;     /**< Y coordinate, relative to window */
            state.X.rel = event.motion.xrel;  /**< The relative motion in the X direction */
            state.Y.rel = event.motion.yrel;  /**< The relative motion in the Y direction */

            return _eventListener.onMouseMoved(arg);
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        {
            constexpr I16 DEFAULT_DEADZONE = 8000;

            const bool isJoystick = event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION;

            JoystickEvent arg(eventWindow, isJoystick ? event.jaxis.which : event.gaxis.which, isJoystick);
            arg._type = JoystickElementType::AXIS_MOVE;
            arg._elementIndex = isJoystick ? event.jaxis.axis : event.gaxis.axis;
            arg._axisMovement[0] = isJoystick ? event.jaxis.value : event.gaxis.value;
            arg._axisMovement[1] = DEFAULT_DEADZONE;
            return _eventListener.onJoystickAxisMoved(arg);
        };
        case SDL_EVENT_JOYSTICK_BALL_MOTION:
        {
            JoystickEvent arg(eventWindow, event.jball.which, true);
            arg._type = JoystickElementType::BALL_MOVE;
            arg._elementIndex = event.jball.ball;
            arg._ballRelMovement[0] = event.jball.xrel;
            arg._ballRelMovement[1] = event.jball.yrel;

            return _eventListener.onJoystickBallMoved(arg);
        };
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
        {
            JoystickEvent arg(eventWindow, event.jhat.which, true);
            arg._type = JoystickElementType::POV_MOVE;
            arg._elementIndex = event.jhat.hat;
            switch (event.jhat.value)
            {
                case SDL_HAT_CENTERED:  arg._povMask = to_base(JoystickPovDirection::CENTERED); break;
                case SDL_HAT_UP:        arg._povMask = to_base(JoystickPovDirection::UP);       break;
                case SDL_HAT_RIGHT:     arg._povMask = to_base(JoystickPovDirection::RIGHT);    break;
                case SDL_HAT_DOWN:      arg._povMask = to_base(JoystickPovDirection::DOWN);     break;
                case SDL_HAT_LEFT:      arg._povMask = to_base(JoystickPovDirection::LEFT);     break;
                case SDL_HAT_RIGHTUP:   arg._povMask = to_base(JoystickPovDirection::RIGHT) |
                                                       to_base(JoystickPovDirection::UP);       break;
                case SDL_HAT_RIGHTDOWN: arg._povMask = to_base(JoystickPovDirection::RIGHT) |
                                                       to_base(JoystickPovDirection::DOWN);     break;
                case SDL_HAT_LEFTUP:    arg._povMask = to_base(JoystickPovDirection::LEFT)  |
                                                       to_base(JoystickPovDirection::UP);       break;
                case SDL_HAT_LEFTDOWN:  arg._povMask = to_base(JoystickPovDirection::LEFT)  |
                                                       to_base(JoystickPovDirection::DOWN);     break;
                default: break;
            };

            return _eventListener.onJoystickPovMoved(arg);
        };
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
        {
            const bool isJoystick = event.type == SDL_EVENT_JOYSTICK_BUTTON_UP || event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN;

            JoystickEvent arg(eventWindow, isJoystick ? event.jbutton.which : event.gbutton.which, isJoystick);
            arg._type = JoystickElementType::BUTTON_PRESS;
            arg._elementIndex = isJoystick ? event.jbutton.button : event.gbutton.button;
            arg._state = (isJoystick ? event.jbutton.down : event.gbutton.down) ? InputState::PRESSED : InputState::RELEASED;

            return _eventListener.onJoystickButton(arg);
        };
        case SDL_EVENT_GAMEPAD_REMAPPED:
        {
            JoystickEvent arg(eventWindow,  event.gdevice.which, false);
            arg._type = JoystickElementType::JOY_REMAP;

            return _eventListener.onJoystickRemap(arg);
        };
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
        {
            const bool isJoystick = event.type == SDL_EVENT_JOYSTICK_ADDED || event.type == SDL_EVENT_JOYSTICK_REMOVED;
            const bool added = event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_JOYSTICK_ADDED;

            InputEvent arg(eventWindow,
                           isJoystick ? InputDeviceType::JOYSTICK : InputDeviceType::GAMEPAD,
                           added ? InputEventType::DEVICE_ADDED : InputEventType::DEVICE_REMOVED,
                           isJoystick ? event.jdevice.which : event.cdevice.which);

            return _eventListener.onDeviceAddOrRemove(arg);
        };
        case SDL_EVENT_MOUSE_ADDED:
        case SDL_EVENT_MOUSE_REMOVED:
        {
            InputEvent arg(eventWindow,
                           InputDeviceType::MOUSE,
                           event.type == SDL_EVENT_MOUSE_ADDED ? InputEventType::DEVICE_ADDED : InputEventType::DEVICE_REMOVED,
                           event.mdevice.which); 

            return _eventListener.onDeviceAddOrRemove(arg);
        };
        case SDL_EVENT_KEYBOARD_ADDED:
        case SDL_EVENT_KEYBOARD_REMOVED:
        {
            InputEvent arg(eventWindow,
                InputDeviceType::KEYBOARD,
                event.type == SDL_EVENT_KEYBOARD_ADDED ? InputEventType::DEVICE_ADDED : InputEventType::DEVICE_REMOVED,
                event.kdevice.which);

            return _eventListener.onDeviceAddOrRemove(arg);
        };
        default: break;
     }

    return false;
}
};  // namespace Input
};  // namespace Divide
