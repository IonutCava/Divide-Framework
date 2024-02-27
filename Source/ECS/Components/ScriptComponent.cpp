

#include "Headers/ScriptComponent.h"

namespace Divide {

ScriptComponent::ScriptComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<ScriptComponent, ComponentType::SCRIPT>(parentSGN, context)
{
}


} //namespace Divide
