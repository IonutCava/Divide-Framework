

#include "Headers/RagdollComponent.h"

namespace Divide {

RagdollComponent::RagdollComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<RagdollComponent, ComponentType::RAGDOLL>(parentSGN, context)
{
}

}