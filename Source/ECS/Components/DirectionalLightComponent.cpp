

#include "Headers/DirectionalLightComponent.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide {

namespace {
    constexpr F32 g_defaultLightDistance = 500.0f;
}

DirectionalLightComponent::DirectionalLightComponent(SceneGraphNode* sgn, PlatformContext& context)
    : BaseComponentType<DirectionalLightComponent, ComponentType::DIRECTIONAL_LIGHT>(sgn, context), 
      Light(sgn, -1, LightType::DIRECTIONAL, *sgn->sceneGraph()->parentScene().lightPool())
{
    range(g_defaultLightDistance);

    _shadowProperties._lightDetails.z = 0.00001f;
    csmSplitCount(context.config().rendering.shadowMapping.csm.splitCount);

    EditorComponentField directionField = {};
    directionField._name = "Direction";
    directionField._dataGetter = [this](void* dataOut) noexcept { static_cast<vec3<F32>*>(dataOut)->set(directionCache()); };
    directionField._dataSetter = [this](const void* data) { setDirection(*static_cast<const vec3<F32>*>(data)); };
    directionField._type = EditorComponentFieldType::PUSH_TYPE;
    directionField._readOnly = lockDirection();
    directionField._basicType = PushConstantType::VEC3;

    editorComponent().registerField(MOV(directionField));

    for (U8 cascade = 0; cascade < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++cascade) {
        EditorComponentField sceneFitField = {};
        sceneFitField._name = Util::StringFormat("Fit CSM To AABB [ {} ]", cascade).c_str();
        sceneFitField._data = &_csmUseSceneAABBFit[cascade];
        sceneFitField._type = EditorComponentFieldType::PUSH_TYPE;
        sceneFitField._readOnly = cascade >= csmSplitCount();
        sceneFitField._basicType = PushConstantType::BOOL;

        editorComponent().registerField(MOV(sceneFitField));
    }

    EditorComponentField csmNearClipField = {};
    csmNearClipField._name = "CSM Near Clip Offset";
    csmNearClipField._data = &_csmNearClipOffset;
    csmNearClipField._range = { -g_defaultLightDistance, g_defaultLightDistance };
    csmNearClipField._type = EditorComponentFieldType::PUSH_TYPE;
    csmNearClipField._readOnly = false;
    csmNearClipField._basicType = PushConstantType::FLOAT;

    editorComponent().registerField(MOV(csmNearClipField));

    EditorComponentField showConeField = {};
    showConeField._name = "Show direction cone";
    showConeField._data = &_showDirectionCone;
    showConeField._type = EditorComponentFieldType::PUSH_TYPE;
    showConeField._readOnly = false;
    showConeField._basicType = PushConstantType::BOOL;

    editorComponent().registerField(MOV(showConeField));

    registerFields(editorComponent());

    BoundingBox bb = {};
    bb.setMin(-g_defaultLightDistance * 0.5f);
    bb.setMax(-g_defaultLightDistance * 0.5f);
    Attorney::SceneNodeLightComponent::setBounds(sgn->getNode(), bb);

    _positionCache.set(VECTOR3_ZERO);
    _feedbackContainers.resize(csmSplitCount());
}

void DirectionalLightComponent::lockDirection(const bool state) noexcept {
    _lockDirection = state;

    TransformComponent* tComp = _parentSGN->get<TransformComponent>();
    if (tComp != nullptr) {
        tComp->editorLockRotation(lockDirection());
    }
}

void DirectionalLightComponent::OnData(const ECS::CustomEvent& data) {
    SGNComponent::OnData(data);

    if (data._type == ECS::CustomEvent::Type::TransformUpdated) {
        updateCache(data);
    } else if (data._type == ECS::CustomEvent::Type::EntityFlagChanged) {
        const SceneGraphNode::Flags flag = static_cast<SceneGraphNode::Flags>(data._flag);
        if (flag == SceneGraphNode::Flags::SELECTED) {
            const bool state = data._dataPair._first == 1u;
            drawImpostor(state);
        }
    }
}

void DirectionalLightComponent::setDirection(const vec3<F32>& direction) {
    TransformComponent* tComp = _parentSGN->get<TransformComponent>();
    if (tComp != nullptr) {
        tComp->setDirection(direction);
    }
}

}