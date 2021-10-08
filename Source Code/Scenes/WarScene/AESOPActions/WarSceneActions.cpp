#include "stdafx.h"

#include "Headers/WarSceneActions.h"

#include "Scenes/WarScene/Headers/WarSceneAIProcessor.h"

namespace Divide::AI {

WarSceneAction::WarSceneAction(const ActionType type,
                               const string& name,
                               const F32 cost)
    : GOAPAction(name, to_I32(cost)),
    _type(type)
{
}

bool WarSceneAction::preAction(WarSceneAIProcessor& parentProcessor) const {
    return Attorney::WarAISceneWarAction::preAction(parentProcessor, _type, this);
}

bool WarSceneAction::postAction(WarSceneAIProcessor& parentProcessor) const {
    return Attorney::WarAISceneWarAction::postAction(parentProcessor, _type, this);
}

}  // namespace Divide::AI
