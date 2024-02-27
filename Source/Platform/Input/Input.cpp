

#include "Headers/Input.h"

#include <SDL2/SDL_keycode.h>

namespace Divide {
namespace Input {
    struct KeyMapEntry {
        SDL_Keycode _sdlKCode;
        KeyCode _kCode;
    };

    constexpr KeyMapEntry KeyCodeSDLMap[] = {
        {SDLK_ESCAPE, KeyCode::KC_ESCAPE},
        {SDLK_1, KeyCode::KC_1},
        {SDLK_2, KeyCode::KC_2},
        {SDLK_3, KeyCode::KC_3},
        {SDLK_4, KeyCode::KC_4},
        {SDLK_5, KeyCode::KC_5},
        {SDLK_6, KeyCode::KC_6},
        {SDLK_7, KeyCode::KC_7},
        {SDLK_8, KeyCode::KC_8},
        {SDLK_9, KeyCode::KC_9},
        {SDLK_0, KeyCode::KC_0},
        {SDLK_MINUS, KeyCode::KC_MINUS},
        {SDLK_EQUALS, KeyCode::KC_EQUALS},
        {SDLK_BACKSPACE, KeyCode::KC_BACK},
        {SDLK_TAB, KeyCode::KC_TAB},
        {SDLK_q, KeyCode::KC_Q},
        {SDLK_w, KeyCode::KC_W},
        {SDLK_e, KeyCode::KC_E},
        {SDLK_r, KeyCode::KC_R},
        {SDLK_t, KeyCode::KC_T},
        {SDLK_y, KeyCode::KC_Y},
        {SDLK_u, KeyCode::KC_U},
        {SDLK_i, KeyCode::KC_I},
        {SDLK_o, KeyCode::KC_O},
        {SDLK_p, KeyCode::KC_P},
        {SDLK_RETURN, KeyCode::KC_RETURN},
        {SDLK_LCTRL, KeyCode::KC_LCONTROL},
        {SDLK_RCTRL, KeyCode::KC_RCONTROL},
        {SDLK_a, KeyCode::KC_A},
        {SDLK_s, KeyCode::KC_S},
        {SDLK_d, KeyCode::KC_D},
        {SDLK_f, KeyCode::KC_F},
        {SDLK_g, KeyCode::KC_G},
        {SDLK_h, KeyCode::KC_H},
        {SDLK_j, KeyCode::KC_J},
        {SDLK_k, KeyCode::KC_K},
        {SDLK_l, KeyCode::KC_L},
        {SDLK_SEMICOLON, KeyCode::KC_SEMICOLON},
        {SDLK_COLON, KeyCode::KC_COLON},
        {SDLK_QUOTE, KeyCode::KC_APOSTROPHE},
        {SDLK_BACKQUOTE, KeyCode::KC_GRAVE},
        {SDLK_LSHIFT, KeyCode::KC_LSHIFT},
        {SDLK_BACKSLASH, KeyCode::KC_BACKSLASH},
        {SDLK_SLASH, KeyCode::KC_SLASH},
        {SDLK_z, KeyCode::KC_Z},
        {SDLK_x, KeyCode::KC_X},
        {SDLK_c, KeyCode::KC_C},
        {SDLK_v, KeyCode::KC_V},
        {SDLK_b, KeyCode::KC_B},
        {SDLK_n, KeyCode::KC_N},
        {SDLK_m, KeyCode::KC_M},
        {SDLK_COMMA, KeyCode::KC_COMMA},
        {SDLK_PERIOD, KeyCode::KC_PERIOD},
        {SDLK_RSHIFT, KeyCode::KC_RSHIFT},
        {SDLK_KP_MULTIPLY, KeyCode::KC_MULTIPLY},
        {SDLK_LALT, KeyCode::KC_LMENU},
        {SDLK_SPACE, KeyCode::KC_SPACE},
        {SDLK_CAPSLOCK, KeyCode::KC_CAPITAL},
        {SDLK_F1, KeyCode::KC_F1},
        {SDLK_F2, KeyCode::KC_F2},
        {SDLK_F3, KeyCode::KC_F3},
        {SDLK_F4, KeyCode::KC_F4},
        {SDLK_F5, KeyCode::KC_F5},
        {SDLK_F6, KeyCode::KC_F6},
        {SDLK_F7, KeyCode::KC_F7},
        {SDLK_F8, KeyCode::KC_F8},
        {SDLK_F9, KeyCode::KC_F9},
        {SDLK_F10, KeyCode::KC_F10},
        {SDLK_NUMLOCKCLEAR, KeyCode::KC_NUMLOCK},
        {SDLK_SCROLLLOCK, KeyCode::KC_SCROLL},
        {SDLK_KP_7, KeyCode::KC_NUMPAD7},
        {SDLK_KP_8, KeyCode::KC_NUMPAD8},
        {SDLK_KP_9, KeyCode::KC_NUMPAD9},
        {SDLK_KP_MINUS, KeyCode::KC_SUBTRACT},
        {SDLK_KP_4, KeyCode::KC_NUMPAD4},
        {SDLK_KP_5, KeyCode::KC_NUMPAD5},
        {SDLK_KP_6, KeyCode::KC_NUMPAD6},
        {SDLK_KP_PLUS, KeyCode::KC_ADD},
        {SDLK_KP_1, KeyCode::KC_NUMPAD1},
        {SDLK_KP_2, KeyCode::KC_NUMPAD2},
        {SDLK_KP_3, KeyCode::KC_NUMPAD3},
        {SDLK_KP_0, KeyCode::KC_NUMPAD0},
        {SDLK_KP_PERIOD, KeyCode::KC_DECIMAL},
        {SDLK_PLUS, KeyCode::KC_ADD},
        {SDLK_MINUS, KeyCode::KC_MINUS},
        {SDLK_KP_ENTER, KeyCode::KC_NUMPADENTER},
        {SDLK_F11, KeyCode::KC_F11},
        {SDLK_F12, KeyCode::KC_F12},
        {SDLK_F13, KeyCode::KC_F13},
        {SDLK_F14, KeyCode::KC_F14},
        {SDLK_F15, KeyCode::KC_F15},
        {SDLK_KP_EQUALS, KeyCode::KC_NUMPADEQUALS},
        {SDLK_KP_DIVIDE, KeyCode::KC_DIVIDE},
        {SDLK_SYSREQ, KeyCode::KC_SYSRQ},
        {SDLK_RALT, KeyCode::KC_RMENU},
        {SDLK_HOME, KeyCode::KC_HOME},
        {SDLK_UP, KeyCode::KC_UP},
        {SDLK_PAGEUP, KeyCode::KC_PGUP},
        {SDLK_LEFT, KeyCode::KC_LEFT},
        {SDLK_RIGHT, KeyCode::KC_RIGHT},
        {SDLK_END, KeyCode::KC_END},
        {SDLK_DOWN, KeyCode::KC_DOWN},
        {SDLK_PAGEDOWN, KeyCode::KC_PGDOWN},
        {SDLK_INSERT, KeyCode::KC_INSERT},
        {SDLK_DELETE, KeyCode::KC_DELETE},
        {SDLK_LGUI, KeyCode::KC_LWIN},
        {SDLK_RGUI, KeyCode::KC_RWIN},
        {SDLK_PRINTSCREEN, KeyCode::KC_PRINTSCREEN},
        {SDLK_APPLICATION, KeyCode::KC_APPS},
    };

