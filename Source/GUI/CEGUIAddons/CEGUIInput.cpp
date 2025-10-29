

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
            case Input::KeyCode::KC_BACK         : return Key::Backspace;
            case Input::KeyCode::KC_TAB          : return Key::Tab;
            case Input::KeyCode::KC_RETURN       : return Key::Return;
            case Input::KeyCode::KC_PAUSE        : return Key::Pause;
            case Input::KeyCode::KC_ESCAPE       : return Key::Escape;
            case Input::KeyCode::KC_SPACE        : return Key::Space;
            case Input::KeyCode::KC_COMMA        : return Key::Comma;
            case Input::KeyCode::KC_MINUS        : return Key::Minus;
            case Input::KeyCode::KC_PERIOD       : return Key::Period;
            case Input::KeyCode::KC_SLASH        : return Key::Slash;
            case Input::KeyCode::KC_0            : return Key::Zero;
            case Input::KeyCode::KC_1            : return Key::One;
            case Input::KeyCode::KC_2            : return Key::Two;
            case Input::KeyCode::KC_3            : return Key::Three;
            case Input::KeyCode::KC_4            : return Key::Four;
            case Input::KeyCode::KC_5            : return Key::Five;
            case Input::KeyCode::KC_6            : return Key::Six;
            case Input::KeyCode::KC_7            : return Key::Seven;
            case Input::KeyCode::KC_8            : return Key::Eight;
            case Input::KeyCode::KC_9            : return Key::Nine;
            case Input::KeyCode::KC_COLON        : return Key::Colon;
            case Input::KeyCode::KC_SEMICOLON    : return Key::Semicolon;
            case Input::KeyCode::KC_EQUALS       : return Key::Equals;
            case Input::KeyCode::KC_LBRACKET     : return Key::LeftBracket;
            case Input::KeyCode::KC_BACKSLASH    : return Key::Backslash;
            case Input::KeyCode::KC_RBRACKET     : return Key::RightBracket;
            case Input::KeyCode::KC_A            : return Key::A;
            case Input::KeyCode::KC_B            : return Key::B;
            case Input::KeyCode::KC_C            : return Key::C;
            case Input::KeyCode::KC_D            : return Key::D;
            case Input::KeyCode::KC_E            : return Key::E;
            case Input::KeyCode::KC_F            : return Key::F;
            case Input::KeyCode::KC_G            : return Key::G;
            case Input::KeyCode::KC_H            : return Key::H;
            case Input::KeyCode::KC_I            : return Key::I;
            case Input::KeyCode::KC_J            : return Key::J;
            case Input::KeyCode::KC_K            : return Key::K;
            case Input::KeyCode::KC_L            : return Key::L;
            case Input::KeyCode::KC_M            : return Key::M;
            case Input::KeyCode::KC_N            : return Key::N;
            case Input::KeyCode::KC_O            : return Key::O;
            case Input::KeyCode::KC_P            : return Key::P;
            case Input::KeyCode::KC_Q            : return Key::Q;
            case Input::KeyCode::KC_R            : return Key::R;
            case Input::KeyCode::KC_S            : return Key::S;
            case Input::KeyCode::KC_T            : return Key::T;
            case Input::KeyCode::KC_U            : return Key::U;
            case Input::KeyCode::KC_V            : return Key::V;
            case Input::KeyCode::KC_W            : return Key::W;
            case Input::KeyCode::KC_X            : return Key::X;
            case Input::KeyCode::KC_Y            : return Key::Y;
            case Input::KeyCode::KC_Z            : return Key::Z;
            case Input::KeyCode::KC_DELETE       : return Key::Delete;
            case Input::KeyCode::KC_NUMPAD0      : return Key::Numpad0;
            case Input::KeyCode::KC_NUMPAD1      : return Key::Numpad1;
            case Input::KeyCode::KC_NUMPAD2      : return Key::Numpad2;
            case Input::KeyCode::KC_NUMPAD3      : return Key::Numpad3;
            case Input::KeyCode::KC_NUMPAD4      : return Key::Numpad4;
            case Input::KeyCode::KC_NUMPAD5      : return Key::Numpad5;
            case Input::KeyCode::KC_NUMPAD6      : return Key::Numpad6;
            case Input::KeyCode::KC_NUMPAD7      : return Key::Numpad7;
            case Input::KeyCode::KC_NUMPAD8      : return Key::Numpad8;
            case Input::KeyCode::KC_NUMPAD9      : return Key::Numpad9;
            case Input::KeyCode::KC_DECIMAL      : return Key::Decimal;
            case Input::KeyCode::KC_DIVIDE       : return Key::Divide;
            case Input::KeyCode::KC_MULTIPLY     : return Key::Multiply;
            case Input::KeyCode::KC_SUBTRACT     : return Key::Subtract;
            case Input::KeyCode::KC_ADD          : return Key::Add;
            case Input::KeyCode::KC_NUMPADENTER  : return Key::NumpadEnter;
            case Input::KeyCode::KC_NUMPADEQUALS : return Key::NumpadEquals;
            case Input::KeyCode::KC_UP           : return Key::ArrowUp;
            case Input::KeyCode::KC_DOWN         : return Key::ArrowDown;
            case Input::KeyCode::KC_RIGHT        : return Key::ArrowRight;
            case Input::KeyCode::KC_LEFT         : return Key::ArrowLeft;
            case Input::KeyCode::KC_INSERT       : return Key::Insert;
            case Input::KeyCode::KC_HOME         : return Key::Home;
            case Input::KeyCode::KC_END          : return Key::End;
            case Input::KeyCode::KC_PGUP         : return Key::PageUp;
            case Input::KeyCode::KC_PGDOWN       : return Key::PageDown;
            case Input::KeyCode::KC_F1           : return Key::F1;
            case Input::KeyCode::KC_F2           : return Key::F2;
            case Input::KeyCode::KC_F3           : return Key::F3;
            case Input::KeyCode::KC_F4           : return Key::F4;
            case Input::KeyCode::KC_F5           : return Key::F5;
            case Input::KeyCode::KC_F6           : return Key::F6;
            case Input::KeyCode::KC_F7           : return Key::F7;
            case Input::KeyCode::KC_F8           : return Key::F8;
            case Input::KeyCode::KC_F9           : return Key::F9;
            case Input::KeyCode::KC_F10          : return Key::F10;
            case Input::KeyCode::KC_F11          : return Key::F11;
            case Input::KeyCode::KC_F12          : return Key::F12;
            case Input::KeyCode::KC_F13          : return Key::F13;
            case Input::KeyCode::KC_F14          : return Key::F14;
            case Input::KeyCode::KC_F15          : return Key::F15;
            case Input::KeyCode::KC_NUMLOCK      : return Key::NumLock;
            case Input::KeyCode::KC_SCROLL       : return Key::ScrollLock;
            case Input::KeyCode::KC_RSHIFT       : return Key::RightShift;
            case Input::KeyCode::KC_LSHIFT       : return Key::LeftShift;
            case Input::KeyCode::KC_RCONTROL     : return Key::RightControl;
            case Input::KeyCode::KC_LCONTROL     : return Key::LeftControl;
            case Input::KeyCode::KC_RMENU        : return Key::RightAlt;
            case Input::KeyCode::KC_LMENU        : return Key::LeftAlt;
            case Input::KeyCode::KC_LWIN         : return Key::LeftWindows;
            case Input::KeyCode::KC_RWIN         : return Key::RightWindows;
            case Input::KeyCode::KC_SYSRQ        : return Key::SysRq;
            case Input::KeyCode::KC_APPS         : return Key::AppMenu;
            case Input::KeyCode::KC_POWER        : return Key::Power;
            case Input::KeyCode::KC_GRAVE        : return Key::Grave;
            case Input::KeyCode::KC_APOSTROPHE   : return Key::Apostrophe;
            case Input::KeyCode::KC_CAPITAL      : return Key::Capital;
            case Input::KeyCode::KC_OEM_102      : return Key::OEM_102;
            case Input::KeyCode::KC_KANA         : return Key::Kana;
            case Input::KeyCode::KC_ABNT_C1      : return Key::ABNT_C1;
            case Input::KeyCode::KC_CONVERT      : return Key::Convert;
            case Input::KeyCode::KC_NOCONVERT    : return Key::NoConvert;
            case Input::KeyCode::KC_YEN          : return Key::Yen;
            case Input::KeyCode::KC_ABNT_C2      : return Key::ABNT_C2;
            case Input::KeyCode::KC_PREVTRACK    : return Key::PrevTrack;
            case Input::KeyCode::KC_AT           : return Key::At;
            case Input::KeyCode::KC_UNDERLINE    : return Key::Underline;
            case Input::KeyCode::KC_KANJI        : return Key::Kanji;
            case Input::KeyCode::KC_STOP         : return Key::Stop;
            case Input::KeyCode::KC_AX           : return Key::AX;
            case Input::KeyCode::KC_UNLABELED    : return Key::Unlabeled;
            case Input::KeyCode::KC_NEXTTRACK    : return Key::NextTrack;
            case Input::KeyCode::KC_MUTE         : return Key::Mute;
            case Input::KeyCode::KC_CALCULATOR   : return Key::Calculator;
            case Input::KeyCode::KC_PLAYPAUSE    : return Key::PlayPause;
            case Input::KeyCode::KC_MEDIASTOP    : return Key::MediaStop;
            case Input::KeyCode::KC_VOLUMEDOWN   : return Key::VolumeDown;
            case Input::KeyCode::KC_VOLUMEUP     : return Key::VolumeUp;
            case Input::KeyCode::KC_WEBHOME      : return Key::WebHome;
            case Input::KeyCode::KC_NUMPADCOMMA  : return Key::NumpadComma;
            case Input::KeyCode::KC_SLEEP        : return Key::Sleep;
            case Input::KeyCode::KC_WAKE         : return Key::Wake;
            case Input::KeyCode::KC_WEBSEARCH    : return Key::WebSearch;
            case Input::KeyCode::KC_WEBFAVORITES : return Key::WebFavorites;
            case Input::KeyCode::KC_WEBREFRESH   : return Key::WebRefresh;
            case Input::KeyCode::KC_WEBSTOP      : return Key::WebStop;
            case Input::KeyCode::KC_WEBFORWARD   : return Key::WebForward;
            case Input::KeyCode::KC_WEBBACK      : return Key::WebBack;
            case Input::KeyCode::KC_MYCOMPUTER   : return Key::MyComputer;
            case Input::KeyCode::KC_MAIL         : return Key::Mail;
            case Input::KeyCode::KC_MEDIASELECT  : return Key::MediaSelect;

            case Input::KeyCode::KC_PRINTSCREEN  : //SYSRQ?
            case Input::KeyCode::KC_TWOSUPERIOR  : //Grave?
            case Input::KeyCode::KC_UNASSIGNED   : return Key::Unknown;

            default: DIVIDE_UNEXPECTED_CALL();   break;
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

