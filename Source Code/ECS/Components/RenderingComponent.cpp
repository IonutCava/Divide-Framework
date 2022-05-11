#include "stdafx.h"

#include "config.h"

#include "Headers/RenderingComponent.h"
#include "Headers/AnimationComponent.h"
#include "Headers/BoundsComponent.h"
#include "Headers/EnvironmentProbeComponent.h"
#include "Headers/TransformComponent.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"

#include "Editor/Headers/Editor.h"

#include "Managers/Headers/SceneManager.h"

#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Scenes/Headers/SceneState.h"

#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"
#include "Rendering/RenderPass/Headers/RenderPassExecutor.h"

namespace Divide {

namespace {
    constexpr U8 MAX_LOD_LEVEL = 4u;

    constexpr I16 g_renderRangeLimit = I16_MAX;
}

RenderingComponent::RenderingComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<RenderingComponent, ComponentType::RENDERING>(parentSGN, context),
      _context(context.gfx()),
      _config(context.config()),
      _reflectionProbeIndex(SceneEnvironmentProbePool::SkyProbeLayerIndex())
{
    _lodLevels.fill(0u);
    _lodLockLevels.fill({ false, to_U8(0u) });
    _renderRange.min =  0.f;
    _renderRange.max =  g_renderRangeLimit;

    instantiateMaterial(parentSGN->getNode().getMaterialTpl());

    toggleRenderOption(RenderOptions::RENDER_GEOMETRY, true);
    toggleRenderOption(RenderOptions::CAST_SHADOWS, true);
    toggleRenderOption(RenderOptions::RECEIVE_SHADOWS, true);
    toggleRenderOption(RenderOptions::IS_VISIBLE, true);

    _showAxis       = renderOptionEnabled(RenderOptions::RENDER_AXIS);
    _receiveShadows = renderOptionEnabled(RenderOptions::RECEIVE_SHADOWS);
    _castsShadows   = renderOptionEnabled(RenderOptions::CAST_SHADOWS);
    {
        EditorComponentField occlusionCullField = {};
        occlusionCullField._name = "HiZ Occlusion Cull";
        occlusionCullField._data = &_occlusionCull;
        occlusionCullField._type = EditorComponentFieldType::SWITCH_TYPE;
        occlusionCullField._basicType = GFX::PushConstantType::BOOL;
        occlusionCullField._readOnly = false;
        _editorComponent.registerField(MOV(occlusionCullField));
    }
    {
        EditorComponentField vaxisField = {};
        vaxisField._name = "Show Debug Axis";
        vaxisField._data = &_showAxis;
        vaxisField._type = EditorComponentFieldType::SWITCH_TYPE;
        vaxisField._basicType = GFX::PushConstantType::BOOL;
        vaxisField._readOnly = false;
        _editorComponent.registerField(MOV(vaxisField));
    }
    {
        EditorComponentField receivesShadowsField = {};
        receivesShadowsField._name = "Receives Shadows";
        receivesShadowsField._data = &_receiveShadows;
        receivesShadowsField._type = EditorComponentFieldType::SWITCH_TYPE;
        receivesShadowsField._basicType = GFX::PushConstantType::BOOL;
        receivesShadowsField._readOnly = false;
        _editorComponent.registerField(MOV(receivesShadowsField));
    }
    {
        EditorComponentField castsShadowsField = {};
        castsShadowsField._name = "Casts Shadows";
        castsShadowsField._data = &_castsShadows;
        castsShadowsField._type = EditorComponentFieldType::SWITCH_TYPE;
        castsShadowsField._basicType = GFX::PushConstantType::BOOL;
        castsShadowsField._readOnly = false;
        _editorComponent.registerField(MOV(castsShadowsField));
    }
    _editorComponent.onChangedCbk([this](const std::string_view field) {
        if (field == "Show Axis") {
            toggleRenderOption(RenderOptions::RENDER_AXIS, _showAxis);
        } else if (field == "Receives Shadows") {
            toggleRenderOption(RenderOptions::RECEIVE_SHADOWS, _receiveShadows);
        } else if (field == "Casts Shadows") {
            toggleRenderOption(RenderOptions::CAST_SHADOWS, _castsShadows);
        }
    });

    const SceneNode& node = _parentSGN->getNode();
    if (node.type() == SceneNodeType::TYPE_OBJECT3D) {
        // Do not cull the sky
        if (static_cast<const Object3D&>(node).type() == SceneNodeType::TYPE_SKY) {
            occlusionCull(false);
        }
    }

    RenderPassExecutor::OnRenderingComponentCreation(this);
}

RenderingComponent::~RenderingComponent()
{
    RenderPassExecutor::OnRenderingComponentDestruction(this);
}

void RenderingComponent::instantiateMaterial(const Material_ptr& material) {
    if (material == nullptr) {
        return;
    }

    _materialInstance = material->clone(_parentSGN->name() + "_i");

    if (_materialInstance != nullptr) {
        assert(!_materialInstance->resourceName().empty());

        EditorComponentField materialField = {};
        materialField._name = "Material";
        materialField._data = _materialInstance.get();
        materialField._type = EditorComponentFieldType::MATERIAL;
        materialField._readOnly = false;
        // should override any existing entry
        _editorComponent.registerField(MOV(materialField));

        EditorComponentField lockLodField = {};
        lockLodField._name = "Rendered LOD Level";
        lockLodField._type = EditorComponentFieldType::PUSH_TYPE;
        lockLodField._basicTypeSize = GFX::PushConstantSize::BYTE;
        lockLodField._basicType = GFX::PushConstantType::UINT;
        lockLodField._data = &_lodLevels[to_base(RenderStage::DISPLAY)];
        lockLodField._readOnly = true;
        lockLodField._serialise = false;
        _editorComponent.registerField(MOV(lockLodField));

        EditorComponentField lockLodLevelField = {};
        lockLodLevelField._name = "Lock LoD Level";
        lockLodLevelField._type = EditorComponentFieldType::PUSH_TYPE;
        lockLodLevelField._range = { 0.0f, to_F32(MAX_LOD_LEVEL) };
        lockLodLevelField._basicType = GFX::PushConstantType::UINT;
        lockLodLevelField._basicTypeSize = GFX::PushConstantSize::BYTE;
        lockLodLevelField._data = &_lodLockLevels[to_base(RenderStage::DISPLAY)].second;
        lockLodLevelField._readOnly = false;
        _editorComponent.registerField(MOV(lockLodLevelField));

        EditorComponentField renderLodField = {};
        renderLodField._name = "Lock LoD";
        renderLodField._type = EditorComponentFieldType::SWITCH_TYPE;
        renderLodField._basicType = GFX::PushConstantType::BOOL;
        renderLodField._data = &_lodLockLevels[to_base(RenderStage::DISPLAY)].first;
        renderLodField._readOnly = false;
        _editorComponent.registerField(MOV(renderLodField));

        _materialInstance->properties().isStatic(_parentSGN->usageContext() == NodeUsageContext::NODE_STATIC);
        _materialInstance->properties().isInstanced(_materialInstance->properties().isInstanced() || isInstanced());
    }
}

void RenderingComponent::setMinRenderRange(const F32 minRange) noexcept {
    _renderRange.min = std::max(minRange, 0.f);
}

void RenderingComponent::setMaxRenderRange(const F32 maxRange) noexcept {
    _renderRange.max = std::min(maxRange,  1.0f * g_renderRangeLimit);
}

void RenderingComponent::clearDrawPackages() {
    SharedLock<SharedMutex> r_lock(_renderPackagesLock);
    for (auto& packagesPerPassType : _renderPackages) {
        for (auto& packagesPerVariant : packagesPerPassType) {
            for (auto& packagesPerPassIndex : packagesPerVariant) {
                for (auto& pacakgesPerIndex : packagesPerPassIndex) {
                    for (PackageEntry& entry : pacakgesPerIndex) {
                        Clear(entry._package);
                    }
                }
            }
        }
    }
}

bool RenderingComponent::canDraw(const RenderStagePass& renderStagePass) {
    OPTICK_EVENT();
    OPTICK_TAG("Node", (_parentSGN->name().c_str()));

    // Can we render without a material? Maybe. IDK.
    bool shaderJustFinishedLoading = false;
    if (_materialInstance == nullptr || _materialInstance->canDraw(renderStagePass, shaderJustFinishedLoading)) {
        if (shaderJustFinishedLoading) {
            _parentSGN->SendEvent({
                ECS::CustomEvent::Type::NewShaderReady,
                this
            });
        }

        return renderOptionEnabled(RenderOptions::IS_VISIBLE);
    }

    return false;
}

void RenderingComponent::onParentUsageChanged(const NodeUsageContext context) const {
    if (_materialInstance != nullptr) {
        _materialInstance->properties().isStatic(context == NodeUsageContext::NODE_STATIC);
    }
}

void RenderingComponent::rebuildMaterial() {
    if (_materialInstance != nullptr) {
        _materialInstance->rebuild();
        rebuildDrawCommands(true);
    }

    const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
    SharedLock<SharedMutex> r_lock(children._lock);
    const U32 childCount = children._count;
    for (U32 i = 0u; i < childCount; ++i) {
        if (children._data[i]->HasComponents(ComponentType::RENDERING)) {
            children._data[i]->get<RenderingComponent>()->rebuildMaterial();
        }
    }
}

void RenderingComponent::setLoDIndexOffset(const U8 lodIndex, size_t indexOffset, size_t indexCount) noexcept {
    if (lodIndex < _lodIndexOffsets.size()) {
        _lodIndexOffsets[lodIndex] = { indexOffset, indexCount };
    }
}

bool RenderingComponent::hasDrawCommands() noexcept {
    SharedLock<SharedMutex> r_lock(_drawCommands._dataLock);
    return !_drawCommands._data.empty();
}

void RenderingComponent::getMaterialData(NodeMaterialData& dataOut) const {
    OPTICK_EVENT();

    if (_materialInstance != nullptr) {
        _materialInstance->getData(*this, _reflectionProbeIndex, dataOut);
    }
}

void RenderingComponent::getMaterialTextures(NodeMaterialTextures& texturesOut, const SamplerAddress defaultTexAddress) const {
    if (_materialInstance != nullptr) {
        _materialInstance->getTextures(*this, texturesOut);
        return;
    }

    texturesOut.fill(TextureToUVec2(defaultTexAddress));
}

/// Called after the current node was rendered
void RenderingComponent::postRender(const SceneRenderState& sceneRenderState, const RenderStagePass& renderStagePass, GFX::CommandBuffer& bufferInOut) {
    if (renderStagePass._stage != RenderStage::DISPLAY ||
        renderStagePass._passType != RenderPassType::MAIN_PASS) 
    {
        return;
    }

    // Draw bounding box if needed and only in the final stage to prevent Shadow/PostFX artifacts
    drawBounds(_drawAABB || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_AABB),
               _drawOBB || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_OBB),
               _drawBS || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_BSPHERES));

    if (renderOptionEnabled(RenderOptions::RENDER_AXIS) ||
        (sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::SELECTION_GIZMO) && _parentSGN->hasFlag(SceneGraphNode::Flags::SELECTED)) ||
        sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::ALL_GIZMOS))
    {
        drawDebugAxis();
    }

    if (renderOptionEnabled(RenderOptions::RENDER_SKELETON) ||
        sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_SKELETONS))
    {
        drawSkeleton();
    }

    if (renderOptionEnabled(RenderOptions::RENDER_SELECTION)) {
        drawSelectionGizmo();
    }
    

    SceneGraphNode* parent = _parentSGN->parent();
    if (parent != nullptr && !parent->hasFlag(SceneGraphNode::Flags::PARENT_POST_RENDERED)) {
        parent->setFlag(SceneGraphNode::Flags::PARENT_POST_RENDERED);
        if (parent->HasComponents(ComponentType::RENDERING)) {
            parent->get<RenderingComponent>()->postRender(sceneRenderState, renderStagePass, bufferInOut);
        }
    }
}

