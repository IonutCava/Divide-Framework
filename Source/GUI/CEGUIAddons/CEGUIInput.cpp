

#include "Headers/CEGUIInput.h"

#include "Core/Headers/Configuration.h"
#include "GUI/Headers/GUI.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{
namespace
{
    CEGUI::Key::Scan DivideKeyToCeGuiKey(const Input::KeyCode key) noexcept
    {
        using namespace CEGUI;
        switch (key)
        {
            case Input::KeyCode::KC_BACK:         return Key::Backspace;
            case Input::KeyCode::KC_TAB:          return Key::Tab;
            case Input::KeyCode::KC_RETURN:       return Key::Return;
            case Input::KeyCode::KC_PAUSE:        return Key::Pause;
            case Input::KeyCode::KC_ESCAPE:       return Key::Escape;
            case Input::KeyCode::KC_SPACE:        return Key::Space;
            case Input::KeyCode::KC_COMMA:        return Key::Comma;
            case Input::KeyCode::KC_MINUS:        return Key::Minus;
            case Input::KeyCode::KC_PERIOD:       return Key::Period;
            case Input::KeyCode::KC_SLASH:        return Key::Slash;
            case Input::KeyCode::KC_0:            return Key::Zero;
            case Input::KeyCode::KC_1:            return Key::One;
            case Input::KeyCode::KC_2:            return Key::Two;
            case Input::KeyCode::KC_3:            return Key::Three;
            case Input::KeyCode::KC_4:            return Key::Four;
            case Input::KeyCode::KC_5:            return Key::Five;
            case Input::KeyCode::KC_6:            return Key::Six;
            case Input::KeyCode::KC_7:            return Key::Seven;
            case Input::KeyCode::KC_8:            return Key::Eight;
            case Input::KeyCode::KC_9:            return Key::Nine;
            case Input::KeyCode::KC_COLON:        return Key::Colon;
            case Input::KeyCode::KC_SEMICOLON:    return Key::Semicolon;
            case Input::KeyCode::KC_EQUALS:       return Key::Equals;
            case Input::KeyCode::KC_LBRACKET:     return Key::LeftBracket;
            case Input::KeyCode::KC_BACKSLASH:    return Key::Backslash;
            case Input::KeyCode::KC_RBRACKET:     return Key::RightBracket;
            case Input::KeyCode::KC_A:            return Key::A;
            case Input::KeyCode::KC_B:            return Key::B;
            case Input::KeyCode::KC_C:            return Key::C;
            case Input::KeyCode::KC_D:            return Key::D;
            case Input::KeyCode::KC_E:            return Key::E;
            case Input::KeyCode::KC_F:            return Key::F;
            case Input::KeyCode::KC_G:            return Key::G;
            case Input::KeyCode::KC_H:            return Key::H;
            case Input::KeyCode::KC_I:            return Key::I;
            case Input::KeyCode::KC_J:            return Key::J;
            case Input::KeyCode::KC_K:            return Key::K;
            case Input::KeyCode::KC_L:            return Key::L;
            case Input::KeyCode::KC_M:            return Key::M;
            case Input::KeyCode::KC_N:            return Key::N;
            case Input::KeyCode::KC_O:            return Key::O;
            case Input::KeyCode::KC_P:            return Key::P;
            case Input::KeyCode::KC_Q:            return Key::Q;
            case Input::KeyCode::KC_R:            return Key::R;
            case Input::KeyCode::KC_S:            return Key::S;
            case Input::KeyCode::KC_T:            return Key::T;
            case Input::KeyCode::KC_U:            return Key::U;
            case Input::KeyCode::KC_V:            return Key::V;
            case Input::KeyCode::KC_W:            return Key::W;
            case Input::KeyCode::KC_X:            return Key::X;
            case Input::KeyCode::KC_Y:            return Key::Y;
            case Input::KeyCode::KC_Z:            return Key::Z;
            case Input::KeyCode::KC_DELETE:       return Key::Delete;
            case Input::KeyCode::KC_NUMPAD0:      return Key::Numpad0;
            case Input::KeyCode::KC_NUMPAD1:      return Key::Numpad1;
            case Input::KeyCode::KC_NUMPAD2:      return Key::Numpad2;
            case Input::KeyCode::KC_NUMPAD3:      return Key::Numpad3;
            case Input::KeyCode::KC_NUMPAD4:      return Key::Numpad4;
            case Input::KeyCode::KC_NUMPAD5:      return Key::Numpad5;
            case Input::KeyCode::KC_NUMPAD6:      return Key::Numpad6;
            case Input::KeyCode::KC_NUMPAD7:      return Key::Numpad7;
            case Input::KeyCode::KC_NUMPAD8:      return Key::Numpad8;
            case Input::KeyCode::KC_NUMPAD9:      return Key::Numpad9;
            case Input::KeyCode::KC_DECIMAL:      return Key::Decimal;
            case Input::KeyCode::KC_DIVIDE:       return Key::Divide;
            case Input::KeyCode::KC_MULTIPLY:     return Key::Multiply;
            case Input::KeyCode::KC_SUBTRACT:     return Key::Subtract;
            case Input::KeyCode::KC_ADD:          return Key::Add;
            case Input::KeyCode::KC_NUMPADENTER:  return Key::NumpadEnter;
            case Input::KeyCode::KC_NUMPADEQUALS: return Key::NumpadEquals;
            case Input::KeyCode::KC_UP:           return Key::ArrowUp;
            case Input::KeyCode::KC_DOWN:         return Key::ArrowDown;
            case Input::KeyCode::KC_RIGHT:        return Key::ArrowRight;
            case Input::KeyCode::KC_LEFT:         return Key::ArrowLeft;
            case Input::KeyCode::KC_INSERT:       return Key::Insert;
            case Input::KeyCode::KC_HOME:         return Key::Home;
            case Input::KeyCode::KC_END:          return Key::End;
            case Input::KeyCode::KC_PGUP:         return Key::PageUp;
            case Input::KeyCode::KC_PGDOWN:       return Key::PageDown;
            case Input::KeyCode::KC_F1:           return Key::F1;
            case Input::KeyCode::KC_F2:           return Key::F2;
            case Input::KeyCode::KC_F3:           return Key::F3;
            case Input::KeyCode::KC_F4:           return Key::F4;
            case Input::KeyCode::KC_F5:           return Key::F5;
            case Input::KeyCode::KC_F6:           return Key::F6;
            case Input::KeyCode::KC_F7:           return Key::F7;
            case Input::KeyCode::KC_F8:           return Key::F8;
            case Input::KeyCode::KC_F9:           return Key::F9;
            case Input::KeyCode::KC_F10:          return Key::F10;
            case Input::KeyCode::KC_F11:          return Key::F11;
            case Input::KeyCode::KC_F12:          return Key::F12;
            case Input::KeyCode::KC_F13:          return Key::F13;
            case Input::KeyCode::KC_F14:          return Key::F14;
            case Input::KeyCode::KC_F15:          return Key::F15;
            case Input::KeyCode::KC_NUMLOCK:      return Key::NumLock;
            case Input::KeyCode::KC_SCROLL:       return Key::ScrollLock;
            case Input::KeyCode::KC_RSHIFT:       return Key::RightShift;
            case Input::KeyCode::KC_LSHIFT:       return Key::LeftShift;
            case Input::KeyCode::KC_RCONTROL:     return Key::RightControl;
            case Input::KeyCode::KC_LCONTROL:     return Key::LeftControl;
            case Input::KeyCode::KC_RMENU:        return Key::RightAlt;
            case Input::KeyCode::KC_LMENU:        return Key::LeftAlt;
            case Input::KeyCode::KC_LWIN:         return Key::LeftWindows;
            case Input::KeyCode::KC_RWIN:         return Key::RightWindows;
            case Input::KeyCode::KC_SYSRQ:        return Key::SysRq;
            case Input::KeyCode::KC_APPS:         return Key::AppMenu;
            case Input::KeyCode::KC_POWER:        return Key::Power;
            default:               break;
        }
        return Key::Unknown;
    }
};

