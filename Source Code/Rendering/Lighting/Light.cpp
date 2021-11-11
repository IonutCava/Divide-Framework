#include "stdafx.h"

#include "Headers/Light.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Sphere3D.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Managers/Headers/SceneManager.h"

namespace Divide {


namespace TypeUtil {
    const char* LightTypeToString(const LightType lightType) noexcept {
        return Names::lightType[to_base(lightType)];
    }

    LightType StringToLightType(const string& name) {
        for (U8 i = 0; i < to_U8(LightType::COUNT); ++i) {
            if (strcmp(name.c_str(), Names::lightType[i]) == 0) {
                return static_cast<LightType>(i);
            }
        }

        return LightType::COUNT;
    }
}

Light::Light(SceneGraphNode* sgn, [[maybe_unused]] const F32 range, const LightType type, LightPool& parentPool)
    : IEventListener(sgn->sceneGraph()->GetECSEngine()),
      _castsShadows(false),
      _sgn(sgn),
      _parentPool(parentPool),
      _type(type)
{
    _shadowProperties._lightDetails.z = 0.005f;
    _shadowProperties._lightDetails.w = 1.0f;

    if (!_parentPool.addLight(*this)) {
        //assert?
    }

    for (U8 i = 0; i < 6; ++i) {
        _shadowProperties._lightVP[i].identity();
        _shadowProperties._lightPosition[i].w = std::numeric_limits<F32>::max();
    }
    
    _shadowProperties._lightDetails.x = to_F32(type);
    setDiffuseColour(FColour3(DefaultColours::WHITE));

    const ECS::CustomEvent evt = {
        ECS::CustomEvent::Type::TransformUpdated,
        sgn->get<TransformComponent>(),
        to_U32(TransformType::ALL)
    };

    updateCache(evt);


    _enabled = true;
}

Light::~Light()
{
    UnregisterAllEventCallbacks();
    if (!_parentPool.removeLight(*this)) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void Light::registerFields(EditorComponent& comp) {
    EditorComponentField rangeField = {};
    rangeField._name = "Range";
    rangeField._data = &_range;
    rangeField._type = EditorComponentFieldType::PUSH_TYPE;
    rangeField._readOnly = false;
    rangeField._range = { std::numeric_limits<F32>::epsilon(), 10000.f };
    rangeField._basicType = GFX::PushConstantType::FLOAT;
    comp.registerField(MOV(rangeField));

    EditorComponentField intensityField = {};
    intensityField._name = "Intensity";
    intensityField._data = &_intensity;
    intensityField._type = EditorComponentFieldType::PUSH_TYPE;
    intensityField._readOnly = false;
    intensityField._range = { std::numeric_limits<F32>::epsilon(), 25.f };
    intensityField._basicType = GFX::PushConstantType::FLOAT;
    comp.registerField(MOV(intensityField));

    EditorComponentField colourField = {};
    colourField._name = "Colour";
    colourField._dataGetter = [this](void* dataOut) noexcept { static_cast<FColour3*>(dataOut)->set(getDiffuseColour()); };
    colourField._dataSetter = [this](const void* data) noexcept { setDiffuseColour(*static_cast<const FColour3*>(data)); };
    colourField._type = EditorComponentFieldType::PUSH_TYPE;
    colourField._readOnly = false;
    colourField._basicType = GFX::PushConstantType::FCOLOUR3;
    comp.registerField(MOV(colourField));

    EditorComponentField castsShadowsField = {};
    castsShadowsField._name = "Is Shadow Caster";
    castsShadowsField._data = &_castsShadows;
    castsShadowsField._type = EditorComponentFieldType::PUSH_TYPE;
    castsShadowsField._readOnly = false;
    castsShadowsField._basicType = GFX::PushConstantType::BOOL;
    comp.registerField(MOV(castsShadowsField));

    EditorComponentField shadowBiasField = {};
    shadowBiasField._name = "Shadow Bias";
    shadowBiasField._data = &_shadowProperties._lightDetails.z;
    shadowBiasField._type = EditorComponentFieldType::SLIDER_TYPE;
    shadowBiasField._readOnly = false;
    shadowBiasField._range = { std::numeric_limits<F32>::epsilon(), 1.0f };
    shadowBiasField._basicType = GFX::PushConstantType::FLOAT;
    comp.registerField(MOV(shadowBiasField));

    EditorComponentField shadowStrengthField = {};
    shadowStrengthField._name = "Shadow Strength";
    shadowStrengthField._data = &_shadowProperties._lightDetails.w;
    shadowStrengthField._type = EditorComponentFieldType::SLIDER_TYPE;
    shadowStrengthField._readOnly = false;
    shadowStrengthField._range = { std::numeric_limits<F32>::epsilon(), 10.0f };
    shadowStrengthField._basicType = GFX::PushConstantType::FLOAT;
    comp.registerField(MOV(shadowStrengthField));

    EditorComponentField lightTagField = {};
    lightTagField._name = "Light Tag Value";
    lightTagField._data = &_tag;
    lightTagField._type = EditorComponentFieldType::PUSH_TYPE;
    lightTagField._readOnly = false;
    lightTagField._hexadecimal = true;
    lightTagField._basicType = GFX::PushConstantType::UINT;

    comp.registerField(MOV(lightTagField));
}

void Light::updateCache(const ECS::CustomEvent& event) {
    OPTICK_EVENT();

    const TransformComponent* tComp = static_cast<TransformComponent*>(event._sourceCmp);
    assert(tComp != nullptr);

    if (_type != LightType::DIRECTIONAL && BitCompare(event._flag, to_U32(TransformType::TRANSLATION))) {
        _positionCache = tComp->getPosition();
    }

    if (_type != LightType::POINT && BitCompare(event._flag, to_U32(TransformType::ROTATION))) {
        _directionCache = DirectionFromAxis(tComp->getOrientation(), WORLD_Z_NEG_AXIS);
    }
}

void Light::setDiffuseColour(const UColour3& newDiffuseColour) noexcept {
    _colour.rgb = newDiffuseColour;
}

}