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

namespace Divide {

namespace {
    constexpr U8 MAX_LOD_LEVEL = 4u;

    constexpr I16 g_renderRangeLimit = std::numeric_limits<I16>::max();
}

RenderingComponent::RenderingComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<RenderingComponent, ComponentType::RENDERING>(parentSGN, context),
      _context(context.gfx()),
      _config(context.config())
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
        vaxisField._name = "Show Axis";
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

    RenderStateBlock primitiveStateBlock = {};
    PipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor._stateHash = primitiveStateBlock.getHash();
    pipelineDescriptor._shaderProgramHandle = ShaderProgram::DefaultShader()->getGUID();
    _primitivePipeline[0] = _context.newPipeline(pipelineDescriptor);

    const SceneNode& node = _parentSGN->getNode();
    if (node.type() == SceneNodeType::TYPE_OBJECT3D) {
        // Do not cull the sky
        if (static_cast<const Object3D&>(node).type() == SceneNodeType::TYPE_SKY) {
            occlusionCull(false);
        }
        // Prepare it for rendering lines
        if (static_cast<const Object3D&>(node).getObjectFlag(Object3D::ObjectFlag::OBJECT_FLAG_SKINNED)) {
            RenderStateBlock primitiveStateBlockNoZRead = {};
            primitiveStateBlockNoZRead.depthTestEnabled(false);
            pipelineDescriptor._stateHash = primitiveStateBlockNoZRead.getHash();
            _primitivePipeline[1] = _context.newPipeline(pipelineDescriptor);
        }
    }

    if_constexpr (Config::Build::ENABLE_EDITOR) {
        // Prepare it for line rendering
        pipelineDescriptor._stateHash = _context.getDefaultStateBlock(true);
        _primitivePipeline[2] = _context.newPipeline(pipelineDescriptor);
    }

    for (U8 s = 0; s < to_U8(RenderStage::COUNT); ++s) {
        const U8 count = RenderStagePass::totalPassCountForStage(static_cast<RenderStage>(s));
        if (s == to_U8(RenderStage::SHADOW)) {
            _renderPackages[s][to_base(RenderPassType::MAIN_PASS)].resize(count);
            _rebuildDrawCommandsFlags[s][to_base(RenderPassType::MAIN_PASS)].fill(true);
        } else {
            PackagesPerPassType & perPassPkgs = _renderPackages[s];
            FlagsPerPassType & perPassFlags = _rebuildDrawCommandsFlags[s];
            for (U8 p = 0; p < to_U8(RenderPassType::COUNT); ++p) {
                perPassPkgs[p].resize(count);
                perPassFlags[p].fill(true);
            }
        }
    }
}

RenderingComponent::~RenderingComponent()
{
    if (_boundingBoxPrimitive) {
        _context.destroyIMP(_boundingBoxPrimitive);
    }
    if (_orientedBoundingBoxPrimitive) {
        _context.destroyIMP(_orientedBoundingBoxPrimitive);
    }
    if (_boundingSpherePrimitive) {
        _context.destroyIMP(_boundingSpherePrimitive);
    }

    if (_skeletonPrimitive) {
        _context.destroyIMP(_skeletonPrimitive);
    }

    if (_axisGizmo) {
        _context.destroyIMP(_axisGizmo);
    }

    if (_selectionGizmo) {
        _context.destroyIMP(_selectionGizmo);
    }
}

