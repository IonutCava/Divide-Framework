

#include "Headers/ScriptComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

DVD_INSTANTIATE_SGN_COMPONENT_NODE_OPS(ScriptComponent);

ScriptComponent::ScriptComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<ScriptComponent, ComponentType::SCRIPT>(parentSGN, context)
{
}


} //namespace Divide
