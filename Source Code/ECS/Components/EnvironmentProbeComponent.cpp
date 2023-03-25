#include "stdafx.h"

#include "Headers/EnvironmentProbeComponent.h"
#include "Headers/TransformComponent.h"

#include "Core/Headers/Kernel.h"

#include "Graphs/Headers/SceneGraph.h"
#include "Scenes/Headers/Scene.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Scenes/Headers/SceneShaderData.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"

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
    const Scene& parentScene = sgn->sceneGraph()->parentScene();

    EditorComponentField layerField = {};
    layerField._name = "RT Layer index";
    layerField._data = &_rtLayerIndex;
    layerField._type = EditorComponentFieldType::PUSH_TYPE;
    layerField._readOnly = true;
    layerField._serialise = false;
    layerField._basicType = GFX::PushConstantType::INT;
    layerField._basicTypeSize = GFX::PushConstantSize::WORD;

    editorComponent().registerField(MOV(layerField));

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

    editorComponent().registerField(MOV(typeField));

    EditorComponentField bbField = {};
    bbField._name = "Bounding Box";
    bbField._data = &_refaabb;
    bbField._type = EditorComponentFieldType::BOUNDING_BOX;
    bbField._readOnly = false;
    editorComponent().registerField(MOV(bbField));

    EditorComponentField updateRateField = {};
    updateRateField._name = "Update Rate";
    updateRateField._tooltip = "[0...TARGET_FPS]. Every Nth frame. 0 = disabled;";
    updateRateField._data = &_updateRate;
    updateRateField._type = EditorComponentFieldType::PUSH_TYPE;
    updateRateField._readOnly = false;
    updateRateField._range = { 0.f, Config::TARGET_FRAME_RATE };
    updateRateField._basicType = GFX::PushConstantType::UINT;
    updateRateField._basicTypeSize = GFX::PushConstantSize::BYTE;

    editorComponent().registerField(MOV(updateRateField));

    EditorComponentField updateTypeField = {};
    updateTypeField._name = "Update Type";
    updateTypeField._type = EditorComponentFieldType::DROPDOWN_TYPE;
    updateTypeField._readOnly = false;
    updateTypeField._range = { 0u, to_U8(UpdateType::COUNT) };
    updateTypeField._dataGetter = [this](void* dataOut) {
        *static_cast<U8*>(dataOut) = to_U8(updateType());
    };
    updateTypeField._dataSetter = [this](const void* data) noexcept {
        updateType(*static_cast<const UpdateType*>(data));
    };
    updateTypeField._displayNameGetter = [](const U8 index) noexcept {
        return TypeUtil::EnvProveUpdateTypeToString(static_cast<UpdateType>(index));
    };

    editorComponent().registerField(MOV(updateTypeField));

    EditorComponentField showBoxField = {};
    showBoxField._name = "Show parallax correction AABB";
    showBoxField._data = &_showParallaxAABB;
    showBoxField._type = EditorComponentFieldType::PUSH_TYPE;
    showBoxField._readOnly = false;
    showBoxField._basicType = GFX::PushConstantType::BOOL;

    editorComponent().registerField(MOV(showBoxField)); 

    EditorComponentField updateProbeNowButton = {};
    updateProbeNowButton._name = "Update Now";
    updateProbeNowButton._type = EditorComponentFieldType::BUTTON;
    updateProbeNowButton._readOnly = false; //disabled/enabled
    editorComponent().registerField(MOV(updateProbeNowButton));

    editorComponent().onChangedCbk([this](std::string_view field) {
        if (field == "Update Now") {
            dirty(true);
            queueRefresh();
        } else {
            const vec3<F32> pos = _parentSGN->get<TransformComponent>()->getWorldPosition();
            setBounds(_refaabb.getMin() + pos, _refaabb.getMax() + pos);
        }
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
    if (parentSGN()->parent() != nullptr) {
        SceneGraphNode* parent = parentSGN()->parent();
        while (parent != nullptr) {
            const SceneNodeType type = parent->getNode().type();

            if (type != SceneNodeType::TYPE_TRANSFORM &&
                type != SceneNodeType::TYPE_TRIGGER)
            {
                return parent;
            }

            // Keep walking up
            parent = parent->parent();
        }
    }

    return nullptr;
}

bool EnvironmentProbeComponent::checkCollisionAndQueueUpdate(const BoundingSphere& sphere) noexcept {
    if (!dirty() && updateType() == UpdateType::ON_DIRTY && _aabb.collision(sphere)) {
        dirty(true);
    }

    return dirty();
}

bool EnvironmentProbeComponent::refresh(GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    if (!dirty()) {
        return false;
    }
    dirty(false);
    _queueRefresh = false;

    SceneEnvironmentProbePool::ProbesDirty(true);

    if (updateType() != UpdateType::ALWAYS) {
        rtLayerIndex(SceneEnvironmentProbePool::AllocateSlice(false));
    }

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("EnvironmentProbePass Id: [ %d ]", rtLayerIndex()).c_str(), to_U32(rtLayerIndex())));

    vector<Camera*>& probeCameras = SceneEnvironmentProbePool::ProbeCameras();

    std::array<Camera*, 6> cameras = {};
    std::copy_n(std::begin(probeCameras), std::min(cameras.size(), probeCameras.size()), std::begin(cameras));

    RenderPassParams params = {};
    SetDefaultDrawDescriptor( params );

    params._target = SceneEnvironmentProbePool::ReflectionTarget()._targetID;
    params._sourceNode = findNodeToIgnore();

    // Probes come after reflective nodes in buffer positions and array layers for management reasons (rate of update and so on)
    params._stagePass = { RenderStage::REFLECTION, RenderPassType::COUNT, Config::MAX_REFLECTIVE_NODES_IN_VIEW + rtLayerIndex(), static_cast<RenderStagePass::VariantType>(ReflectorType::CUBE) };

    ClearBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES)));
    params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )]._colour = DefaultColours::BLUE;

    _context.gfx().generateCubeMap(params,
                                   rtLayerIndex(),
                                   _aabb.getCenter(),
                                   vec2<F32>(0.1f, _aabb.getHalfExtent().length()),
                                   bufferInOut,
                                   memCmdInOut,
                                   cameras);
 
    SceneEnvironmentProbePool::ProcessEnvironmentMap(_context.gfx(), rtLayerIndex(), updateType() == UpdateType::ALWAYS, bufferInOut);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    _currentUpdateCall = 0;
    return true;
}

