

#include "Headers/NPC.h"
#include "AI/Headers/AIEntity.h"

namespace Divide {

NPC::NPC( const vec3<F32>& currentPosition, const std::string_view name )
    : Character(CharacterType::CHARACTER_TYPE_NPC)
    , _aiUnit(std::make_unique<AI::AIEntity>( this, currentPosition, name))
{
    _aiUnit->load(currentPosition);
}

void NPC::update(const U64 deltaTimeUS)
{
    Character::update(deltaTimeUS);
}

AI::AIEntity* NPC::getAIEntity() const noexcept
{
    return _aiUnit.get();
}

} //namespace Divide
