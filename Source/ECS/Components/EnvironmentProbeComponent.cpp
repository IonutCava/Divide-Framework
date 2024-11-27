

#include "Headers/EnvironmentProbeComponent.h"
#include "Headers/TransformComponent.h"

#include "Core/Headers/Kernel.h"

#include "Graphs/Headers/SceneGraph.h"
#include "Scenes/Headers/Scene.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/PlatformContext.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
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
    Scene& parentScene = sgn->sceneGraph()->parentScene();

    EditorComponentField layerField = {};
    layerField._name = "RT Layer index";
    layerField._data = &_rtLayerIndex;
    layerField._type = EditorComponentFieldType::PUSH_TYPE;
    layerField._readOnly = true;
    layerField._serialise = false;
    layerField._basicType = PushConstantType::UINT;
    layerField._basicTypeSize = PushConstantSize::WORD;

    editorComponent().registerField(MOV(layerField));

    EditorComponentField typeField = {};
    typeField._name = "Is Local";
    typeField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data)
    {
        *static_cast<bool*>(dataOut) = _probeType == ProbeType::TYPE_LOCAL;
    };
    typeField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data)
    {
        _probeType = *static_cast<const bool*>(data) ? ProbeType::TYPE_LOCAL : ProbeType::TYPE_INFINITE;
    };
    typeField._type = EditorComponentFieldType::PUSH_TYPE;
    typeField._readOnly = false;
    typeField._basicType = PushConstantType::BOOL;

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
    updateRateField._basicType = PushConstantType::UINT;
    updateRateField._basicTypeSize = PushConstantSize::BYTE;

    editorComponent().registerField(MOV(updateRateField));

    EditorComponentField updateTypeField = {};
    updateTypeField._name = "Update Type";
    updateTypeField._type = EditorComponentFieldType::DROPDOWN_TYPE;
    updateTypeField._readOnly = false;
    updateTypeField._range = { 0u, to_U32(UpdateType::COUNT) };
    updateTypeField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data)
    {
        *static_cast<U32*>(dataOut) = to_U32(updateType());
    };
    updateTypeField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) noexcept
    {
        updateType(*static_cast<const UpdateType*>(data));
    };
    updateTypeField._displayNameGetter = [](const U32 index, [[maybe_unused]] void* user_data) noexcept
    {
        return TypeUtil::EnvProveUpdateTypeToString(static_cast<UpdateType>(index));
    };

    editorComponent().registerField(MOV(updateTypeField));

    EditorComponentField showBoxField = {};
    showBoxField._name = "Show parallax correction AABB";
    showBoxField._data = &_showParallaxAABB;
    showBoxField._type = EditorComponentFieldType::PUSH_TYPE;
    showBoxField._readOnly = false;
    showBoxField._basicType = PushConstantType::BOOL;

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
            const float3 pos = _parentSGN->get<TransformComponent>()->getWorldPosition();
            setBounds(_refaabb.getMin() + pos, _refaabb.getMax() + pos);
        }
    });

    Attorney::SceneEnvironmentProbeComponent::registerProbe(&parentScene, this);
    enabled(true);
}

EnvironmentProbeComponent::~EnvironmentProbeComponent()
{
    Attorney::SceneEnvironmentProbeComponent::unregisterProbe(&_parentSGN->sceneGraph()->parentScene(), this);
    enabled(false);
}

