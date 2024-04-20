

#include "Headers/GUIMessageBox.h"

namespace Divide {

GUIMessageBox::GUIMessageBox(const string& name,
                             const string& title,
                             const string& message,
                             const vec2<I32> offsetFromCentre,
                             CEGUI::Window* parent)
    : GUIElementBase(name, parent)
{
    if (parent != nullptr) {
        // Get a local pointer to the CEGUI Window Manager, Purely for convenience
        // to reduce typing
        CEGUI::WindowManager* pWindowManager = CEGUI::WindowManager::getSingletonPtr();
        // load the messageBox Window from the layout file
        _msgBoxWindow = pWindowManager->loadLayoutFromFile("messageBox.layout");
        _msgBoxWindow->setName((title + "_MesageBox").c_str());
        _msgBoxWindow->setTextParsingEnabled(false);
        _parent->addChild(_msgBoxWindow);
        CEGUI::PushButton* confirmBtn = dynamic_cast<CEGUI::PushButton*>(_msgBoxWindow->getChild("ConfirmBtn"));
        _confirmEvent = confirmBtn->subscribeEvent(CEGUI::PushButton::EventClicked, CEGUI::Event::Subscriber(&GUIMessageBox::onConfirm, this));
    }
    setTitle(title);
    setMessage(message);
    setOffset(offsetFromCentre);
    GUIMessageBox::active(true);
    GUIMessageBox::visible(false);
}

GUIMessageBox::~GUIMessageBox()
{
    if (_parent != nullptr) {
        _parent->removeChild(_msgBoxWindow);
        CEGUI::WindowManager::getSingletonPtr()->destroyWindow(_msgBoxWindow);
    }
}

bool GUIMessageBox::onConfirm(const CEGUI::EventArgs& /*e*/) noexcept {
    active(false);
    visible(false);
    return true;
}

void GUIMessageBox::visible(const bool visible) noexcept {
    if (_parent != nullptr) {
        _msgBoxWindow->setVisible(visible);
        _msgBoxWindow->setModalState(visible);
    }
    GUIElement::visible(visible);
}

void GUIMessageBox::active(const bool active) noexcept {
    if (_parent != nullptr) {
        _msgBoxWindow->setEnabled(active);
    }
    GUIElement::active(active);
}

void GUIMessageBox::setTitle(const string& titleText) {
    if (_parent != nullptr) {
        _msgBoxWindow->setText(titleText.c_str());
    }
}

void GUIMessageBox::setMessage(const string& message) {
    if (_parent != nullptr) {
        _msgBoxWindow->getChild("MessageText")->setText(message.c_str());
    }
}

void GUIMessageBox::setOffset(const vec2<I32> offsetFromCentre) {
    if (_parent != nullptr) {
        CEGUI::UVector2 crtPosition(_msgBoxWindow->getPosition());
        crtPosition.d_x.d_offset += offsetFromCentre.x;
        crtPosition.d_y.d_offset += offsetFromCentre.y;
        _msgBoxWindow->setPosition(crtPosition);
    }
}

void GUIMessageBox::setMessageType(const MessageType type) {
    if (_parent != nullptr) {
        switch (type) {
        case MessageType::MESSAGE_INFO: {
            _msgBoxWindow->setProperty("CaptionColour", "FFFFFFFF");
        } break;
        case MessageType::MESSAGE_WARNING: {
            _msgBoxWindow->setProperty("CaptionColour", "00FFFFFF");
        } break;
        case MessageType::MESSAGE_ERROR: {
            _msgBoxWindow->setProperty("CaptionColour", "FF0000FF");
        } break;
        }
    }
}
} //namespace Divide
