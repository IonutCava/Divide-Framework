

#include "Headers/RagdollComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

DVD_INSTANTIATE_SGN_COMPONENT_NODE_OPS(RagdollComponent);

RagdollComponent::RagdollComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<RagdollComponent, ComponentType::RAGDOLL>(parentSGN, context)
{
}

} //namespace Divide