void RenderingComponent::instantiateMaterial(const Material_ptr& material) {
    if (material == nullptr) {
        return;
    }

    _materialInstance = material->clone(_parentSGN->name() + "_i");
    _materialInstance->usePlanarReflections(_reflectorType == ReflectorType::PLANAR);
    _materialInstance->usePlanarRefractions(_refractorType == RefractorType::PLANAR);

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

        EditorComponentField renderLodField = {};
        renderLodField._name = "Lock LoD";
        renderLodField._type = EditorComponentFieldType::SWITCH_TYPE;
        renderLodField._basicType = GFX::PushConstantType::BOOL;
        renderLodField._data = &_lodLockLevels[to_base(RenderStage::DISPLAY)].first;
        renderLodField._readOnly = false;
        _editorComponent.registerField(MOV(renderLodField));

        EditorComponentField lockLodLevelField = {};
        lockLodLevelField._name = "Lock LoD Level";
        lockLodLevelField._type = EditorComponentFieldType::PUSH_TYPE;
        lockLodLevelField._range = { 0.0f, to_F32(MAX_LOD_LEVEL) };
        lockLodLevelField._basicType = GFX::PushConstantType::UINT;
        lockLodLevelField._basicTypeSize = GFX::PushConstantSize::BYTE;
        lockLodLevelField._data = &_lodLockLevels[to_base(RenderStage::DISPLAY)].second;
        lockLodLevelField._readOnly = false;
        _editorComponent.registerField(MOV(lockLodLevelField));

        _materialInstance->isStatic(_parentSGN->usageContext() == NodeUsageContext::NODE_STATIC);
    }
}

void RenderingComponent::setMinRenderRange(const F32 minRange) noexcept {
    _renderRange.min = std::max(minRange, 0.f);
}

void RenderingComponent::setMaxRenderRange(const F32 maxRange) noexcept {
    _renderRange.max = std::min(maxRange,  1.0f * g_renderRangeLimit);
}

void RenderingComponent::rebuildDrawCommands(const RenderStagePass& stagePass, const Camera& crtCamera, RenderPackage& pkg) const {
    OPTICK_EVENT();
    pkg.clear();

    // The following commands are needed for material rendering
    // In the absence of a material, use the SceneNode buildDrawCommands to add all of the needed commands
    if (_materialInstance != nullptr) {
        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._stateHash = _materialInstance->getRenderStateBlock(stagePass);
        pipelineDescriptor._shaderProgramHandle = _materialInstance->getProgramGUID(stagePass);

        pkg.add(GFX::BindPipelineCommand{ _context.newPipeline(pipelineDescriptor) });

        GFX::BindDescriptorSetsCommand bindDescriptorSetsCommand = {};
        for (const ShaderBufferBinding binding : getShaderBuffers()) {
            bindDescriptorSetsCommand._set._buffers.add(binding);
        }
        pkg.add(bindDescriptorSetsCommand);
    }

    _parentSGN->getNode().buildDrawCommands(_parentSGN, stagePass, crtCamera, pkg);
}

void RenderingComponent::onMaterialChanged() {
    OPTICK_EVENT();

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        if (s == to_U8(RenderStage::SHADOW)) {
            continue;
        }
        PackagesPerPassType& perPassPkg = _renderPackages[s];
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            PackagesPerIndex& perIndexPkg = perPassPkg[p];
            for (RenderPackage& pkg : perIndexPkg) {
                pkg.textureDataDirty(true);
            }
        }
    }
    _parentSGN->getNode().rebuildDrawCommands(true);
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
        _materialInstance->isStatic(context == NodeUsageContext::NODE_STATIC);
    }
}

void RenderingComponent::rebuildMaterial() {
    if (_materialInstance != nullptr) {
        _materialInstance->rebuild();
        onMaterialChanged();
    }

    _parentSGN->forEachChild([](const SceneGraphNode* child, I32 /*childIdx*/) {
        RenderingComponent* const renderable = child->get<RenderingComponent>();
        if (renderable) {
            renderable->rebuildMaterial();
        }
        return true;
    });
}

void RenderingComponent::setReflectionAndRefractionType(const ReflectorType reflectType, const RefractorType refractType) noexcept { 
    if (_reflectorType != reflectType) {
        _reflectorType = reflectType;
        if (_materialInstance != nullptr) {
            _materialInstance->usePlanarReflections(_reflectorType == ReflectorType::PLANAR);
        }
    }

    if (_refractorType != refractType) {
        _refractorType = refractType;
        if (_materialInstance != nullptr) {
            _materialInstance->usePlanarRefractions(_refractorType == RefractorType::PLANAR);
        }
    }
}

