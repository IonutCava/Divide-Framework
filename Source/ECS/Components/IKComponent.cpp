

#include "Headers/IKComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

DVD_INSTANTIATE_SGN_COMPONENT_NODE_OPS(IKComponent);

IKComponent::IKComponent(SceneGraphNode* parentSGN, PlatformContext& context)
 : BaseComponentType<IKComponent, ComponentType::INVERSE_KINEMATICS>(parentSGN, context)
{
}

} //namespace Divide
