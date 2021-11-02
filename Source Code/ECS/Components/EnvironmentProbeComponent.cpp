#include "stdafx.h"

#include "Headers/EnvironmentProbeComponent.h"

#include "Core/Headers/Kernel.h"
#include "Headers/TransformComponent.h"

#include "Scenes/Headers/Scene.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Scenes/Headers/SceneShaderData.h"

namespace Divide {

namespace TypeUtil {
    const char* EnvProveUpdateTypeToString(const EnvironmentProbeComponent::UpdateType type) noexcept {
        return EnvironmentProbeComponent::Names::updateType[to_base(type)];
    }

    EnvironmentProbeComponent::UpdateType StringToEnvProveUpdateType(const char* name) noexcept {
        for (U8 i = 0; i < to_U8(EnvironmentProbeComponent::UpdateType::COUNT); ++i) {
            if (strcmp(name, EnvironmentProbeComponent::Names::updateType[i]) == 0) {
                return static_cast<EnvironmentProbeComponent::UpdateType>(i);
            }
        }

        return EnvironmentProbeComponent::UpdateType::COUNT;
    }
}

EnvironmentProbeComponent::EnvironmentProbeComponent(SceneGraphNode* sgn, PlatformContext& context)
    : BaseComponentType<EnvironmentProbeComponent, ComponentType::ENVIRONMENT_PROBE>(sgn, context),
      GUIDWrapper()
{
    Scene& parentScene = sgn->sceneGraph()->parentScene();

    EditorComponentField layerField = {};
    layerField._name = "RT Layer index";
    layerField._data = &_rtLayerIndex;
    layerField._type = EditorComponentFieldType::PUSH_TYPE;
    layerField._readOnly = true;
    layerField._serialise = false;
    layerField._basicType = GFX::PushConstantType::INT;
    layerField._basicTypeSize = GFX::PushConstantSize::WORD;

    getEditorComponent().registerField(MOV(layerField));

    EditorComponentField typeField = {};
    typeField._name = "Is Local";
    typeField._dataGetter = [this](void* dataOut) {
        *static_cast<bool*>(dataOut) = _type == ProbeType::TYPE_LOCAL;
    };
    typeField._dataSetter = [this](const void* data) {
        _type = *static_cast<const bool*>(data) ? ProbeType::TYPE_LOCAL : ProbeType::TYPE_INFINITE;
    };
    typeField._type = EditorComponentFieldType::PUSH_TYPE;
    typeField._readOnly = false;
    typeField._basicType = GFX::PushConstantType::BOOL;

    getEditorComponent().registerField(MOV(typeField));

    EditorComponentField bbField = {};
    bbField._name = "Bounding Box";
    bbField._data = &_refaabb;
    bbField._type = EditorComponentFieldType::BOUNDING_BOX;
    bbField._readOnly = false;
    getEditorComponent().registerField(MOV(bbField));

    EditorComponentField updateTypeField = {};
    updateTypeField._name = "Update Type";
    updateTypeField._type = EditorComponentFieldType::DROPDOWN_TYPE;
    updateTypeField._readOnly = false;
    updateTypeField._range = { 0u, to_U8(UpdateType::COUNT) };
    updateTypeField._dataGetter = [this](void* dataOut) {
        *static_cast<U8*>(dataOut) = to_U8(updateType());
    };
    updateTypeField._dataSetter = [this](const void* data) {
        updateType(*static_cast<const UpdateType*>(data));
    };
    updateTypeField._displayNameGetter = [](const U8 index) {
        return TypeUtil::EnvProveUpdateTypeToString(static_cast<UpdateType>(index));
    };

    getEditorComponent().registerField(MOV(updateTypeField));

    EditorComponentField updateRateField = {};
    updateRateField._name = "Update Rate";
    updateRateField._tooltip = "[0...TARGET_FPS]. Every Nth frame. 0 = disabled;";
    updateRateField._data = &_updateRate;
    updateRateField._type = EditorComponentFieldType::PUSH_TYPE;
    updateRateField._readOnly = false;
    updateRateField._range = { 0.f, Config::TARGET_FRAME_RATE };
    updateRateField._basicType = GFX::PushConstantType::UINT;
    updateRateField._basicTypeSize = GFX::PushConstantSize::BYTE;

    getEditorComponent().registerField(MOV(updateRateField));

    EditorComponentField showBoxField = {};
    showBoxField._name = "Show parallax correction AABB";
    showBoxField._data = &_showParallaxAABB;
    showBoxField._type = EditorComponentFieldType::PUSH_TYPE;
    showBoxField._readOnly = false;
    showBoxField._basicType = GFX::PushConstantType::BOOL;

    getEditorComponent().registerField(MOV(showBoxField));

    getEditorComponent().onChangedCbk([this](std::string_view) {
        const vec3<F32> pos = _parentSGN->get<TransformComponent>()->getPosition();
        setBounds(_refaabb.getMin() + pos, _refaabb.getMax() + pos);
    });

    Attorney::SceneEnvironmentProbeComponent::registerProbe(parentScene, this);
    enabled(true);
}

EnvironmentProbeComponent::~EnvironmentProbeComponent()
{
    Attorney::SceneEnvironmentProbeComponent::unregisterProbe(_parentSGN->sceneGraph()->parentScene(), this);
    enabled(false);
}

SceneGraphNode* EnvironmentProbeComponent::findNodeToIgnore() const noexcept {
    //If we are not a root-level probe. Avoid rendering our parent and children into the reflection
    if (getSGN()->parent() != nullptr) {
        SceneGraphNode* parent = getSGN()->parent();
        while (parent != nullptr) {
            const SceneNodeType type = parent->getNode().type();

            if (type != SceneNodeType::TYPE_TRANSFORM || type != SceneNodeType::TYPE_TRIGGER) {
                return parent;
            }

            // Keep walking up
            parent = parent->parent();
        }
    }

    return nullptr;
}

bool EnvironmentProbeComponent::checkCollisionAndQueueUpdate(const BoundingSphere& sphere) {
    if (!dirty() && updateType() == UpdateType::ON_DIRTY && _aabb.collision(sphere)) {
        dirty(true);
    }

    return dirty();
}

bool EnvironmentProbeComponent::refresh(GFX::CommandBuffer& bufferInOut) {
    if (!dirty()) {
        return false;
    }
    dirty(false);
    _queueRefresh = false;

    SceneEnvironmentProbePool::ProbesDirty(true);

    if (updateType() != UpdateType::ALWAYS) {
        rtLayerIndex(SceneEnvironmentProbePool::AllocateSlice(false));
    }

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("EnvironmentProbePass Id: [ %d ]", rtLayerIndex()).c_str()));

    vector<Camera*>& probeCameras = SceneEnvironmentProbePool::ProbeCameras();

    std::array<Camera*, 6> cameras = {};
    std::copy_n(std::begin(probeCameras), std::min(cameras.size(), probeCameras.size()), std::begin(cameras));

    // Probes come after reflective nodes in buffer positions and array layers for management reasons (rate of update and so on)
    const U16 probeIndex = Config::MAX_REFLECTIVE_NODES_IN_VIEW + rtLayerIndex();

    RenderPassParams params = {};
    params._target = SceneEnvironmentProbePool::ReflectionTarget()._targetID;
    params._sourceNode = findNodeToIgnore();
    params._stagePass = RenderStagePass(RenderStage::REFLECTION, RenderPassType::COUNT, to_U8(ReflectorType::CUBE), probeIndex);

    ClearBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES)));

    _context.gfx().generateCubeMap(params,
                                   rtLayerIndex(),
                                   _aabb.getCenter(),
                                   vec2<F32>(0.1f, _aabb.getHalfExtent().length()),
                                   bufferInOut,
                                   cameras);
 
    GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
    computeMipMapsCommand._texture = SceneEnvironmentProbePool::ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0).texture().get();
    computeMipMapsCommand._layerRange = { rtLayerIndex(), 1 };
    computeMipMapsCommand._defer = false;
    EnqueueCommand(bufferInOut, computeMipMapsCommand);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    _currentUpdateCall = 0;
    return true;
}

