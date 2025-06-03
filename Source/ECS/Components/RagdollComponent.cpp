

#include "Headers/RagdollComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

RagdollComponent::RagdollComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<RagdollComponent, ComponentType::RAGDOLL>(parentSGN, context)
{
}

} //namespace Divide
