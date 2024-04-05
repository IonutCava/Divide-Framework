

#include "Headers/InputHandler.h"
#include "Headers/InputAggregatorInterface.h"

#include "Core/Headers/Kernel.h"

namespace Divide {
namespace Input {

InputHandler::InputHandler(InputAggregatorInterface& eventListener, Application& app) noexcept
    : _app(app),
      _eventListener(eventListener)
{
    //Note: We only pass input events to a single listeners. Listeners should forward events where needed instead of doing a loop
    //over all, because where and how we pass input events is very context sensitive: does the ProjectManager consume the input or does it pass
    //it along to the scene? Does the editor consume the input or does it pass if along to the gizmo? Should both editor and gizmo consume an input? etc
}

namespace {
    Uint32 GetEventWindowID(const SDL_Event event) noexcept {
        switch (event.type) {
            case SDL_KEYDOWN :
            case SDL_KEYUP   :
                return event.key.windowID;

            case SDL_MOUSEBUTTONDOWN :
            case SDL_MOUSEBUTTONUP   :
                return event.button.windowID;

            case SDL_MOUSEMOTION :
                return event.motion.windowID;

            case SDL_MOUSEWHEEL :
                return event.wheel.windowID;

            case SDL_WINDOWEVENT :
                return event.window.windowID;

            case SDL_TEXTINPUT :
                return event.text.windowID;
            default: break;
        }

        return 0;
    }
};

bool InputHandler::onSDLEvent(SDL_Event event) {
    // Find the window that sent the event
    DisplayWindow* eventWindow = _app.windowManager().getWindowByID(GetEventWindowID(event));
    if (eventWindow == nullptr) {
        return false;
    }

     switch (event.type) {
        case SDL_TEXTEDITING:
        case SDL_TEXTINPUT: {
            const TextEvent arg{eventWindow, 0, event.text.text};
            _eventListener.onTextEvent(arg);
            return true;
        };

        case SDL_KEYUP:
        case SDL_KEYDOWN: {
            KeyEvent arg(eventWindow, 0);
            arg._key = KeyCodeFromSDLKey(event.key.keysym.sym);
            arg._pressed = event.type == SDL_KEYDOWN;
            arg._isRepeat = event.key.repeat;
            arg.scancode = event.key.keysym.scancode;
            arg.sym = event.key.keysym.sym;

            if ((event.key.keysym.mod & KMOD_LSHIFT) != 0) {
                arg._modMask |= to_base(KeyModifier::LSHIFT);
            }
            if ((event.key.keysym.mod & KMOD_RSHIFT) != 0) {
                arg._modMask |= to_base(KeyModifier::RSHIFT);
            }
            if ((event.key.keysym.mod & KMOD_LCTRL) != 0) {
                arg._modMask |= to_base(KeyModifier::LCTRL);
            }
            if ((event.key.keysym.mod & KMOD_RCTRL) != 0) {
                arg._modMask |= to_base(KeyModifier::RCTRL);
            }
            if ((event.key.keysym.mod & KMOD_LALT) != 0) {
                arg._modMask |= to_base(KeyModifier::LALT);
            }
            if ((event.key.keysym.mod & KMOD_RALT) != 0) {
                arg._modMask |= to_base(KeyModifier::RALT);
            }
            if ((event.key.keysym.mod & KMOD_LGUI) != 0) {
                arg._modMask |= to_base(KeyModifier::LGUI);
            }
            if ((event.key.keysym.mod & KMOD_RGUI) != 0) {
                arg._modMask |= to_base(KeyModifier::RGUI);
            }
            if ((event.key.keysym.mod & KMOD_NUM) != 0) {
                arg._modMask |= to_base(KeyModifier::NUM);
            }
            if ((event.key.keysym.mod & KMOD_CAPS) != 0) {
                arg._modMask |= to_base(KeyModifier::CAPS);
            }
            if ((event.key.keysym.mod & KMOD_MODE) != 0) {
                arg._modMask |= to_base(KeyModifier::MODE);
            }
            if (arg._pressed) {
                _eventListener.onKeyDown(arg);
            } else {
                _eventListener.onKeyUp(arg);
            }
            return true;
        };
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            MouseButtonEvent arg(eventWindow, to_U8(event.button.which));
            arg.pressed(event.type == SDL_MOUSEBUTTONDOWN);
            switch (event.button.button) {
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
            };

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
        };
        case SDL_MOUSEWHEEL:
        case SDL_MOUSEMOTION:
        {
            MouseMoveEvent arg(eventWindow, to_U8(event.motion.which), event.type == SDL_MOUSEWHEEL);
            auto& state = Attorney::MouseEventInputHandler::state( arg );
            state.X.abs = event.motion.x;
            state.X.rel = event.motion.xrel;
            state.Y.abs = event.motion.y;
            state.Y.rel = event.motion.yrel;
            state.HWheel = event.wheel.x;
            state.VWheel = event.wheel.y;

            _eventListener.mouseMoved(arg);
            return true;
        };
        case SDL_CONTROLLERAXISMOTION:
        case SDL_JOYAXISMOTION:
        {
            JoystickData jData = {};
            jData._gamePad = event.type == SDL_CONTROLLERAXISMOTION;
            jData._data = jData._gamePad ? event.caxis.value : event.jaxis.value;

            JoystickElement element = {};
            element._type = JoystickElementType::AXIS_MOVE;
            element._data = jData;
            element._elementIndex = jData._gamePad ? event.caxis.axis : event.jaxis.axis;

            JoystickEvent arg(eventWindow, to_U8(jData._gamePad ? event.caxis.which : event.jaxis.which));
            arg._element = element;

            _eventListener.joystickAxisMoved(arg);
            return true;
        };
        case SDL_JOYBALLMOTION:
        {
            JoystickData jData = {};
            jData._smallData[0] = event.jball.xrel;
            jData._smallData[1] = event.jball.yrel;

            JoystickElement element = {};
            element._type = JoystickElementType::BALL_MOVE;
            element._data = jData;
            element._elementIndex = event.jball.ball;

            JoystickEvent arg(eventWindow, to_U8(event.jball.which));
            arg._element = element;

            _eventListener.joystickBallMoved(arg);
            return true;
        };
        case SDL_JOYHATMOTION:
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
            element._elementIndex = event.jhat.hat;

            JoystickEvent arg(eventWindow, to_U8(event.jhat.which));
            arg._element = element;

            _eventListener.joystickPovMoved(arg);
            return true;
        };
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
        {
            JoystickData jData = {};
            jData._gamePad = event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP;

            const Uint8 state = jData._gamePad ? event.cbutton.state : event.jbutton.state;
            jData._data = state == SDL_PRESSED ? to_U32(InputState::PRESSED) : to_U32(InputState::RELEASED);

            JoystickElement element = {};
            element._type = JoystickElementType::BUTTON_PRESS;
            element._data = jData;
            element._elementIndex = jData._gamePad ? event.cbutton.button : event.jbutton.button;

            JoystickEvent arg(eventWindow, to_U8(jData._gamePad  ? event.cbutton.which : event.jbutton.which));
            arg._element = element;

            if (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONDOWN) {
                _eventListener.joystickButtonPressed(arg);
            } else {
                _eventListener.joystickButtonReleased(arg);
            }
            return true;
        };
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
        {
            JoystickData jData = {};
            jData._gamePad = event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED;
            jData._data = (jData._gamePad ? event.type == SDL_CONTROLLERDEVICEADDED : event.type == SDL_JOYDEVICEADDED) ? 1 : 0;

            JoystickElement element = {};
            element._type = JoystickElementType::JOY_ADD_REMOVE;
            element._data = jData;

            JoystickEvent arg(eventWindow, to_U8(jData._gamePad ? event.cdevice.which : event.jdevice.which));
            arg._element = element;

            _eventListener.joystickAddRemove(arg);
            return true;
        };
        
       
        case SDL_CONTROLLERDEVICEREMAPPED:
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