    SDL_Keycode SDLKeyCodeFromKey(const KeyCode code) noexcept {
        for (const KeyMapEntry& entry : KeyCodeSDLMap) {
            if (entry._kCode == code) {
                return entry._sdlKCode;
            }
        }

        return SDLK_UNKNOWN;
    }

    KeyCode KeyCodeFromSDLKey(const SDL_Keycode code) noexcept {
        for (const KeyMapEntry& entry : KeyCodeSDLMap) {
            if (entry._sdlKCode == code) {
                return entry._kCode;
            }
        }

        return KeyCode::KC_UNASSIGNED;
    }

    KeyCode KeyCodeByName(const char* keyName) noexcept {
        return KeyCodeFromSDLKey(SDL_GetKeyFromName(keyName));
    }

    InputState GetKeyState([[maybe_unused]] const U8 deviceIndex, const KeyCode key) noexcept {
        const Uint8 *state = SDL_GetKeyboardState(nullptr);

        return state[SDL_GetScancodeFromKey(SDLKeyCodeFromKey(key))] ? InputState::PRESSED : InputState::RELEASED;
    }

    InputState GetMouseButtonState([[maybe_unused]] const U8 deviceIndex, const MouseButton button) noexcept {
        I32 x = -1, y = -1;
        const U32 state = SDL_GetMouseState(&x, &y);

        U32 sdlButton = 0u;
        switch (button) {
        case MouseButton::MB_Left:
            sdlButton = SDL_BUTTON_LEFT;
            break;
        case MouseButton::MB_Right:
            sdlButton = SDL_BUTTON_RIGHT;
            break;
        case MouseButton::MB_Middle:
            sdlButton = SDL_BUTTON_MIDDLE;
            break;
        case MouseButton::MB_Button3:
            sdlButton = SDL_BUTTON_X1;
            break;
        case MouseButton::MB_Button4:
            sdlButton = SDL_BUTTON_X2;
            break;
        default:
            return InputState::RELEASED;
        };

        return (state & SDL_BUTTON(sdlButton)) != 0 ? InputState::PRESSED : InputState::RELEASED;
    }

    InputState GetJoystickElementState([[maybe_unused]] Joystick deviceIndex, [[maybe_unused]] JoystickElement element) noexcept {
        assert(false && "implement me!");

        return InputState::RELEASED;
    }
}; //namespace Input
}; //namespace Divide