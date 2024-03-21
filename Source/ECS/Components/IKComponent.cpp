

#include "Headers/IKComponent.h"

namespace Divide {

IKComponent::IKComponent(SceneGraphNode* parentSGN, PlatformContext& context)
 : BaseComponentType<IKComponent, ComponentType::INVERSE_KINEMATICS>(parentSGN, context)
{
}

} //namespace Divide