U8 RenderingComponent::getLoDLevel(const RenderStage renderStage) const noexcept {
    const auto& [_, level] = _lodLockLevels[to_base(renderStage)];
    return CLAMPED(level, to_U8(0u), MAX_LOD_LEVEL);
}

U8 RenderingComponent::getLoDLevel(const F32 distSQtoCenter, const RenderStage renderStage, const vec4<U16>& lodThresholds) {
    return getLoDLevelInternal(distSQtoCenter, renderStage, lodThresholds);
}

U8 RenderingComponent::getLoDLevelInternal(const F32 distSQtoCenter, const RenderStage renderStage, const vec4<U16>& lodThresholds) {
    const auto&[state, level] = _lodLockLevels[to_base(renderStage)];

    if (state) {
        return CLAMPED(level, to_U8(0u), MAX_LOD_LEVEL);
    }

    const F32 distSQtoCenterClamped = std::max(distSQtoCenter, std::numeric_limits<F32>::epsilon());
    for (U8 i = 0u; i < MAX_LOD_LEVEL; ++i) {
        if (distSQtoCenterClamped <= to_F32(SQUARED(lodThresholds[i]))) {
            return i;
        }
    }

    return MAX_LOD_LEVEL;
}

bool RenderingComponent::prepareDrawPackage(const CameraSnapshot& cameraSnapshot,
                                            const SceneRenderState& sceneRenderState,
                                            const RenderStagePass& renderStagePass,
                                            const bool refreshData)
{
    OPTICK_EVENT();

    bool hasCommands = hasDrawCommands();

    if (refreshData) {
        U8 drawCmdOptions = 0u;
        ToggleBit(drawCmdOptions, CmdRenderOptions::RENDER_GEOMETRY, (renderOptionEnabled(RenderOptions::RENDER_GEOMETRY) && sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY)));
        ToggleBit(drawCmdOptions, CmdRenderOptions::RENDER_WIREFRAME, (renderOptionEnabled(RenderOptions::RENDER_WIREFRAME) || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME)));

        if (!hasCommands) {
            ScopedLock<SharedMutex> w_lock(_drawCommands._dataLock);
            _parentSGN->getNode().buildDrawCommands(_parentSGN, _drawCommands._data);
            for (GFX::DrawCommand& drawCmd : _drawCommands._data) {
                for (GenericDrawCommand& cmd : drawCmd._drawCommands) {
                    hasCommands = true;
                    cmd._renderOptions = drawCmdOptions;
                    if (cmd._cmd.primCount > 1) {
                        isInstanced(true);
                    }
                }
            }
        } else {
            ScopedLock<SharedMutex> w_lock(_drawCommands._dataLock);
            for (GFX::DrawCommand& drawCmd : _drawCommands._data) {
                for (GenericDrawCommand& cmd : drawCmd._drawCommands) {
                    cmd._renderOptions = drawCmdOptions;
                }
            }
        }

        if (hasCommands) {
            const BoundsComponent* bComp = _parentSGN->get<BoundsComponent>();
            const vec3<F32>& cameraEye = cameraSnapshot._eye;
            const SceneNodeRenderState& renderState = _parentSGN->getNode<>().renderState();
            if (renderState.lod0OnCollision() && bComp->getBoundingBox().containsPoint(cameraEye)) {
                _lodLevels[to_base(renderStagePass._stage)] = 0u;
            } else {
                const BoundingBox& aabb = bComp->getBoundingBox();
                const vec3<F32> LoDtarget = renderState.useBoundsCenterForLoD() ? aabb.getCenter() : aabb.nearestPoint(cameraEye);
                const F32 distanceSQToCenter = LoDtarget.distanceSquared(cameraEye);
                _lodLevels[to_base(renderStagePass._stage)] = getLoDLevelInternal(distanceSQToCenter, renderStagePass._stage, sceneRenderState.lodThresholds(renderStagePass._stage));
            }
        }
    }

    if (hasCommands) {
        RenderPackage& pkg = getDrawPackage(renderStagePass);
        if (pkg.pipelineCmd()._pipeline == nullptr) {
            if (isInstanced()) {
                pkg.pushConstantsCmd()._constants.set(_ID("INDIRECT_DATA_IDX"), GFX::PushConstantType::UINT, 0u);
                if (_materialInstance != nullptr) {
                    _materialInstance->properties().isInstanced(true);
                }
            }
            PipelineDescriptor pipelineDescriptor = {};

            if (_materialInstance != nullptr) {
                pipelineDescriptor._stateHash = _materialInstance->getOrCreateRenderStateBlock(renderStagePass);
                pipelineDescriptor._shaderProgramHandle = _materialInstance->getProgramHandle(renderStagePass);
                pkg.descriptorSetCmd()._set = _materialInstance->getTextureData(renderStagePass);
            } else {
                pipelineDescriptor._stateHash = _context.getDefaultStateBlock(false);
                pipelineDescriptor._shaderProgramHandle = _context.defaultIMShaderWorld()->handle();
            }
            if (renderStagePass._passType == RenderPassType::TRANSPARENCY_PASS) {
                BlendingSettings& state0 = pipelineDescriptor._blendStates._settings[to_U8(GFXDevice::ScreenTargets::ALBEDO)];
                state0.enabled(true);
                state0.blendSrc(BlendProperty::SRC_ALPHA);
                state0.blendDest(BlendProperty::INV_SRC_ALPHA);
                state0.blendOp(BlendOperation::ADD);
            } else if (renderStagePass._passType == RenderPassType::OIT_PASS) {
                BlendingSettings& state0 = pipelineDescriptor._blendStates._settings[to_U8(GFXDevice::ScreenTargets::ACCUMULATION)];
                state0.enabled(true);
                state0.blendSrc(BlendProperty::ONE);
                state0.blendDest(BlendProperty::ONE);
                state0.blendOp(BlendOperation::ADD);

                BlendingSettings& state1 = pipelineDescriptor._blendStates._settings[to_U8(GFXDevice::ScreenTargets::REVEALAGE)];
                state1.enabled(true);
                state1.blendSrc(BlendProperty::ZERO);
                state1.blendDest(BlendProperty::INV_SRC_COLOR);
                state1.blendOp(BlendOperation::ADD);

                if_constexpr(Config::USE_COLOURED_WOIT) {
                    BlendingSettings& state2 = pipelineDescriptor._blendStates._settings[to_U8(GFXDevice::ScreenTargets::MODULATE)];
                    state2.enabled(true);
                    state2.blendSrc(BlendProperty::ONE);
                    state2.blendDest(BlendProperty::ONE);
                    state2.blendOp(BlendOperation::ADD);
                }
            }

            pkg.pipelineCmd()._pipeline = _context.newPipeline(pipelineDescriptor);
        }

        Attorney::SceneGraphNodeComponent::prepareRender(_parentSGN, *this, cameraSnapshot, renderStagePass, refreshData);
    }

    return hasCommands;
}

