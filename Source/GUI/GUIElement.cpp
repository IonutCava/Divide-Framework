

#include "Headers/GUIElement.h"

namespace Divide {

GUIElement::GUIElement(const std::string_view name, CEGUI::Window* const parent) noexcept
    : GUIDWrapper(),
      _name(name),
      _parent(parent)
{
}

} //namespace Divide
