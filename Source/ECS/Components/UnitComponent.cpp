

#include "Headers/UnitComponent.h"

#include "Graphs/Headers/SceneGraphNode.h"
#include "Dynamics/Entities/Units/Headers/Unit.h"

namespace Divide {
DVD_INSTANTIATE_SGN_COMPONENT_NODE_OPS(UnitComponent);

UnitComponent::UnitComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<UnitComponent, ComponentType::UNIT>(parentSGN, context),
      _unit(nullptr)
{
}

bool UnitComponent::setUnit(const Unit_ptr& unit) {
    if (!_unit) {
        _unit = unit;
        Attorney::UnitComponent::setParentNode(_unit.get(), _parentSGN);
        return true;
    }

    return false;
}

} //namespace Divide
