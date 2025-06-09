

#include "Headers/ScriptComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

ScriptComponent::ScriptComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<ScriptComponent, ComponentType::SCRIPT>(parentSGN, context)
{
}


} //namespace Divide