void RenderingComponent::retrieveDrawCommands(const RenderStagePass& stagePass, const U32 cmdOffset, DrawCommandContainer& cmdsInOut) {
    OPTICK_EVENT();

    const U8 stageIdx = to_U8(stagePass._stage);
    const U8 lodLevel = _lodLevels[stageIdx];
    const U16 pkgIndex = RenderStagePass::indexForStage(stagePass);
    const U16 pkgCount = RenderStagePass::passCountForStagePass(stagePass);
    const NodeDataIdx dataIdx = _lastDataIndex[stageIdx];

    PackagesPerIndex& packages = _renderPackages[stageIdx][to_U8(stagePass._passType)];
    U32 startCmdOffset = cmdOffset + to_U32(cmdsInOut.size());
    for (U16 i = 0; i < pkgCount; ++i) {
        RenderPackage& pkg = packages[i + pkgIndex];
        startCmdOffset += Attorney::RenderPackageRenderingComponent::updateAndRetrieveDrawCommands(pkg, dataIdx, startCmdOffset, lodLevel, cmdsInOut);
    }
}

bool RenderingComponent::hasDrawCommands(const RenderStagePass& stagePass) {
    return getDrawPackage(stagePass).count<GFX::DrawCommand>() > 0u;
}

void RenderingComponent::getMaterialData(NodeMaterialData& dataOut, NodeMaterialTextures& texturesOut) const {
    OPTICK_EVENT();

    if (_materialInstance != nullptr) {
        //ProbeID: 0u = sky cubemap. Shader: (Probe - 1u) = environment cube array index and probe data lookup index
        _materialInstance->getData(*this, _reflectionProbeIndex + 1u, dataOut, texturesOut);
        for (U8 i = 0u; i < MATERIAL_TEXTURE_COUNT; ++i) {
            // We don't overwrite custom reflection/refraction textures!
            if (texturesOut[i] == 0u) {
                if (g_materialTextures[i] == TextureUsage::REFLECTION) {
                    const ReflectRefractData& refData = _reflectRefractData[to_base(DataType::REFLECT)];
                    SharedLock<SharedMutex> r_lock(refData._lock);
                    texturesOut[i] = refData._gpuAddress;
                } else if (g_materialTextures[i] == TextureUsage::REFRACTION) {
                    const ReflectRefractData& refData = _reflectRefractData[to_base(DataType::REFRACT)];
                    SharedLock<SharedMutex> r_lock(refData._lock);
                    texturesOut[i] = refData._gpuAddress;
                }
            }
        }
    }
}

void RenderingComponent::getRenderingProperties(const RenderStage stage, NodeRenderingProperties& propertiesOut) const {
    propertiesOut._lod = _lodLevels[to_base(stage)];
    propertiesOut._nodeFlagValue = dataFlag();
    propertiesOut._occlusionCull = occlusionCull();
}

/// Called after the current node was rendered
void RenderingComponent::postRender(const SceneRenderState& sceneRenderState, const RenderStagePass& renderStagePass, GFX::CommandBuffer& bufferInOut) {
    if (renderStagePass._stage != RenderStage::DISPLAY || renderStagePass._passType == RenderPassType::PRE_PASS) {
        return;
    }

    // Draw bounding box if needed and only in the final stage to prevent Shadow/PostFX artifacts
    const bool renderBBox = _drawAABB || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_AABB);
    const bool renderOBB = _drawOBB || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_OBB);
    const bool renderBSphere = _drawBS || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_BSPHERES);
    const bool renderSkeleton = renderOptionEnabled(RenderOptions::RENDER_SKELETON) || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_SKELETONS);
    const bool renderSelection = renderOptionEnabled(RenderOptions::RENDER_SELECTION);
    const bool renderSelectionGizmo = renderOptionEnabled(RenderOptions::RENDER_AXIS) || (sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::SELECTION_GIZMO) && _parentSGN->hasFlag(SceneGraphNode::Flags::SELECTED)) || sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::ALL_GIZMOS);

    if (renderSelectionGizmo) {
        drawDebugAxis(bufferInOut);
    } else if (_axisGizmo) {
        _context.destroyIMP(_axisGizmo);
    }
    if (renderSelection) {
        drawSelectionGizmo(bufferInOut);
    } else if (_selectionGizmo) {
        _context.destroyIMP(_selectionGizmo);
    }

    drawBounds(renderBBox, renderOBB, renderBSphere, bufferInOut);
    if (renderSkeleton) {
        drawSkeleton(bufferInOut);
    }

    SceneGraphNode* parent = _parentSGN->parent();
    if (parent != nullptr && !parent->hasFlag(SceneGraphNode::Flags::PARENT_POST_RENDERED)) {
        parent->setFlag(SceneGraphNode::Flags::PARENT_POST_RENDERED);
        RenderingComponent* rComp = parent->get<RenderingComponent>();
        if (rComp != nullptr) {
            rComp->postRender(sceneRenderState, renderStagePass, bufferInOut);
        }
    }
}

