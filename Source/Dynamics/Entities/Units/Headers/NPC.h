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
#ifndef DVD_NON_PLAYER_CHARACTER_H_
#define DVD_NON_PLAYER_CHARACTER_H_

#include "Character.h"

namespace Divide {

namespace AI {
    class AIEntity;
}

/// NPC base class. Every character in the game is an NPC by default except the Player
class NPC final : public Character
{
   public:
    /// NPCs don't need AI by default
    explicit NPC( const vec3<F32>& currentPosition, std::string_view name );
    ~NPC() override = default;
    void update(U64 deltaTimeUS) override;
    
    [[nodiscard]] AI::AIEntity* getAIEntity() const noexcept;

protected:
    std::unique_ptr<AI::AIEntity> _aiUnit;
};

}  // namespace Divide

#endif //DVD_NON_PLAYER_CHARACTER_H_
