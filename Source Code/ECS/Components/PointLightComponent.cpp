#include "stdafx.h"

#include "Headers/PointLightComponent.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Managers/Headers/SceneManager.h"

namespace Divide {

PointLightComponent::PointLightComponent(SceneGraphNode* sgn, PlatformContext& context)
     : BaseComponentType<PointLightComponent, ComponentType::POINT_LIGHT>(sgn, context),
       Light(sgn, 20.0f, LightType::POINT, *sgn->sceneGraph()->parentScene().lightPool())
{
    _shadowProperties._lightDetails.z = 0.025f;

    registerFields(editorComponent());

    BoundingBox bb = {};
    bb.setMin(-10.0f);
    bb.setMax(10.0f);
    Attorney::SceneNodeLightComponent::setBounds(sgn->getNode(), bb);

    _directionCache.set(VECTOR3_ZERO);
}

void PointLightComponent::OnData(const ECS::CustomEvent& data) {
    SGNComponent::OnData(data);

    if (data._type == ECS::CustomEvent::Type::TransformUpdated) {
        updateCache(data);
    } else if (data._type == ECS::CustomEvent::Type::EntityFlagChanged) {
        const SceneGraphNode::Flags flag = static_cast<SceneGraphNode::Flags>(data._flag);
        if (flag == SceneGraphNode::Flags::SELECTED) {
            _drawImpostor = data._dataFirst == 1u;
        }
    }
}

}