bool CEGUIInput::onKeyInternal(Input::KeyEvent& argInOut)
{
    return injectKey(argInOut._state == Input::InputState::PRESSED, argInOut);
}

// Return true if input was consumed
bool CEGUIInput::onMouseMovedInternal(Input::MouseMoveEvent& argInOut)
{
    if (!_enabled)
    {
        return false;
    }

    if (argInOut._wheelEvent)
    {
        return _parent.getCEGUIContext()->injectMouseWheelChange(to_F32(argInOut.state().Wheel.yTicks ));
    }

    return _parent.getCEGUIContext()->injectMousePosition( to_F32(argInOut.state().X.abs ), to_F32(argInOut.state().Y.abs ) );
}

bool CEGUIInput::onMouseButtonInternal(Input::MouseButtonEvent& argInOut)
{
    if (!_enabled)
    {
        return false;
    }
    const bool pressed = argInOut.pressedState() == Input::InputState::PRESSED;

    CEGUI::MouseButton button = CEGUI::NoButton;

    switch (argInOut.button())
    {
        case Input::MouseButton::MB_Left:    button = CEGUI::LeftButton;   break;
        case Input::MouseButton::MB_Middle:  button = CEGUI::MiddleButton; break;
        case Input::MouseButton::MB_Right:   button = CEGUI::RightButton;  break;
        case Input::MouseButton::MB_Button3: button = CEGUI::X1Button;     break;
        case Input::MouseButton::MB_Button4: button = CEGUI::X2Button;     break;

        case Input::MouseButton::MB_Button5:
        case Input::MouseButton::MB_Button6:
        case Input::MouseButton::MB_Button7:
        case Input::MouseButton::COUNT:
        default: break;
    };

    if (button != CEGUI::NoButton)
    {
    
        return pressed ? _parent.getCEGUIContext()->injectMouseButtonDown(button)
                       : _parent.getCEGUIContext()->injectMouseButtonUp(button);
    }

    return false;
}

