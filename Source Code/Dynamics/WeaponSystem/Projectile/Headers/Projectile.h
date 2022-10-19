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
#ifndef _PROJECTILE_H_
#define _PROJECTILE_H_

namespace Divide {

/// Defines a projectile object (usually bullets or rockets)
class Projectile {
   public:
    /// The physical characteristics of this projectile
    enum class ProjectileType : U8 {
        /// Use a raytrace scan to the target's hitbox (raygun, non-tracked
        /// bullets)
        PROJECTILE_TYPE_RAYTRACE = toBit(1),
        /// Use a slow-moving object as projectile (e.g. rockets, arrows,
        /// spells)
        PROJECTILE_TYPE_SLOW = toBit(2),
        /// Add new projectile types above
        COUNT
    };

    enum class ProjectileProperty : U8 {
        /// Projectile is not affected by gravity (raygun, spells)
        PROJECTILE_PROPERTYE_NO_GRAVITY = toBit(1),
        /// Projectile is affected by gravity (rockets, boulders, sniperbullets)
        PROJECTILE_PROPERTY_GRAVITY = toBit(2),
        /// Add new projectile properties above
        COUNT
    };

    Projectile(ProjectileType type);
    ~Projectile() = default;

    /// Add a specific property to this projectile
    bool addProperties(ProjectileProperty property);

   private:
    ProjectileType _type;
    U32 _properyMask;  ///< weapon properties
};

}  // namespace Divide

#endif