U8 RenderingComponent::getLoDLevel(const F32 distSQtoCenter, const RenderStage renderStage, const vec4<U16>& lodThresholds) {
    OPTICK_EVENT();

    const auto&[state, level] = _lodLockLevels[to_base(renderStage)];

    if (state) {
        return CLAMPED(level, to_U8(0u), MAX_LOD_LEVEL);
    }

    const F32 distSQtoCenterClamped = std::max(distSQtoCenter, std::numeric_limits<F32>::epsilon());
    for (U8 i = 0; i < MAX_LOD_LEVEL; ++i) {
        if (distSQtoCenterClamped <= to_F32(SQUARED(lodThresholds[i]))) {
            return i;
        }
    }

    return MAX_LOD_LEVEL;
}

void RenderingComponent::prepareDrawPackage(const Camera& camera, const SceneRenderState& sceneRenderState, const RenderStagePass& renderStagePass, const bool refreshData) {
    OPTICK_EVENT();

    RenderPackage& pkg = getDrawPackage(renderStagePass);
    if (pkg.empty() || getRebuildFlag(renderStagePass)) {
        rebuildDrawCommands(renderStagePass, camera, pkg);
        setRebuildFlag(renderStagePass, false);
    }

    Attorney::SceneGraphNodeComponent::prepareRender(_parentSGN, *this, camera, renderStagePass, refreshData);

    if (refreshData) {
        BoundsComponent* bComp = static_cast<BoundsComponent*>(_parentSGN->get<BoundsComponent>());
        const vec3<F32> cameraEye = camera.getEye();
        const SceneNodeRenderState& renderState = _parentSGN->getNode<>().renderState();
        if (renderState.lod0OnCollision() && bComp->getBoundingBox().containsPoint(cameraEye)) {
            _lodLevels[to_base(renderStagePass._stage)] = 0u;
        } else {
            const BoundingBox& aabb = bComp->getBoundingBox();
            const vec3<F32> LoDtarget = renderState.useBoundsCenterForLoD() ? aabb.getCenter() : aabb.nearestPoint(cameraEye);
            const F32 distanceSQToCenter = LoDtarget.distanceSquared(cameraEye);
            _lodLevels[to_base(renderStagePass._stage)] = getLoDLevel(distanceSQToCenter, renderStagePass._stage, sceneRenderState.lodThresholds(renderStagePass._stage));
        }
    }

    pkg.setDrawOption(CmdRenderOptions::RENDER_GEOMETRY, (renderOptionEnabled(RenderOptions::RENDER_GEOMETRY) &&
                                                          sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY)));

    pkg.setDrawOption(CmdRenderOptions::RENDER_WIREFRAME, (renderOptionEnabled(RenderOptions::RENDER_WIREFRAME) ||
                                                           sceneRenderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME)));

    DescriptorSet& set = pkg.get<GFX::BindDescriptorSetsCommand>(0)->_set;
    if (pkg.textureDataDirty()) {
        if (_materialInstance != nullptr) {
            _materialInstance->getTextureData(renderStagePass, set._textureData);
        }
        pkg.textureDataDirty(false);
    }

    if (!renderStagePass.isDepthPass()) {
        if (renderStagePass._stage != RenderStage::REFLECTION) {
            ReflectRefractData& refData = _reflectRefractData[to_base(DataType::REFLECT)];
            SharedLock<SharedMutex> r_lock(refData._lock);
            if (refData._texture._textureType != TextureType::COUNT) {
                const TextureEntry* existingEntry = set._textureData.find(to_U8(TextureUsage::REFLECTION));
                if (existingEntry == nullptr || !IsValid(*existingEntry)) {
                    set._textureData.add(TextureEntry{ refData._texture, refData._samplerHash, TextureUsage::REFLECTION });
                }
            }
        }

        if (renderStagePass._stage != RenderStage::REFRACTION) {
            ReflectRefractData& refData = _reflectRefractData[to_base(DataType::REFRACT)];
            SharedLock<SharedMutex> r_lock(refData._lock);
            if (refData._texture._textureType != TextureType::COUNT) {
                const TextureEntry* existingEntry = set._textureData.find(to_U8(TextureUsage::REFRACTION));
                if (existingEntry == nullptr || !IsValid(*existingEntry)) {
                    set._textureData.add(TextureEntry{ refData._texture, refData._samplerHash, TextureUsage::REFRACTION });
                }
            }
        }
    }
}