bool CEGUIInput::onJoystickButtonInternal( [[maybe_unused]] Input::JoystickEvent& argInOut)
{
    const bool consumed = false;

    return consumed;
}


bool CEGUIInput::onJoystickAxisMovedInternal( [[maybe_unused]] Input::JoystickEvent& argInOut)
{
    const bool consumed = false;

    return consumed;
}

bool CEGUIInput::onJoystickPovMovedInternal( [[maybe_unused]] Input::JoystickEvent& argInOut)
{
    const bool consumed = false;

    return consumed;
}

bool CEGUIInput::onJoystickBallMovedInternal( [[maybe_unused]] Input::JoystickEvent& argInOut)
{
    const bool consumed = false;

    return consumed;
}

bool CEGUIInput::onJoystickRemapInternal( [[maybe_unused]] Input::JoystickEvent& argInOut)
{
    const bool consumed = false;

    return consumed;
}

bool CEGUIInput::onTextInputInternal(Input::TextInputEvent& argInOut)
{
    if (!_enabled)
    {
        return false;
    }

    Uint32 codePoint = 0u;
    while ((codePoint = SDL_StepUTF8(&argInOut._utf8Text, NULL)) != 0)
    {
        if (codePoint == SDL_INVALID_UNICODE_CODEPOINT) // invalid code point
        {
            Console::errorfn(LOCALE_STR("ERROR_CEGUI_UTF_CONVERSION"));
        }
        else
        {
            _parent.getCEGUIContext()->injectChar(codePoint);
        }
    }

    return true;
}

bool CEGUIInput::onTextEditInternal([[maybe_unused]] Input::TextEditEvent& argInOut)
{
    return _enabled;
}

bool CEGUIInput::onDeviceAddOrRemoveInternal([[maybe_unused]] Input::InputEvent& argInOut)
{
    const bool consumed = false;
    return consumed;
}

} //namespace Divide
