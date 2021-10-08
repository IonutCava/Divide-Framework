#include "stdafx.h"

#include "Headers/GUIText.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {
GUIText::GUIText(const string& name,
                 const string& text,
                 const bool  multiLine,
                 const RelativePosition2D& relativePosition,
                 const string& font,
                 const UColour4& colour,
                 CEGUI::Window* parent,
                 const U8 fontSize)
    : GUIElementBase(name, parent),
      TextElement(TextLabelStyle(font.c_str(), colour, fontSize), relativePosition)
{
    this->text(text.c_str(), multiLine);
}

const RelativePosition2D& GUIText::getPosition() const {
    return _position;
}
};