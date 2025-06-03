

#include "Headers/SpotLightComponent.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Managers/Headers/ProjectManager.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide {

SpotLightComponent::SpotLightComponent(SceneGraphNode* sgn, PlatformContext& context)
     : BaseComponentType<SpotLightComponent, ComponentType::SPOT_LIGHT>(sgn, context)
     , Light(sgn, 30.0f, LightType::SPOT, *sgn->sceneGraph()->parentScene().lightPool())
{
    _shadowProperties._lightDetails.z = 0.0001f;

    EditorComponentField cutoffAngle = {};
    cutoffAngle._name = "Cutoff Angle";
    cutoffAngle._data = &_coneCutoffAngle;
    cutoffAngle._type = EditorComponentFieldType::PUSH_TYPE;
    cutoffAngle._readOnly = false;
    cutoffAngle._range = { EPSILON_F32, 179.99f };
    cutoffAngle._basicType = PushConstantType::FLOAT;
    editorComponent().registerField(MOV(cutoffAngle));

    EditorComponentField outerCutoffAngle = {};
    outerCutoffAngle._name = "Outer Cutoff Angle";
    outerCutoffAngle._data = &_outerConeCutoffAngle;
    outerCutoffAngle._type = EditorComponentFieldType::PUSH_TYPE;
    outerCutoffAngle._readOnly = false;
    outerCutoffAngle._range = { EPSILON_F32, 180.0f };
    outerCutoffAngle._basicType = PushConstantType::FLOAT;
    editorComponent().registerField(MOV(outerCutoffAngle));

    EditorComponentField directionField = {};
    directionField._name = "Direction";
    directionField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data) noexcept { static_cast<float3*>(dataOut)->set(directionCache()); };
    directionField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) { setDirection(*static_cast<const float3*>(data)); };
    directionField._type = EditorComponentFieldType::PUSH_TYPE;
    directionField._readOnly = true;
    directionField._basicType = PushConstantType::VEC3;

    editorComponent().registerField(MOV(directionField));

    EditorComponentField showConeField = {};
    showConeField._name = "Show direction cone";
    showConeField._data = &_showDirectionCone;
    showConeField._type = EditorComponentFieldType::PUSH_TYPE;
    showConeField._readOnly = false;
    showConeField._basicType = PushConstantType::BOOL;

    editorComponent().registerField(MOV(showConeField));

    registerFields(editorComponent());

    editorComponent().onChangedCbk([this](std::string_view) noexcept
    {
        if (coneCutoffAngle() > outerConeCutoffAngle())
        {
            coneCutoffAngle(outerConeCutoffAngle());
        }
    });

    Attorney::SceneNodeLightComponent::setBounds(sgn->getNode(), BoundingBox(float3(-1.0f), float3(1.0f)));
}

F32 SpotLightComponent::outerConeRadius() const noexcept
{
    return range() * std::tan(Angle::to_RADIANS(outerConeCutoffAngle()));
}

F32 SpotLightComponent::innerConeRadius() const noexcept
{
    return range() * std::tan(Angle::to_RADIANS(coneCutoffAngle()));
}

F32 SpotLightComponent::coneSlantHeight() const noexcept
{
    return Sqrt<F32>(SQUARED(outerConeRadius()) + SQUARED(range()));
}

void SpotLightComponent::OnData(const ECS::CustomEvent& data)
{
    SGNComponent::OnData(data);

    if (data._type == ECS::CustomEvent::Type::TransformUpdated)
    {
        updateCache(data);
    }
    else if (data._type == ECS::CustomEvent::Type::EntityFlagChanged)
    {
        const SceneGraphNode::Flags flag = static_cast<SceneGraphNode::Flags>(data._flag);
        if (flag == SceneGraphNode::Flags::SELECTED)
        {
            _drawImpostor = data._dataPair._first == 1u;
        }
    }
}

void SpotLightComponent::setDirection(const float3& direction) const
{
    TransformComponent* tComp = _parentSGN->get<TransformComponent>();
    if (tComp != nullptr)
    {
        tComp->setDirection(direction);
    }
}

} //namespace Divide
