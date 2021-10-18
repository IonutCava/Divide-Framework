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
/*
    OgreCrowd
    ---------

    Copyright (c) 2012 Jonas Hauquier

    Additional contributions by:

    - mkultra333
    - Paul Wilson

    Sincere thanks and to:

    - Mikko Mononen (developer of Recast navigation libraries)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
   deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.

*/

#pragma once
#ifndef _CHARACTER_H_
#define _CHARACTER_H_

#include "Unit.h"

namespace Divide {

/// Basic character class.
/// Use the internally calculated position to update the attached node's
/// transform!
class Character : public Unit {
   public:
    /// Currently supported character types
    enum class CharacterType : U8 {
        /// user controlled character
        CHARACTER_TYPE_PLAYER = 0,
        /// non-user(player) character
        CHARACTER_TYPE_NPC,
        /// placeholder
        COUNT
    };

    explicit Character(CharacterType type);
    virtual ~Character() = default;

    /**
      * Update this character for drawing a new frame.
      * Updates one tick in the render loop.
      * In order for the agents to be updated, you first need to call the
      *detourCrowd
      * update function.
      * What is updated specifically is up to a specific implementation of
      *Character,
      * but at the very least the position in the scene should be updated to
      *reflect
      * the detour agent position (possibly with additional physics engine
      *clipping
      * and collision testing).
      **/
    virtual void update(U64 deltaTimeUS);
    /// Set the current position of this charater
    virtual void setPosition(const vec3<F32>& newPosition);
    /// Update character velocity
    virtual void setVelocity(const vec3<F32>& newVelocity);
    /// The current position of this character
    [[nodiscard]] virtual vec3<F32> getPosition() const;
    /// The direction in which the character is currently looking.
    [[nodiscard]] virtual vec3<F32> getLookingDirection();
    /// Rotate the character to look at another character
    virtual void lookAt(const vec3<F32>& targetPos);

    [[nodiscard]] const vec3<F32>& getRelativeLookingDirection() const noexcept { return _lookingDirection; }
    void setRelativeLookingDirection(const vec3<F32>& direction) noexcept { _lookingDirection = direction; }

    void playAnimation(I32 index) const;
    void playNextAnimation() const;
    void playPreviousAnimation() const;
    void pauseAnimation(bool state) const;

    PROPERTY_RW(CharacterType, characterType, CharacterType::COUNT);

   protected:
    void setParentNode(SceneGraphNode* node) override;

   private:
    vec3<F32> _lookingDirection;
    vec3<F32> _newPosition, _oldPosition, _curPosition;
    vec3<F32> _newVelocity, _curVelocity;
    std::atomic_bool _positionDirty;
    std::atomic_bool _velocityDirty;
};

}  // namespace Divide

#endif