void RenderingComponent::retrieveDrawCommands(const RenderStagePass& stagePass, const U32 cmdOffset, DrawCommandContainer& cmdsInOut) {
    OPTICK_EVENT();

    const U32 iBufferEntry = _indirectionBufferEntry;

    {
        RenderPackage& pkg = getDrawPackage(stagePass);
        if (isInstanced()) {
            pkg.pushConstantsCmd()._constants.set(_ID("INDIRECT_DATA_IDX"), GFX::PushConstantType::UINT, iBufferEntry);
        }
        pkg.stagePassBaseIndex(BaseIndex(stagePass));

        pkg.drawCmdOffset(cmdOffset + to_U32(cmdsInOut.size()));
        
    }

    const auto& [offset, count] = _lodIndexOffsets[std::min(_lodLevels[to_U8(stagePass._stage)], to_U8(_lodIndexOffsets.size() - 1))];
    const bool autoIndex = offset != 0u || count != 0u;
    {
        SharedLock<SharedMutex> r_lock2(_drawCommands._dataLock);
        for (const GFX::DrawCommand& drawCmd : _drawCommands._data) {
            for (const GenericDrawCommand& gCmd : drawCmd._drawCommands) {
                cmdsInOut.push_back(gCmd._cmd);
                IndirectDrawCommand& iCmd = cmdsInOut.back();
                iCmd.baseInstance = isInstanced() ? 0u : (iBufferEntry + 1u); //Make sure to substract 1 in the shader!
                if (autoIndex) {
                    iCmd.firstIndex = to_U32(offset);
                    iCmd.indexCount = to_U32(count);
                }
            }
        }
    }
}