void EnvironmentProbeComponent::enabled(const bool state) {
    Parent::enabled(state);
    const auto sceneData = _context.kernel().sceneManager()->sceneData();
    if (sceneData != nullptr) {
        sceneData->probeState(poolIndex(), state);
    }
}

void EnvironmentProbeComponent::updateProbeData() const noexcept {
    const auto sceneData = _context.kernel().sceneManager()->sceneData();
    sceneData->probeData(poolIndex(), _aabb.getCenter(), _aabb.getHalfExtent());
}

void EnvironmentProbeComponent::poolIndex(const U16 index) noexcept {
    _poolIndex = index;
    updateProbeData();
}

void EnvironmentProbeComponent::setBounds(const vec3<F32>& min, const vec3<F32>& max) {
    _aabb.set(min, max);
    updateProbeData();
}

void EnvironmentProbeComponent::setBounds(const vec3<F32>& center, const F32 radius) {
    _aabb.createFromSphere(center, radius);
    updateProbeData();
}

void EnvironmentProbeComponent::updateType(const UpdateType type) noexcept {
    if (updateType() == type){
        return;
    }

    if (updateType() == UpdateType::ALWAYS && rtLayerIndex() >= 0) {
        // Release slice if we switch to on-demand updates
        SceneEnvironmentProbePool::UnlockSlice(rtLayerIndex());
    }

    _updateType = type;
    if (type == UpdateType::ALWAYS) {
        const I16 newSlice = SceneEnvironmentProbePool::AllocateSlice(true);
        if (newSlice >= 0) {
            rtLayerIndex(newSlice);
        } else {
            if_constexpr (Config::Build::IS_DEBUG_BUILD) {
                DIVIDE_UNEXPECTED_CALL();
            }
            // Failed to allocate a slice. Fallback to manual updates
            updateType(UpdateType::ON_DIRTY);
        }
    }
}

