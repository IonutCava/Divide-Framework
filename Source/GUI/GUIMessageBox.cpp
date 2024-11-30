

#include "Headers/GUIMessageBox.h"

namespace Divide {

GUIMessageBox::GUIMessageBox(const std::string_view name,
                             const std::string_view title,
                             const std::string_view message,
                             const int2 offsetFromCentre,
                             CEGUI::Window* parent)
    : GUIElementBase(name, parent)
{
    if (parent != nullptr)
    {
        // Get a local pointer to the CEGUI Window Manager, Purely for convenience
        // to reduce typing
        CEGUI::WindowManager* pWindowManager = CEGUI::WindowManager::getSingletonPtr();
        // load the messageBox Window from the layout file
        _msgBoxWindow = pWindowManager->loadLayoutFromFile("messageBox.layout");
        _msgBoxWindow->setName(CEGUI::String(title.data(), title.length()) + "_MesageBox");
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
    GUIMessageBox::active(false);
    if (_parent != nullptr)
    {
        CEGUI::Window* ptrCpy = _msgBoxWindow;
        _parent->removeChild(ptrCpy);
        _msgBoxWindow = nullptr;
        _parent = nullptr;
        CEGUI::WindowManager::getSingletonPtr()->destroyWindow(ptrCpy);
    }
}

bool GUIMessageBox::onConfirm(const CEGUI::EventArgs& /*e*/) noexcept
{
    active(false);
    visible(false);
    return true;
}

void GUIMessageBox::visible(const bool visible) noexcept
{
    if (_parent != nullptr)
    {
        _msgBoxWindow->setVisible(visible);
        _msgBoxWindow->setModalState(visible);
    }
    GUIElement::visible(visible);
}

void GUIMessageBox::active(const bool active) noexcept
{
    if (_parent != nullptr)
    {
        _msgBoxWindow->setEnabled(active);
    }

    GUIElement::active(active);
}

void GUIMessageBox::setTitle(const std::string_view titleText)
{
    if (_parent != nullptr)
    {
        _msgBoxWindow->setText(CEGUI::String(titleText.data(), titleText.length()));
    }
}

void GUIMessageBox::setMessage(const std::string_view message)
{
    if (_parent != nullptr)
    {
        _msgBoxWindow->getChild("MessageText")->setText(CEGUI::String(message.data(), message.length()));
    }
}

void GUIMessageBox::setOffset(const int2 offsetFromCentre)
{
    if (_parent != nullptr)
    {
        CEGUI::UVector2 crtPosition(_msgBoxWindow->getPosition());
        crtPosition.d_x.d_offset += offsetFromCentre.x;
        crtPosition.d_y.d_offset += offsetFromCentre.y;
        _msgBoxWindow->setPosition(crtPosition);
    }
}

void GUIMessageBox::setMessageType(const MessageType type)
{
    if (_parent != nullptr)
    {
        switch (type)
        {
            default:
            case MessageType::MESSAGE_INFO:    _msgBoxWindow->setProperty("CaptionColour", "FFFFFFFF"); break;
            case MessageType::MESSAGE_WARNING: _msgBoxWindow->setProperty("CaptionColour", "00FFFFFF"); break;
            case MessageType::MESSAGE_ERROR:   _msgBoxWindow->setProperty("CaptionColour", "FF0000FF"); break;
        }
    }
}
} //namespace Divide