void RenderingComponent::getCommandBuffer(RenderPackage* const pkg, GFX::CommandBuffer& bufferInOut) {
    if (pkg->additionalCommands() != nullptr) {
        bufferInOut.add(*pkg->additionalCommands());
    }

    bufferInOut.add(pkg->pipelineCmd());
    bufferInOut.add(pkg->descriptorSetCmd());
    bufferInOut.add(pkg->pushConstantsCmd());
    {
        U32 startOffset = pkg->drawCmdOffset();

        SharedLock<SharedMutex> r_lock(_drawCommands._dataLock);
        for (const GFX::DrawCommand& drawCmd : _drawCommands._data) {
            GFX::DrawCommand* cmd = bufferInOut.add(drawCmd);
            for (GenericDrawCommand& gCmd : cmd->_drawCommands) {
                gCmd._commandOffset = startOffset++;
                if (gCmd._bufferIndex == GenericDrawCommand::INVALID_BUFFER_INDEX) {
                    gCmd._bufferIndex = 0u;
                }
            }
        }
    }
}

DescriptorSet& RenderingComponent::getDescriptorSet(const RenderStagePass& renderStagePass) {
    return getDrawPackage(renderStagePass).descriptorSetCmd()._set;
}

PushConstants& RenderingComponent::getPushConstants(const RenderStagePass& renderStagePass) {
    return getDrawPackage(renderStagePass).pushConstantsCmd()._constants;
}