F32 EnvironmentProbeComponent::distanceSqTo(const vec3<F32>& pos) const {
    return _aabb.getCenter().distanceSquared(pos);
}

void EnvironmentProbeComponent::OnData(const ECS::CustomEvent& data) {
    if (data._type == ECS::CustomEvent::Type::TransformUpdated) {
        const vec3<F32> pos = _parentSGN->get<TransformComponent>()->getPosition();
        setBounds(_refaabb.getMin() + pos, _refaabb.getMax() + pos);
    } else if (data._type == ECS::CustomEvent::Type::EntityFlagChanged) {
        const SceneGraphNode::Flags flag = static_cast<SceneGraphNode::Flags>(data._flag);
        if (flag == SceneGraphNode::Flags::SELECTED) {
            _drawImpostor = data._dataFirst == 1u;
        }
    }
}

std::array<Camera*, 6> EnvironmentProbeComponent::probeCameras() const noexcept {
    // Because we pool probe cameras, the current look at and projection settings may not  match
    // this probe specifically. Because we update the cameras per refresh call, changing camera settings
    // should be safe.

    static const std::array<vec3<F32>, 6> TabUp ={
          WORLD_Y_NEG_AXIS,
          WORLD_Y_NEG_AXIS,
          WORLD_Z_AXIS,
          WORLD_Z_NEG_AXIS,
          WORLD_Y_NEG_AXIS,
          WORLD_Y_NEG_AXIS
    };

    // Get the center and up vectors for each cube face
    static const std::array<vec3<F32>,6> TabCenter = {
        vec3<F32>( 1.0f,  0.0f,  0.0f), //Pos X
        vec3<F32>(-1.0f,  0.0f,  0.0f), //Neg X
        vec3<F32>( 0.0f,  1.0f,  0.0f), //Pos Y
        vec3<F32>( 0.0f, -1.0f,  0.0f), //Neg Y
        vec3<F32>( 0.0f,  0.0f,  1.0f), //Pos Z
        vec3<F32>( 0.0f,  0.0f, -1.0f)  //Neg Z
    };

    vector<Camera*>& probeCameras = SceneEnvironmentProbePool::ProbeCameras();

    std::array<Camera*, 6> cameras = {};
    std::copy_n(std::begin(probeCameras), std::min(cameras.size(), probeCameras.size()), std::begin(cameras));

    for (U8 i = 0; i < 6; ++i) {
        Camera* camera = cameras[i];
        // Set a 90 degree horizontal FoV perspective projection
        camera->setProjection(1.0f, Angle::to_VerticalFoV(Angle::DEGREES<F32>(90.0f), 1.0f), vec2<F32>(0.1f, _aabb.getHalfExtent().length()));
        // Point our camera to the correct face
        camera->lookAt(_aabb.getCenter(), _aabb.getCenter() + TabCenter[i] * _aabb.getHalfExtent().length(), TabUp[i]);
        camera->updateLookAt();
    }

    return cameras;
}

void EnvironmentProbeComponent::loadFromXML(const boost::property_tree::ptree& pt) {
    SGNComponent::loadFromXML(pt);
    updateProbeData();
}

} //namespace Divide