RenderPackage& RenderingComponent::getDrawPackage(const RenderStagePass& renderStagePass) {
    const U8 s = to_U8(renderStagePass._stage);
    const U8 p = to_U8(renderStagePass._stage == RenderStage::SHADOW ? RenderPassType::MAIN_PASS : renderStagePass._passType);
    const U16 i = RenderStagePass::indexForStage(renderStagePass);

    return _renderPackages[s][p][i];
}

const RenderPackage& RenderingComponent::getDrawPackage(const RenderStagePass& renderStagePass) const {
    const U8 s = to_U8(renderStagePass._stage);
    const U8 p = to_U8(renderStagePass._stage == RenderStage::SHADOW ? RenderPassType::MAIN_PASS : renderStagePass._passType);
    const U16 i = RenderStagePass::indexForStage(renderStagePass);

    return _renderPackages[s][p][i];
}

void RenderingComponent::setRebuildFlag(const RenderStagePass& renderStagePass, const bool state) {
    const U8 s = to_U8(renderStagePass._stage);
    const U8 p = to_U8(renderStagePass._stage == RenderStage::SHADOW ? RenderPassType::MAIN_PASS : renderStagePass._passType);
    const U16 i = RenderStagePass::indexForStage(renderStagePass);

    _rebuildDrawCommandsFlags[s][p][i] = state;
}

bool RenderingComponent::getRebuildFlag(const RenderStagePass& renderStagePass) const {
    const U8 s = to_U8(renderStagePass._stage);
    const U8 p = to_U8(renderStagePass._stage == RenderStage::SHADOW ? RenderPassType::MAIN_PASS : renderStagePass._passType);
    const U16 i = RenderStagePass::indexForStage(renderStagePass);

    return _rebuildDrawCommandsFlags[s][p][i];
}

size_t RenderingComponent::getSortKeyHash(const RenderStagePass& renderStagePass) const {
    const RenderPackage& pkg = getDrawPackage(renderStagePass);
    if (pkg.count<GFX::BindPipelineCommand>() > 0) {
        return pkg.get<GFX::BindPipelineCommand>(0)->_pipeline->getHash();
    }

    return 0u;
}