CEGUIInput::CEGUIInput(GUI& parent) noexcept
    : _parent(parent)
{
}

void CEGUIInput::init(const Configuration& config) noexcept
{
    _enabled = config.gui.cegui.enabled;
}

// return true if the input was consumed
bool CEGUIInput::injectKey(const bool pressed, const Input::KeyEvent& evt)
{
    if (!_enabled) 
    {
        return false;
    }

    const CEGUI::Key::Scan CEGUIKey = DivideKeyToCeGuiKey(evt._key);

    bool consumed = false;
    if (pressed) 
    {
        if (_parent.getCEGUIContext()->injectKeyDown(CEGUIKey))
        {
            begin(evt);
            consumed = true;
        }
    }
    else
    {
        if (_parent.getCEGUIContext()->injectKeyUp(CEGUIKey))
        {
            end(evt);
            consumed = true;
        }
    }

    return consumed;
}

void CEGUIInput::repeatKey(const Input::KeyEvent& evt)
{
    if (!_enabled)
    {
        return;
    }

    const CEGUI::Key::Scan CEGUIKey = DivideKeyToCeGuiKey(evt._key);
    _parent.getCEGUIContext()->injectKeyUp(CEGUIKey);
    _parent.getCEGUIContext()->injectKeyDown(CEGUIKey);
}

// Return true if input was consumed
bool CEGUIInput::onKeyDown(const Input::KeyEvent& key)
{
    return injectKey(true, key);
}

// Return true if input was consumed
bool CEGUIInput::onKeyUp(const Input::KeyEvent& key)
{
    return injectKey(false, key);
}

// Return true if input was consumed
bool CEGUIInput::mouseMoved(const Input::MouseMoveEvent& arg)
{
    if (!_enabled)
    {
        return false;
    }

    if (arg._wheelEvent)
    {
        return _parent.getCEGUIContext()->injectMouseWheelChange(to_F32(arg.state().VWheel ));
    }

    return _parent.getCEGUIContext()->injectMousePosition( to_F32( arg.state().X.abs ), to_F32( arg.state().Y.abs ) );
}