void RenderingComponent::addAdditionalCommands(const RenderStagePass& renderStagePass, GFX::CommandBuffer* cmdBuffer) {
    getDrawPackage(renderStagePass).additionalCommands(cmdBuffer);
}

size_t RenderingComponent::getPipelineHash(const RenderStagePass& renderStagePass) {
    const Pipeline* pipeline = getDrawPackage(renderStagePass).pipelineCmd()._pipeline;
    return (pipeline != nullptr ? pipeline->hash() : 0u);
}

RenderPackage& RenderingComponent::getDrawPackage(const RenderStagePass& renderStagePass) {
    PackagesPerPassType&  packagesPerPassType  = _renderPackages[to_base(renderStagePass._stage)];
    PackagesPerVariant&   packagesPerVariant   = packagesPerPassType[to_base(renderStagePass._passType)];
    PackagesPerPassIndex& packagesPerPassIndex = packagesPerVariant[to_base(renderStagePass._variant)];
    PackagesPerIndex&     pacakgesPerIndex     = packagesPerPassIndex[to_base(renderStagePass._pass)];

    {
        SharedLock<SharedMutex> r_lock(_renderPackagesLock);
        for (PackageEntry& entry : pacakgesPerIndex) {
            if (entry._index == renderStagePass._index) {
                return entry._package;
            }
        }
    }

    ScopedLock<SharedMutex> w_lock(_renderPackagesLock);
    // check again
    for (PackageEntry& entry : pacakgesPerIndex) {
        if (entry._index == renderStagePass._index) {
            return entry._package;
        }
    }

    PackageEntry& entry = pacakgesPerIndex.emplace_back();
    entry._index = renderStagePass._index;
    return entry._package;
}

