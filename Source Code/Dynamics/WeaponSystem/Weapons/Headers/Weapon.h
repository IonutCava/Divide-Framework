/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _WEAPON_H_
#define _WEAPON_H_

namespace Divide {

/// Base class for defining a weapon
class Weapon {
   public:
    /// Weapon type mask
    enum class WeaponType : U8 {
        /// Melee weapon (sword,axe,kinfe,lightsaber)
        WEAPON_TYPE_MELEE = toBit(1),
        /// ranged weapons (guns, bows etc)
        WEAPON_TYPE_RANGED = toBit(2),
        /// Place all weapon types above this
        COUNT
    };

    enum class WeaponProperty : U8 {
        /// this weapon does use ammo (or charges for melee)
        WEAPON_PROPERTY_WITH_AMMO = toBit(1),
        /// this weapon does not us ammo
        WEAPON_PROPERTY_WITHOUT_AMMO = toBit(2),
        /// Place all weapon types above this
        COUNT
    };

    Weapon(WeaponType type);
    ~Weapon() = default;

    /// Add a specific property to this weapon
    [[nodiscard]] bool addProperty(WeaponProperty property);

   private:
    WeaponType _type;
    U32 _properyMask;  ///< weapon properties
};

}  // namespace Divide

#endif