bool RenderingComponent::updateReflection(const U16 reflectionIndex,
                                          const bool inBudget,
                                          Camera* camera,
                                          const SceneRenderState& renderState,
                                          GFX::CommandBuffer& bufferInOut)
{
    //Target texture: the opposite of what we bind during the regular passes
    ReflectRefractData& refData = _reflectRefractData[to_base(DataType::REFLECT)];
    if (_reflectorType != ReflectorType::COUNT && _reflectionCallback && inBudget) {
        const RenderTargetID reflectRTID(_reflectorType == ReflectorType::PLANAR 
                                                         ? RenderTargetUsage::REFLECTION_PLANAR
                                                         : RenderTargetUsage::REFLECTION_CUBE,
                                         reflectionIndex);
        RenderPassManager* passManager = _context.parent().renderPassManager();
        RenderCbkParams params(_context, _parentSGN, renderState, reflectRTID, reflectionIndex, to_U8(_reflectorType), camera);
        _reflectionCallback(passManager, params, bufferInOut);

        const auto& targetAtt = _context.renderTargetPool().renderTarget(reflectRTID).getAttachment(RTAttachmentType::Colour, 0u);
        UniqueLock<SharedMutex> w_lock(refData._lock);
        refData._texture = targetAtt.texture()->data();
        refData._samplerHash = targetAtt.samplerHash();
        refData._gpuAddress = targetAtt.texture()->getGPUAddress(refData._samplerHash);
        return true;
    }

    UniqueLock<SharedMutex> w_lock(refData._lock);
    refData._texture = {};
    return false;
}

bool RenderingComponent::updateRefraction(const U16 refractionIndex,
                                          const bool inBudget,
                                          Camera* camera,
                                          const SceneRenderState& renderState,
                                          GFX::CommandBuffer& bufferInOut) {
    //Target texture: the opposite of what we bind during the regular passes
    ReflectRefractData& refData = _reflectRefractData[to_base(DataType::REFRACT)];

    // no default refraction system!
    if (_refractorType != RefractorType::COUNT && _refractionCallback && inBudget) {
        RenderPassManager* passManager = _context.parent().renderPassManager();

        const RenderTargetID refractRTID(RenderTargetUsage::REFRACTION_PLANAR, refractionIndex);
        RenderCbkParams params{ _context, _parentSGN, renderState, refractRTID, refractionIndex, to_U8(_refractorType), camera };
        _refractionCallback(passManager, params, bufferInOut);

        const auto& targetAtt = _context.renderTargetPool().renderTarget(refractRTID).getAttachment(RTAttachmentType::Colour, 0u);
        const Texture_ptr& refractionTexture = targetAtt.texture();

        UniqueLock<SharedMutex> w_lock(refData._lock);
        refData._texture = refractionTexture->data();
        refData._samplerHash = targetAtt.samplerHash();
        refData._gpuAddress = targetAtt.texture()->getGPUAddress(refData._samplerHash);
        return true;
    }

    UniqueLock<SharedMutex> w_lock(refData._lock);
    refData._texture = {};
    return false;
}

void RenderingComponent::updateNearestProbes(const vec3<F32>& position) {
    _envProbes.resize(0);
    _reflectionProbeIndex = 0u;

    SceneEnvironmentProbePool* probePool = _context.context().kernel().sceneManager()->getEnvProbes();
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
                        [&position](const auto& a, const auto& b) -> bool {
                            return a->distanceSqTo(position) < b->distanceSqTo(position);
                        });

            // We need to update this probe because we are going to use it. This will always lag one frame, but at least we keep updates separate from renders.
            _envProbes.front()->queueRefresh();
            _reflectionProbeIndex = _envProbes.front()->poolIndex();
        }
    }
}

/// Draw some kind of selection doodad. May differ if editor is running or not
void RenderingComponent::drawSelectionGizmo(GFX::CommandBuffer& bufferInOut) {
    if (!_selectionGizmo) {
        _selectionGizmo = _context.newIMP();
        _selectionGizmo->name("SelectionGizmo_" + _parentSGN->name());
        _selectionGizmo->skipPostFX(true);
        _selectionGizmo->pipeline(*_primitivePipeline[0]);
        _selectionGizmoDirty = true;
    }
    
    if (_selectionGizmoDirty) {
        _selectionGizmoDirty = false;

        UColour4 colour = UColour4(64, 255, 128, 255);
        if_constexpr(Config::Build::ENABLE_EDITOR) {
            if (_context.parent().platformContext().editor().inEditMode()) {
                colour = UColour4(255, 255, 255, 255);
            }
        }
        //draw something else (at some point ...)
        BoundsComponent* bComp = static_cast<BoundsComponent*>(_parentSGN->get<BoundsComponent>());
        DIVIDE_ASSERT(bComp != nullptr);
        _selectionGizmo->fromOBB(bComp->getOBB(), colour);
    }

    bufferInOut.add(_selectionGizmo->toCommandBuffer());
}