SceneGraphNode* EnvironmentProbeComponent::findNodeToIgnore() const noexcept
{
    //If we are not a root-level probe. Avoid rendering our parent and children into the reflection
    if (parentSGN()->parent() != nullptr)
    {
        SceneGraphNode* parent = parentSGN()->parent();
        while (parent != nullptr)
        {
            if ( parent->getNode().type() != SceneNodeType::TYPE_TRANSFORM)
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

    if (updateType() != UpdateType::ALWAYS)
    {
        rtLayerIndex(SceneEnvironmentProbePool::AllocateSlice(false));
        if ( rtLayerIndex() == Config::MAX_REFLECTIVE_PROBES_PER_PASS)
        {
            if constexpr ( Config::Build::IS_DEBUG_BUILD )
            {
                DIVIDE_UNEXPECTED_CALL();
            }

            return false;
        }
    }

    auto cmd = GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut);
    Util::StringFormat( cmd->_scopeName, "EnvironmentProbePass Id: [ {} ]", rtLayerIndex() ),
    cmd->_scopeId = rtLayerIndex();

    RenderPassParams params
    {
        ._sourceNode = findNodeToIgnore(),
        ._target = SceneEnvironmentProbePool::ReflectionTarget()._targetID,
        ._stagePass =
        { 
            ._stage = RenderStage::REFLECTION,
            ._passType = RenderPassType::COUNT,
            ._index = rtLayerIndex(),
            ._variant = static_cast<RenderStagePass::VariantType>(ReflectorType::CUBE)
        },
    };

    params._targetDescriptorPrePass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = false;
    params._clearDescriptorPrePass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
    params._targetDescriptorMainPass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
    params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::BLUE, true };
    params._drawMask &= ~(1u << to_base( RenderPassParams::Flags::DRAW_DYNAMIC_NODES ));
    params._drawMask &= ~(1u << to_base( RenderPassParams::Flags::DRAW_TRANSLUCENT_NODES ));

    _context.gfx().generateCubeMap(params,
                                   rtLayerIndex(),
                                   _aabb.getCenter(),
                                   float2(0.01f, _aabb.getHalfExtent().length()),
                                   bufferInOut,
                                   memCmdInOut);
 

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    SceneEnvironmentProbePool::ProcessEnvironmentMap( rtLayerIndex(), updateType() == UpdateType::ALWAYS );

    _currentUpdateCall = 0;
    return true;
}

bool EnvironmentProbeComponent::enabled() const
{
    return Parent::enabled();
}

void EnvironmentProbeComponent::enabled(const bool state)
{
    Parent::enabled(state);
    const auto& sceneData = _context.gfx().sceneData();
    if (sceneData != nullptr)
    {
        sceneData->probeState(poolIndex(), state);
    }
}

void EnvironmentProbeComponent::updateProbeData() const noexcept {
    const auto& sceneData = _context.gfx().sceneData();
    sceneData->probeData(poolIndex(), _aabb.getCenter(), _aabb.getHalfExtent());
}

void EnvironmentProbeComponent::poolIndex(const U16 index) noexcept {
    _poolIndex = index;
    updateProbeData();
}

void EnvironmentProbeComponent::setBounds(const float3& min, const float3& max) noexcept {
    _aabb.set(min, max);
    updateProbeData();
}

void EnvironmentProbeComponent::setBounds(const float3& center, const F32 radius) noexcept {
    _aabb.createFromSphere(center, radius);
    updateProbeData();
}

void EnvironmentProbeComponent::updateType(const UpdateType type) {
    if (updateType() == type){
        return;
    }

    if (updateType() == UpdateType::ALWAYS && rtLayerIndex() < Config::MAX_REFLECTIVE_PROBES_PER_PASS )
    {
        // Release slice if we switch to on-demand updates
        SceneEnvironmentProbePool::UnlockSlice(rtLayerIndex());
    }

    _updateType = type;
    if (type == UpdateType::ALWAYS)
    {
        const U16 newSlice = SceneEnvironmentProbePool::AllocateSlice(true);
        if (newSlice < Config::MAX_REFLECTIVE_PROBES_PER_PASS )
        {
            rtLayerIndex(newSlice);
        }
        else
        {
            if constexpr (Config::Build::IS_DEBUG_BUILD)
            {
                DIVIDE_UNEXPECTED_CALL();
            }

            // Failed to allocate a slice. Fallback to manual updates
            updateType(UpdateType::ON_DIRTY);
        }
    }
}

F32 EnvironmentProbeComponent::distanceSqTo(const float3& pos) const noexcept {
    return _aabb.getCenter().distanceSquared(pos);
}

void EnvironmentProbeComponent::OnData(const ECS::CustomEvent& data) {
    SGNComponent::OnData(data);

    if (data._type == ECS::CustomEvent::Type::TransformUpdated) {
        const float3 pos = _parentSGN->get<TransformComponent>()->getWorldPosition();
        setBounds(_refaabb.getMin() + pos, _refaabb.getMax() + pos);
    } else if (data._type == ECS::CustomEvent::Type::EntityFlagChanged) {
        const SceneGraphNode::Flags flag = static_cast<SceneGraphNode::Flags>(data._flag);
        if (flag == SceneGraphNode::Flags::SELECTED) {
            _drawImpostor = data._dataPair._first == 1u;
        }
    }
}

void EnvironmentProbeComponent::loadFromXML(const boost::property_tree::ptree& pt) {
    SGNComponent::loadFromXML(pt);
    updateProbeData();
}

} //namespace Divide