bool RenderingComponent::updateReflection(const U16 reflectionIndex,
                                          const bool inBudget,
                                          Camera* camera,
                                          const SceneRenderState& renderState,
                                          GFX::CommandBuffer& bufferInOut,
                                          GFX::MemoryBarrierCommand& memCmdInOut)
{
    if (_materialInstance == nullptr) {
        return false;
    }

    //Target texture: the opposite of what we bind during the regular passes
    if (_reflectorType != ReflectorType::COUNT && _reflectionCallback && inBudget) {
        const RenderTargetID reflectRTID(_reflectorType == ReflectorType::PLANAR 
                                                         ? RenderTargetNames::REFLECTION_PLANAR[reflectionIndex]
                                                         : RenderTargetNames::REFLECTION_CUBE);

        RenderPassManager* passManager = _context.parent().renderPassManager();
        RenderCbkParams params{ _context, _parentSGN, renderState, reflectRTID, reflectionIndex, to_U8(_reflectorType), camera };
        _reflectionCallback(passManager, params, bufferInOut, memCmdInOut);

        const RTAttachment& targetAtt = _context.renderTargetPool().getRenderTarget(reflectRTID)->getAttachment(RTAttachmentType::Colour, 0u);
        _materialInstance->setTexture(
            _reflectorType == ReflectorType::PLANAR ? TextureUsage::REFLECTION_PLANAR : TextureUsage::REFLECTION_CUBE,
            targetAtt.texture(),
            targetAtt.samplerHash(),
            TextureOperation::REPLACE,
            TexturePrePassUsage::AUTO
        );
        return true;
    }
    // No need to clear the reflection texture (if there is one) as an outdated reflection is better than random artefacts
    return false;
}