/// Draw the axis arrow gizmo
void RenderingComponent::drawDebugAxis(GFX::CommandBuffer& bufferInOut) {
    if (!_axisGizmo) {
        Line temp = {};
        temp.widthStart(10.0f);
        temp.widthEnd(10.0f);
        temp.positionStart(VECTOR3_ZERO);

        Line axisLines[3];
        // Red X-axis
        temp.positionEnd(WORLD_X_AXIS * 4);
        temp.colourStart(UColour4(255, 0, 0, 255));
        temp.colourEnd(UColour4(255, 0, 0, 255));
        axisLines[0] = temp;

        // Green Y-axis
        temp.positionEnd(WORLD_Y_AXIS * 4);
        temp.colourStart(UColour4(0, 255, 0, 255));
        temp.colourEnd(UColour4(0, 255, 0, 255));
        axisLines[1] = temp;

        // Blue Z-axis
        temp.positionEnd(WORLD_Z_AXIS * 4);
        temp.colourStart(UColour4(0, 0, 255, 255));
        temp.colourEnd(UColour4(0, 0, 255, 255));
        axisLines[2] = temp;

        _axisGizmo = _context.newIMP();
        _axisGizmo->name("AxisGizmo_" + _parentSGN->name());
        _axisGizmo->skipPostFX(true);
        _axisGizmo->pipeline(*_primitivePipeline[2]);
        // Create the object containing all of the lines
        _axisGizmo->beginBatch(true, 3 * 2, 1);
        _axisGizmo->attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(axisLines[0].colourStart()));
        // Set the mode to line rendering
        _axisGizmo->begin(PrimitiveType::LINES);
        // Add every line in the list to the batch
        for (const Line& line : axisLines) {
            _axisGizmo->attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(line.colourStart()));
            _axisGizmo->vertex(line.positionStart());
            _axisGizmo->vertex(line.positionEnd());
        }
        _axisGizmo->end();
        // Finish our object
        _axisGizmo->endBatch();
    }

    _axisGizmo->worldMatrix(_worldOffsetMatrixCache);

    bufferInOut.add(_axisGizmo->toCommandBuffer());
}

void RenderingComponent::drawSkeleton(GFX::CommandBuffer& bufferInOut) {
    const SceneNode& node = _parentSGN->getNode();
    const bool isSubMesh = node.type() == SceneNodeType::TYPE_OBJECT3D && static_cast<const Object3D&>(node).getObjectType() == ObjectType::SUBMESH;
    if (!isSubMesh) {
        return;
    }

    // Continue only for skinned 3D objects
    if (static_cast<const Object3D&>(node).getObjectFlag(Object3D::ObjectFlag::OBJECT_FLAG_SKINNED))
    {
        if (_skeletonPrimitive == nullptr) {
            _skeletonPrimitive = _context.newIMP();
            _skeletonPrimitive->skipPostFX(true);
            _skeletonPrimitive->name("Skeleton_" + _parentSGN->name());
            _skeletonPrimitive->pipeline(*_primitivePipeline[1]);
        }
        // Get the animation component of any submesh. They should be synced anyway.
        AnimationComponent* animComp = _parentSGN->get<AnimationComponent>();
        if (animComp != nullptr) {
            // Get the skeleton lines from the submesh's animation component
            const vectorEASTL<Line>& skeletonLines = animComp->skeletonLines();
            // Submit the skeleton lines to the GPU for rendering
            _skeletonPrimitive->fromLines(skeletonLines.data(), skeletonLines.size());
            _skeletonPrimitive->worldMatrix(_worldMatrixCache);
            bufferInOut.add(_skeletonPrimitive->toCommandBuffer());
        }
    } else if (_skeletonPrimitive) {
        _context.destroyIMP(_skeletonPrimitive);
    }
}

