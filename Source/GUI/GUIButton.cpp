

#include "Headers/GUIButton.h"

namespace Divide {

NO_DESTROY GUIButton::AudioCallback GUIButton::s_soundCallback;

GUIButton::GUIButton(const string& name,
                     const string& text,
                     const string& guiScheme,
                     const RelativePosition2D& offset,
                     const RelativeScale2D& size,
                     CEGUI::Window* parent)
    : GUIElementBase(name, parent),
      _btnWindow(nullptr)
{
    
    NO_DESTROY static string buttonInfo = guiScheme + "/Button";

    if (parent != nullptr)
    {
        _btnWindow = CEGUI::WindowManager::getSingleton().createWindow(buttonInfo.c_str(), name.c_str());

        const CEGUI::UDim sizeX(0.f, size._x._width);
        const CEGUI::UDim sizeY(0.f, size._y._height);
        const CEGUI::USize ceguiSize(sizeX, sizeY);

        const CEGUI::UDim posX(offset._x._scale, offset._x._offset);
        const CEGUI::UDim posY(offset._y._scale, offset._y._offset);
        const CEGUI::UVector2 ceguiPosition(posX, posY);

        _btnWindow->setPosition( ceguiPosition );

        _btnWindow->setSize( ceguiSize );

        _btnWindow->setText(text.c_str());


        _connections[to_base(Event::MouseMove)] =
        _btnWindow->subscribeEvent(CEGUI::PushButton::EventMouseMove,
                                   CEGUI::Event::Subscriber([this](const CEGUI::EventArgs& e) ->bool {
                                        return onEvent(Event::MouseMove, e);
                                   }));

        _connections[to_base(Event::HoverEnter)] =
        _btnWindow->subscribeEvent(CEGUI::PushButton::EventMouseEntersArea,
                                   CEGUI::Event::Subscriber([this](const CEGUI::EventArgs& e) ->bool {
                                        return onEvent(Event::HoverEnter, e);
                                   }));

        _connections[to_base(Event::HoverLeave)] =
        _btnWindow->subscribeEvent(CEGUI::PushButton::EventMouseLeavesArea,
                                   CEGUI::Event::Subscriber([this](const CEGUI::EventArgs& e) ->bool {
                                        return onEvent(Event::HoverLeave, e);
                                    }));

        _connections[to_base(Event::MouseDown)] =
        _btnWindow->subscribeEvent(CEGUI::PushButton::EventMouseButtonDown,
                                   CEGUI::Event::Subscriber([this](const CEGUI::EventArgs& e) ->bool {
                                        return onEvent(Event::MouseDown, e);
                                    }));

        _connections[to_base(Event::MouseUp)] =
        _btnWindow->subscribeEvent(CEGUI::PushButton::EventMouseButtonUp,
                                   CEGUI::Event::Subscriber([this](const CEGUI::EventArgs& e) ->bool {
                                        return onEvent(Event::MouseUp, e);
                                    }));

        _connections[to_base(Event::MouseClick)] =
        _btnWindow->subscribeEvent(CEGUI::PushButton::EventMouseClick,
                                   CEGUI::Event::Subscriber([this](const CEGUI::EventArgs& e) ->bool {
                                        return onEvent(Event::MouseClick, e);
                                    }));

        _connections[to_base(Event::MouseDoubleClick)] =
        _btnWindow->subscribeEvent(CEGUI::PushButton::EventMouseDoubleClick,
                                   CEGUI::Event::Subscriber([this](const CEGUI::EventArgs& e) ->bool {
                                        return onEvent(Event::MouseDoubleClick, e);
                                    }));

        _connections[to_base(Event::MouseTripleClick)] =
        _btnWindow->subscribeEvent(CEGUI::PushButton::EventMouseDoubleClick,
                                   CEGUI::Event::Subscriber([this](const CEGUI::EventArgs& e) ->bool {
                                        return onEvent(Event::MouseTripleClick, e);
                                    }));
        _parent->addChild(_btnWindow);
    }
    active(true);
}

GUIButton::~GUIButton()
{
    if (_btnWindow != nullptr) {
        _btnWindow->removeAllEvents();
        _parent->removeChild(_btnWindow);
    }
}

void GUIButton::active(const bool active) noexcept {
    if (_btnWindow != nullptr && GUIElement::active() != active) {
        GUIElement::active(active);
        _btnWindow->setEnabled(active);
    }
}

void GUIButton::visible(const bool visible) noexcept {
    if (_btnWindow != nullptr && GUIElement::visible() != visible) {
        GUIElement::visible(visible);
        _btnWindow->setVisible(visible);
    }
}

void GUIButton::setText(const std::string_view& text) const {
    if (_btnWindow != nullptr) {
        _btnWindow->setText(CEGUI::String{ text.data(), text.length() });
    }
}

void GUIButton::setTooltip(const string& tooltipText) {
    if (_btnWindow != nullptr) {
        _btnWindow->setTooltipText(tooltipText.c_str());
    }
}

void GUIButton::setFont(const string& fontName,
                        const string& fontFileName, const U32 size) const
{
    if (_btnWindow != nullptr) {
        if (!fontName.empty()) {
            if (!CEGUI::FontManager::getSingleton().isDefined(fontName.c_str())) {
                CEGUI::FontManager::getSingleton().createFreeTypeFont(
                    fontName.c_str(), to_F32(size), true, fontFileName.c_str());
            }

            if (CEGUI::FontManager::getSingleton().isDefined(fontName.c_str())) {
                _btnWindow->setFont(fontName.c_str());
            }
        }
    }
}

bool GUIButton::soundCallback(const AudioCallback& cbk) {
    const bool hasCbk = s_soundCallback ? true : false;
    s_soundCallback = cbk;

    return hasCbk;
}

bool GUIButton::onEvent(const Event event, const CEGUI::EventArgs& /*e*/) {
    if (_callbackFunction[to_base(event)]) {
        _callbackFunction[to_base(event)](getGUID());
        if (_eventSound[to_base(event)] && s_soundCallback) {
            s_soundCallback(_eventSound[to_base(event)]);
        }
        return true;
    }
    return false;
}

void GUIButton::setEventCallback(const Event event, const ButtonCallback& callback) {
    _callbackFunction[to_base(event)] = callback;
}

void GUIButton::setEventSound(const Event event, const AudioDescriptor_ptr& sound) {
    _eventSound[to_base(event)] = sound;
}

void GUIButton::setEventCallback(const Event event, const ButtonCallback& callback, const AudioDescriptor_ptr& sound) {
    setEventCallback(event, callback);
    setEventSound(event, sound);
}

} //namespace Divide