bool RenderingComponent::updateRefraction(const U16 refractionIndex,
                                          const bool inBudget,
                                          Camera* camera,
                                          const SceneRenderState& renderState,
                                          GFX::CommandBuffer& bufferInOut,
                                          GFX::MemoryBarrierCommand& memCmdInOut)
{
    if (_materialInstance == nullptr) {
        return false;
    }

    // no default refraction system!
    if (_refractorType != RefractorType::COUNT && _refractionCallback && inBudget) {
        const RenderTargetID refractRTID(RenderTargetNames::REFRACTION_PLANAR[refractionIndex]);

        // Only planar for now
        assert(_refractorType == RefractorType::PLANAR);

        RenderPassManager* passManager = _context.parent().renderPassManager();
        RenderCbkParams params{ _context, _parentSGN, renderState, refractRTID, refractionIndex, 0u, camera };
        _refractionCallback(passManager, params, bufferInOut, memCmdInOut);

        const RTAttachment& targetAtt = _context.renderTargetPool().getRenderTarget(refractRTID)->getAttachment(RTAttachmentType::Colour, 0u);
        _materialInstance->setTexture(
            TextureUsage::REFRACTION_PLANAR,
            targetAtt.texture(),
            targetAtt.samplerHash(),
            TextureOperation::REPLACE,
            TexturePrePassUsage::AUTO
        );
        return true;
    }

    return false;
}

void RenderingComponent::updateNearestProbes(const vec3<F32>& position) {
    _envProbes.resize(0);
    _reflectionProbeIndex = SceneEnvironmentProbePool::SkyProbeLayerIndex();

    const SceneEnvironmentProbePool* probePool = _context.context().kernel().sceneManager()->getEnvProbes();
    if (probePool != nullptr) {
        probePool->lockProbeList();
        const auto& probes = probePool->getLocked();
        _envProbes.reserve(probes.size());

        U8 idx = 0u;
        for (const auto& probe : probes) {
            if (++idx == Config::MAX_REFLECTIVE_PROBES_PER_PASS) {
                break;
            }
            _envProbes.push_back(probe);
        }
        probePool->unlockProbeList();

        if (idx > 0u) {
            eastl::sort(begin(_envProbes),
                        end(_envProbes),
                        [&position](const auto& a, const auto& b) noexcept -> bool {
                            return a->distanceSqTo(position) < b->distanceSqTo(position);
                        });

            // We need to update this probe because we are going to use it. This will always lag one frame, but at least we keep updates separate from renders.
            for (EnvironmentProbeComponent* probe : _envProbes) {
                if (probe->getBounds().containsPoint(position)) {
                    _reflectionProbeIndex = probe->poolIndex();
                    probe->queueRefresh();
                    break;
                }
            }
        }
    }
}

/// Draw some kind of selection doodad. May differ if editor is running or not
void RenderingComponent::drawSelectionGizmo() {
    if (_selectionGizmoDirty) {
        _selectionGizmoDirty = false;

        UColour4 colour = UColour4(64, 255, 128, 255);
        if_constexpr(Config::Build::ENABLE_EDITOR) {
            if (_context.parent().platformContext().editor().inEditMode()) {
                colour = UColour4(255, 255, 255, 255);
            }
        }
        //draw something else (at some point ...)
        BoundsComponent* bComp = _parentSGN->get<BoundsComponent>();
        DIVIDE_ASSERT(bComp != nullptr);
        _selectionGizmoDescriptor.box = bComp->getOBB();
        _selectionGizmoDescriptor.colour = colour;
    }

    _context.debugDrawOBB(_parentSGN->getGUID() + 12345, _selectionGizmoDescriptor);
}

/// Draw the axis arrow gizmo
void RenderingComponent::drawDebugAxis() {
    if (_axisGizmoLinesDescriptor._lines.empty()) {
        Line temp = {};
        temp.widthStart(10.0f);
        temp.widthEnd(10.0f);
        temp.positionStart(VECTOR3_ZERO);

        // Red X-axis
        temp.positionEnd(WORLD_X_AXIS * 4);
        temp.colourStart(UColour4(255, 0, 0, 255));
        temp.colourEnd(UColour4(255, 0, 0, 255));
        _axisGizmoLinesDescriptor._lines.push_back(temp);

        // Green Y-axis
        temp.positionEnd(WORLD_Y_AXIS * 4);
        temp.colourStart(UColour4(0, 255, 0, 255));
        temp.colourEnd(UColour4(0, 255, 0, 255));
        _axisGizmoLinesDescriptor._lines.push_back(temp);

        // Blue Z-axis
        temp.positionEnd(WORLD_Z_AXIS * 4);
        temp.colourStart(UColour4(0, 0, 255, 255));
        temp.colourEnd(UColour4(0, 0, 255, 255));
        _axisGizmoLinesDescriptor._lines.push_back(temp);

        mat4<F32> worldOffsetMatrixCache(GetMatrix(_parentSGN->get<TransformComponent>()->getWorldOrientation()), false);
        worldOffsetMatrixCache.setTranslation(_parentSGN->get<TransformComponent>()->getWorldPosition());
        _axisGizmoLinesDescriptor.worldMatrix = worldOffsetMatrixCache;
    }

    _context.debugDrawLines(_parentSGN->getGUID() + 321, _axisGizmoLinesDescriptor);
}