void EnvironmentProbeComponent::enabled(const bool state) {
    Parent::enabled(state);
    const auto sceneData = _context.gfx().sceneData();
    if (sceneData != nullptr) {
        sceneData->probeState(poolIndex(), state);
    }
}

void EnvironmentProbeComponent::updateProbeData() const noexcept {
    const auto sceneData = _context.gfx().sceneData();
    sceneData->probeData(poolIndex(), _aabb.getCenter(), _aabb.getHalfExtent());
}

void EnvironmentProbeComponent::poolIndex(const U16 index) noexcept {
    _poolIndex = index;
    updateProbeData();
}

void EnvironmentProbeComponent::setBounds(const vec3<F32>& min, const vec3<F32>& max) noexcept {
    _aabb.set(min, max);
    updateProbeData();
}

void EnvironmentProbeComponent::setBounds(const vec3<F32>& center, const F32 radius) noexcept {
    _aabb.createFromSphere(center, radius);
    updateProbeData();
}

void EnvironmentProbeComponent::updateType(const UpdateType type) {
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
            if constexpr (Config::Build::IS_DEBUG_BUILD) {
                DIVIDE_UNEXPECTED_CALL();
            }
            // Failed to allocate a slice. Fallback to manual updates
            updateType(UpdateType::ON_DIRTY);
        }
    }
}

F32 EnvironmentProbeComponent::distanceSqTo(const vec3<F32>& pos) const noexcept {
    return _aabb.getCenter().distanceSquared(pos);
}

void EnvironmentProbeComponent::OnData(const ECS::CustomEvent& data) {
    SGNComponent::OnData(data);

    if (data._type == ECS::CustomEvent::Type::TransformUpdated) {
        const vec3<F32> pos = _parentSGN->get<TransformComponent>()->getWorldPosition();
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