void RenderingComponent::drawBounds(const bool AABB, const bool OBB, const bool Sphere, GFX::CommandBuffer& bufferInOut) {
    if (!AABB && !Sphere && !OBB) {
        return;
    }

    const SceneNode& node = _parentSGN->getNode();
    const bool isSubMesh = node.type() == SceneNodeType::TYPE_OBJECT3D && static_cast<const Object3D&>(node).getObjectType() == ObjectType::SUBMESH;

    if (AABB) {
        if (_boundingBoxPrimitive == nullptr) {
            _boundingBoxPrimitive = _context.newIMP();
            _boundingBoxPrimitive->name("BoundingBox_" + _parentSGN->name());
            _boundingBoxPrimitive->pipeline(*_primitivePipeline[0]);
            _boundingBoxPrimitive->skipPostFX(true);
        }


        const BoundingBox& bb = _parentSGN->get<BoundsComponent>()->getBoundingBox();
        _boundingBoxPrimitive->fromBox(bb.getMin(), bb.getMax(), isSubMesh ? UColour4(0, 0, 255, 255) : UColour4(255, 0, 255, 255));
        bufferInOut.add(_boundingBoxPrimitive->toCommandBuffer());
    } else if (_boundingBoxPrimitive != nullptr) {
        _context.destroyIMP(_boundingBoxPrimitive);
    }

    if (OBB) {
        if (_orientedBoundingBoxPrimitive == nullptr) {
            _orientedBoundingBoxPrimitive = _context.newIMP();
            _orientedBoundingBoxPrimitive->name("OrientedBoundingBox_" + _parentSGN->name());
            _orientedBoundingBoxPrimitive->pipeline(*_primitivePipeline[0]);
            _orientedBoundingBoxPrimitive->skipPostFX(true);
        }

        const auto& obb = _parentSGN->get<BoundsComponent>()->getOBB();
        _orientedBoundingBoxPrimitive->fromOBB(obb, isSubMesh ? UColour4(128, 0, 255, 255) : UColour4(255, 0, 128, 255));
        bufferInOut.add(_orientedBoundingBoxPrimitive->toCommandBuffer());
    } else if (_orientedBoundingBoxPrimitive != nullptr) {
        _context.destroyIMP(_orientedBoundingBoxPrimitive);
    }

    if (Sphere) {
        if (_boundingSpherePrimitive == nullptr) {
            _boundingSpherePrimitive = _context.newIMP();
            _boundingSpherePrimitive->name("BoundingSphere_" + _parentSGN->name());
            _boundingSpherePrimitive->pipeline(*_primitivePipeline[0]);
            _boundingSpherePrimitive->skipPostFX(true);
        }

        const BoundingSphere& bs = _parentSGN->get<BoundsComponent>()->getBoundingSphere();
        _boundingSpherePrimitive->fromSphere(bs.getCenter(), bs.getRadius(), isSubMesh ? UColour4(0, 255, 0, 255) : UColour4(255, 255, 0, 255));
        bufferInOut.add(_boundingSpherePrimitive->toCommandBuffer());
    } else if (_boundingSpherePrimitive != nullptr) {
        _context.destroyIMP(_boundingSpherePrimitive);
    }
}

void RenderingComponent::OnData(const ECS::CustomEvent& data) {
    switch (data._type) {
        case  ECS::CustomEvent::Type::TransformUpdated:
        {
            TransformComponent* tComp = static_cast<TransformComponent*>(data._sourceCmp);
            assert(tComp != nullptr);
            updateNearestProbes(tComp->getPosition());

            tComp->getWorldMatrix(_worldMatrixCache);

            _worldOffsetMatrixCache = mat4<F32>(GetMatrix(tComp->getOrientation()), false);
            _worldOffsetMatrixCache.setTranslation(tComp->getPosition());
        } break;
        case ECS::CustomEvent::Type::DrawBoundsChanged:
        {
            BoundsComponent* bComp = static_cast<BoundsComponent*>(data._sourceCmp);
            toggleBoundsDraw(bComp->showAABB(), bComp->showBS(), true);
        } break;
        case ECS::CustomEvent::Type::BoundsUpdated:
        {
            _selectionGizmoDirty = true;
        } break;
        default: break;
    }
}
}
