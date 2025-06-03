

#include "Headers/IKComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

IKComponent::IKComponent(SceneGraphNode* parentSGN, PlatformContext& context)
 : BaseComponentType<IKComponent, ComponentType::INVERSE_KINEMATICS>(parentSGN, context)
{
}

} //namespace Divide
