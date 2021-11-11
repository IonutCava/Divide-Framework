#include "stdafx.h"

#include "Headers/NPC.h"
#include "AI/Headers/AIEntity.h"

namespace Divide {

NPC::NPC(AI::AIEntity* const aiEntity)
    : Character(CharacterType::CHARACTER_TYPE_NPC),
      _aiUnit(aiEntity)
{
    if (_aiUnit) {
        assert(!_aiUnit->getUnitRef());
        _aiUnit->addUnitRef(this);
    }
}

void NPC::update(const U64 deltaTimeUS) {
    Character::update(deltaTimeUS);
}

AI::AIEntity* NPC::getAIEntity() const noexcept {
    return _aiUnit;
}

}