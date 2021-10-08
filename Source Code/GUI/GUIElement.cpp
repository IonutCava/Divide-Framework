#include "stdafx.h"

#include "Headers/GUIElement.h"

namespace Divide {

GUIElement::GUIElement(string name, CEGUI::Window* const parent)
    : GUIDWrapper(),
      _name(MOV(name)),
      _parent(parent)
{
}

};