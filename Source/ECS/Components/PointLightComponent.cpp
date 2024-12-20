

#include "Headers/PointLightComponent.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Managers/Headers/ProjectManager.h"
#include "Graphs/Headers/SceneGraph.h"

namespace Divide {

PointLightComponent::PointLightComponent(SceneGraphNode* sgn, PlatformContext& context)
     : BaseComponentType<PointLightComponent, ComponentType::POINT_LIGHT>(sgn, context),
       Light(sgn, 20.0f, LightType::POINT, *sgn->sceneGraph()->parentScene().lightPool())
{
    _shadowProperties._lightDetails.z = 0.025f;

    registerFields(editorComponent());

    Attorney::SceneNodeLightComponent::setBounds(sgn->getNode(), BoundingBox(float3(-10.0f), float3(10.0f)));

    _directionCache.set(VECTOR3_ZERO);
}

void PointLightComponent::OnData(const ECS::CustomEvent& data) {
    SGNComponent::OnData(data);

    if (data._type == ECS::CustomEvent::Type::TransformUpdated) {
        updateCache(data);
    } else if (data._type == ECS::CustomEvent::Type::EntityFlagChanged) {
        const SceneGraphNode::Flags flag = static_cast<SceneGraphNode::Flags>(data._flag);
        if (flag == SceneGraphNode::Flags::SELECTED) {
            _drawImpostor = data._dataPair._first == 1u;
        }
    }
}

} //namespace Divide
