#include "stdafx.h"

#include "Headers/SelectionComponent.h"

namespace Divide
{
    SelectionComponent::SelectionComponent(SceneGraphNode* parentSGN, PlatformContext& context)
        : BaseComponentType<SelectionComponent, ComponentType::SELECTION>(parentSGN, context)
    {
    }

} //namespace Divide
