

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
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                return event.key.windowID;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                return event.button.windowID;

            case SDL_EVENT_MOUSE_MOTION:
                return event.motion.windowID;

            case SDL_EVENT_MOUSE_WHEEL:
                return event.wheel.windowID;

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
            case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
                return event.window.windowID;

            case SDL_EVENT_TEXT_INPUT:
                return event.text.windowID;
            default: break;
        }

        return 0;
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
            _eventListener.onTextEdit(arg);
            return true;
        }
        case SDL_EVENT_TEXT_INPUT:
        {
            TextInputEvent arg{eventWindow, 0, event.text.text};
            _eventListener.onTextInput(arg);
            return true;
        }

        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_KEY_DOWN:
        {
            KeyEvent arg(eventWindow, 0);
            arg._key = KeyCodeFromSDLKey(event.key.key);
            arg._pressed = event.type == SDL_EVENT_KEY_DOWN;
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
            if (arg._pressed)
            {
                _eventListener.onKeyDown(arg);
            }
            else
            {
                _eventListener.onKeyUp(arg);
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            MouseButtonEvent arg(eventWindow, to_U8(event.button.which));
            arg.pressed(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            switch (event.button.button)
            {
                case SDL_BUTTON_LEFT:
                    arg.button(MouseButton::MB_Left);
                    break;
                case SDL_BUTTON_RIGHT:
                    arg.button(MouseButton::MB_Right);
                    break;
                case SDL_BUTTON_MIDDLE:
                    arg.button(MouseButton::MB_Middle);
                    break;
                case SDL_BUTTON_X1:
                    arg.button(MouseButton::MB_Button3);
                    break;
                case SDL_BUTTON_X2:
                    arg.button(MouseButton::MB_Button4);
                    break;
                case 6:
                    arg.button(MouseButton::MB_Button5);
                    break;
                case 7:
                    arg.button(MouseButton::MB_Button6);
                    break;
                case 8:
                    arg.button(MouseButton::MB_Button7);
                    break;
                default: break;
            }

            arg.numCliks(to_U8(event.button.clicks));
            auto& state = Attorney::MouseEventInputHandler::state(arg);
            state.X.abs = event.button.x;
            state.Y.abs = event.button.y;

            if (arg.pressed())
            {
                _eventListener.mouseButtonPressed(arg);
            }
            else
            {
                _eventListener.mouseButtonReleased(arg);
            }
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_MOUSE_MOTION:
        {
            MouseMoveEvent arg(eventWindow, to_U8(event.motion.which), event.type == SDL_EVENT_MOUSE_WHEEL);
            auto& state = Attorney::MouseEventInputHandler::state( arg );
            state.X.abs = event.motion.x;
            state.X.rel = event.motion.xrel;
            state.Y.abs = event.motion.y;
            state.Y.rel = event.motion.yrel;
            state.HWheel = event.wheel.x;
            state.VWheel = event.wheel.y;

            _eventListener.mouseMoved(arg);
            return true;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        {
            JoystickData jData = {};
            jData._gamePad = event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
            jData._dataSigned = jData._gamePad ? event.gaxis.value : event.jaxis.value;

            JoystickElement element = {};
            element._type = JoystickElementType::AXIS_MOVE;
            element._data = jData;
            element._elementIndex = (jData._gamePad ? event.gaxis.axis : event.jaxis.axis);

            JoystickEvent arg(eventWindow, to_U8(jData._gamePad ? event.gaxis.which : event.jaxis.which));
            arg._element = element;

            _eventListener.joystickAxisMoved(arg);
            return true;
        };
        case SDL_EVENT_JOYSTICK_BALL_MOTION:
        {
            JoystickData jData = {};
            jData._smallDataSigned[0] = event.jball.xrel;
            jData._smallDataSigned[1] = event.jball.yrel;

            JoystickElement element = {};
            element._type = JoystickElementType::BALL_MOVE;
            element._data = jData;
            element._elementIndex = (event.jball.ball);

            JoystickEvent arg(eventWindow, to_U8(event.jball.which));
            arg._element = element;

            _eventListener.joystickBallMoved(arg);
            return true;
        };
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
        {
            // POV
            U32 PovMask = 0;
            switch (event.jhat.value) {
                case SDL_HAT_CENTERED:
                    PovMask = to_base(JoystickPovDirection::CENTERED);
                    break;
                case SDL_HAT_UP:
                    PovMask = to_base(JoystickPovDirection::UP);
                    break;
                case SDL_HAT_RIGHT:
                    PovMask = to_base(JoystickPovDirection::RIGHT);
                    break;
                case SDL_HAT_DOWN:
                    PovMask = to_base(JoystickPovDirection::DOWN);
                    break;
                case SDL_HAT_LEFT:
                    PovMask = to_base(JoystickPovDirection::LEFT);
                    break;
                case SDL_HAT_RIGHTUP:
                    PovMask = to_base(JoystickPovDirection::RIGHT) |
                              to_base(JoystickPovDirection::UP);
                    break;
                case SDL_HAT_RIGHTDOWN:
                    PovMask = to_base(JoystickPovDirection::RIGHT) |
                              to_base(JoystickPovDirection::DOWN);
                    break;
                case SDL_HAT_LEFTUP:
                    PovMask = to_base(JoystickPovDirection::LEFT) |
                              to_base(JoystickPovDirection::UP);
                    break;
                case SDL_HAT_LEFTDOWN:
                    PovMask = to_base(JoystickPovDirection::LEFT) |
                              to_base(JoystickPovDirection::DOWN);
                    break;
                default: break;
            };

            JoystickData jData = {};
            jData._data = PovMask;

            JoystickElement element = {};
            element._type = JoystickElementType::POV_MOVE;
            element._data = jData;
            element._elementIndex = (event.jhat.hat);

            JoystickEvent arg(eventWindow, to_U8(event.jhat.which));
            arg._element = element;

            _eventListener.joystickPovMoved(arg);
            return true;
        };
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
        {
            JoystickData jData = {};
            jData._gamePad = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP;

            const bool down = jData._gamePad ? event.gbutton.down : event.jbutton.down;
            jData._data = down ? to_U32(InputState::PRESSED) : to_U32(InputState::RELEASED);

            JoystickElement element = {};
            element._type = JoystickElementType::BUTTON_PRESS;
            element._data = jData;
            element._elementIndex = jData._gamePad ? event.gbutton.button : event.jbutton.button;

            JoystickEvent arg(eventWindow, to_U8(jData._gamePad  ? event.gbutton.which : event.jbutton.which));
            arg._element = element;

            if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                _eventListener.joystickButtonPressed(arg);
            } else {
                _eventListener.joystickButtonReleased(arg);
            }
            return true;
        };
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
        {
            JoystickData jData = {};
            jData._gamePad = event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED;
            jData._data = (jData._gamePad ? event.type == SDL_EVENT_GAMEPAD_ADDED : event.type == SDL_EVENT_JOYSTICK_ADDED) ? 1 : 0;

            JoystickElement element = {};
            element._type = JoystickElementType::JOY_ADD_REMOVE;
            element._data = jData;

            JoystickEvent arg(eventWindow, to_U8(jData._gamePad ? event.cdevice.which : event.jdevice.which));
            arg._element = element;

            _eventListener.joystickAddRemove(arg);
            return true;
        };
        
       
        case SDL_EVENT_GAMEPAD_REMAPPED:
        {
            JoystickElement element = {};
            element._type = JoystickElementType::JOY_REMAP;

            JoystickEvent arg(eventWindow, to_U8(event.jdevice.which));
            arg._element = element;

            _eventListener.joystickRemap(arg);
            return true;
        };
        default: break;
     }

    return false;
}
};  // namespace Input
};  // namespace Divide
