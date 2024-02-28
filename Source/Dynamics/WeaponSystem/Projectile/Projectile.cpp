

#include "Headers/Projectile.h"

namespace Divide {

Projectile::Projectile(const ProjectileType type)
    : _type(type),
      _properyMask(0)
{
    /// no placeholders please
    assert(_type != ProjectileType::COUNT);
}

bool Projectile::addProperties(const ProjectileProperty property) {
    assert(property != ProjectileProperty::COUNT);
    _properyMask |= to_U32(property);
    return true;
}
}