// Return true if input was consumed
bool CEGUIInput::mouseButtonPressed(const Input::MouseButtonEvent& arg)
{
    if (!_enabled)
    {
        return false;
    }

    bool consumed = false;
    switch (arg.button())
    {
        case Input::MouseButton::MB_Left:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonDown(CEGUI::LeftButton);
        } break;
        case Input::MouseButton::MB_Middle:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonDown(CEGUI::MiddleButton);
        } break;
        case Input::MouseButton::MB_Right:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonDown(CEGUI::RightButton);
        } break;
        case Input::MouseButton::MB_Button3:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonDown(CEGUI::X1Button);
        } break;
        case Input::MouseButton::MB_Button4:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonDown(CEGUI::X2Button);
        } break;

        case Input::MouseButton::MB_Button5:
        case Input::MouseButton::MB_Button6:
        case Input::MouseButton::MB_Button7:
        case Input::MouseButton::COUNT: break;
    };

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::mouseButtonReleased(const Input::MouseButtonEvent& arg)
{
    if (!_enabled)
    {
        return false;
    }

    bool consumed = false;

    switch (arg.button()) {
        case Input::MouseButton::MB_Left:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonUp(CEGUI::LeftButton);
        } break;
        case Input::MouseButton::MB_Middle:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonUp(CEGUI::MiddleButton);
        } break;
        case Input::MouseButton::MB_Right:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonUp(CEGUI::RightButton);
        } break;
        case Input::MouseButton::MB_Button3:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonUp(CEGUI::X1Button);
        } break;
        case Input::MouseButton::MB_Button4:
        {
            consumed = _parent.getCEGUIContext()->injectMouseButtonUp(CEGUI::X2Button);
        } break;
        default: break;
    };

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickAxisMoved( [[maybe_unused]] const Input::JoystickEvent& arg )
{
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickPovMoved( [[maybe_unused]] const Input::JoystickEvent& arg )
{
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickButtonPressed( [[maybe_unused]] const Input::JoystickEvent& arg )
{
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickButtonReleased( [[maybe_unused]] const Input::JoystickEvent& arg )
{
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickBallMoved( [[maybe_unused]] const Input::JoystickEvent& arg )
{
    const bool consumed = false;

    return consumed;
}

// Return true if input was consumed
bool CEGUIInput::joystickAddRemove( [[maybe_unused]] const Input::JoystickEvent& arg )
{
    const bool consumed = false;

    return consumed;
}

bool CEGUIInput::joystickRemap( [[maybe_unused]] const Input::JoystickEvent &arg )
{
    const bool consumed = false;

    return consumed;
}

bool CEGUIInput::onTextEvent(const Input::TextEvent& arg)
{
    if (!_enabled)
    {
        return false;
    }

    const char* utf8str = arg._text.c_str();

    static SDL_iconv_t cd = SDL_iconv_t(-1);

    if (cd == SDL_iconv_t(-1))
    {
        // note: just "UTF-32" doesn't work as toFormat, because then you get BOMs, which we don't want.
        const char* toFormat = "UTF-32LE"; // TODO: what does CEGUI expect on big endian machines?
        cd = SDL_iconv_open(toFormat, "UTF-8");
        if (cd == SDL_iconv_t(-1))
        {
            Console::errorfn(LOCALE_STR("ERROR_CEGUI_SDL_UTF"));
            return false;
        }
    }

    // utf8str has at most SDL_TEXTINPUTEVENT_TEXT_SIZE (32) chars,
    // so we won't have have more utf32 chars than that
    Uint32 utf32buf[SDL_TEXTINPUTEVENT_TEXT_SIZE] = {0};

    // we'll convert utf8str to a utf32 string, saved in utf32buf.
    // the utf32 chars will be injected into cegui

    size_t len = strlen(utf8str);

    size_t inbytesleft = len;
    size_t outbytesleft = 4 * SDL_TEXTINPUTEVENT_TEXT_SIZE; // *4 because utf-32 needs 4x as much space as utf-8
    char* outbuf = (char*)utf32buf;
    size_t n = SDL_iconv(cd, &utf8str, &inbytesleft, &outbuf, &outbytesleft);

    if (n == size_t(-1)) // some error occured during iconv
    {
        Console::errorfn(LOCALE_STR("ERROR_CEGUI_UTF_CONVERSION"));
        return false;
    }

    for (U8 i = 0u; i < SDL_TEXTINPUTEVENT_TEXT_SIZE; ++i)
    {
        if (utf32buf[i] == 0)
            break; // end of string

        _parent.getCEGUIContext()->injectChar(utf32buf[i]);
    }

    // reset cd so it can be used again
    SDL_iconv(cd, NULL, &inbytesleft, NULL, &outbytesleft);
    return true;
}

} //namespace Divide
