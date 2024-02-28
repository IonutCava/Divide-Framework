

#include "Headers/Weapon.h"

namespace Divide {

Weapon::Weapon(const WeaponType type)
    : _type(type),
      _properyMask(0)
{
    /// no placeholders please
    assert(_type != WeaponType::COUNT);
}

bool Weapon::addProperty(const WeaponProperty property) {
    assert(property != WeaponProperty::COUNT);
    _properyMask |= to_U32(property);
    return true;
}
}