void RenderingComponent::drawSkeleton() {
    const SceneNode& node = _parentSGN->getNode();
    const bool isSubMesh = node.type() == SceneNodeType::TYPE_OBJECT3D && static_cast<const Object3D&>(node).geometryType() == ObjectType::SUBMESH;
    if (!isSubMesh) {
        return;
    }

    // Get the animation component of any submesh. They should be synced anyway.
    if (_parentSGN->HasComponents(ComponentType::ANIMATION)) {
        // Get the skeleton lines from the submesh's animation component
        _skeletonLinesDescriptor._lines = _parentSGN->get<AnimationComponent>()->skeletonLines();
        _skeletonLinesDescriptor.worldMatrix.set(_parentSGN->get<TransformComponent>()->getWorldMatrix());
        // Submit the skeleton lines to the GPU for rendering
        _context.debugDrawLines(_parentSGN->getGUID() + 213, _skeletonLinesDescriptor);
    }
}

void RenderingComponent::drawBounds(const bool AABB, const bool OBB, const bool Sphere) {
    if (!AABB && !Sphere && !OBB) {
        return;
    }

    const SceneNode& node = _parentSGN->getNode();
    const bool isSubMesh = node.type() == SceneNodeType::TYPE_OBJECT3D && static_cast<const Object3D&>(node).geometryType() == ObjectType::SUBMESH;

    if (AABB) {
        const BoundingBox& bb = _parentSGN->get<BoundsComponent>()->getBoundingBox();
        IMPrimitive::BoxDescriptor descriptor;
        descriptor.min = bb.getMin();
        descriptor.max = bb.getMax();
        descriptor.colour = isSubMesh ? UColour4(0, 0, 255, 255) : UColour4(255, 0, 255, 255);
        _context.debugDrawBox(_parentSGN->getGUID() + 123, descriptor);
    }

    if (OBB) {
        const auto& obb = _parentSGN->get<BoundsComponent>()->getOBB();
        IMPrimitive::OBBDescriptor descriptor;
        descriptor.box = obb;
        descriptor.colour = isSubMesh ? UColour4(128, 0, 255, 255) : UColour4(255, 0, 128, 255);

        _context.debugDrawOBB(_parentSGN->getGUID() + 123, descriptor);
    }

    if (Sphere) {
        const BoundingSphere& bs = _parentSGN->get<BoundsComponent>()->getBoundingSphere();
        IMPrimitive::SphereDescriptor descriptor;
        descriptor.center = bs.getCenter();
        descriptor.radius = bs.getRadius();
        descriptor.colour = isSubMesh ? UColour4(0, 255, 0, 255) : UColour4(255, 255, 0, 255);
        descriptor.slices = 16u;
        descriptor.stacks = 16u;
        _context.debugDrawSphere(_parentSGN->getGUID() + 123, descriptor);
    }
}

void RenderingComponent::OnData(const ECS::CustomEvent& data) {
    SGNComponent::OnData(data);

    switch (data._type) {
        case  ECS::CustomEvent::Type::TransformUpdated:
        {
            const TransformComponent* tComp = static_cast<TransformComponent*>(data._sourceCmp);
            assert(tComp != nullptr);
            updateNearestProbes(tComp->getWorldPosition());

            _axisGizmoLinesDescriptor.worldMatrix.set(mat4<F32>(GetMatrix(tComp->getWorldOrientation()), false));
            _axisGizmoLinesDescriptor.worldMatrix.setTranslation(tComp->getWorldPosition());
        } break;
        case ECS::CustomEvent::Type::DrawBoundsChanged:
        {
            const BoundsComponent* bComp = static_cast<BoundsComponent*>(data._sourceCmp);
            toggleBoundsDraw(bComp->showAABB(), bComp->showBS(), bComp->showOBB(), false);
        } break;
        case ECS::CustomEvent::Type::BoundsUpdated:
        {
            _selectionGizmoDirty = true;
        } break;
        default: break;
    }
}
}
