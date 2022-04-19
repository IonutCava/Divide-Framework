#include "stdafx.h"

#include "Headers/Water.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Managers/Headers/SceneManager.h"
#include "Managers/Headers/RenderPassManager.h"

#include "Geometry/Material/Headers/Material.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "ECS/Components/Headers/NavigationComponent.h"

namespace Divide {

namespace {
    // how far to offset the clipping planes for reflections in order to avoid artefacts at water/geometry intersections with high wave noise factors
    constexpr F32 g_reflectionPlaneCorrectionHeight = -1.0f;
}

WaterPlane::WaterPlane(ResourceCache* parentCache, size_t descriptorHash, const Str256& name)
    : SceneNode(parentCache, descriptorHash, name, ResourcePath{ name }, {}, SceneNodeType::TYPE_WATER, to_base(ComponentType::TRANSFORM))
{
    _fogStartEnd = { 648.f, 1300.f };
    _noiseTile = { 15.0f, 15.0f };
    _noiseFactor = { 0.1f, 0.1f };
    _refractionTint = {0.f, 0.567f, 0.845f};
    _waterDistanceFogColour = {0.9f, 0.9f, 1.f};
    _dimensions = { 500u, 500u, 500u };
    // The water doesn't cast shadows, doesn't need ambient occlusion and doesn't have real "depth"
    renderState().addToDrawExclusionMask(RenderStage::SHADOW);


    EditorComponentField blurReflectionField = {};
    blurReflectionField._name = "Blur reflections";
    blurReflectionField._data = &_blurReflections;
    blurReflectionField._type = EditorComponentFieldType::PUSH_TYPE;
    blurReflectionField._readOnly = false;
    blurReflectionField._basicType = GFX::PushConstantType::BOOL;

    getEditorComponent().registerField(MOV(blurReflectionField));
    
    EditorComponentField blurKernelSizeField = {};
    blurKernelSizeField._name = "Blur kernel size";
    blurKernelSizeField._data = &_blurKernelSize;
    blurKernelSizeField._type = EditorComponentFieldType::SLIDER_TYPE;
    blurKernelSizeField._readOnly = false;
    blurKernelSizeField._basicType = GFX::PushConstantType::UINT;
    blurKernelSizeField._basicTypeSize = GFX::PushConstantSize::WORD;
    blurKernelSizeField._range = { 2.f, 20.f };
    blurKernelSizeField._step = 1.f;

    getEditorComponent().registerField(MOV(blurKernelSizeField));

    EditorComponentField reflPlaneOffsetField = {};
    reflPlaneOffsetField._name = "Reflection Plane Offset";
    reflPlaneOffsetField._data = &_reflPlaneOffset;
    reflPlaneOffsetField._range = { -5.0f, 5.0f };
    reflPlaneOffsetField._type = EditorComponentFieldType::PUSH_TYPE;
    reflPlaneOffsetField._readOnly = false;
    reflPlaneOffsetField._basicType = GFX::PushConstantType::FLOAT;

    getEditorComponent().registerField(MOV(reflPlaneOffsetField));

    EditorComponentField refrPlaneOffsetField = {};
    refrPlaneOffsetField._name = "Refraction Plane Offset";
    refrPlaneOffsetField._data = &_refrPlaneOffset;
    refrPlaneOffsetField._range = { -5.0f, 5.0f };
    refrPlaneOffsetField._type = EditorComponentFieldType::PUSH_TYPE;
    refrPlaneOffsetField._readOnly = false;
    refrPlaneOffsetField._basicType = GFX::PushConstantType::FLOAT;

    getEditorComponent().registerField(MOV(refrPlaneOffsetField));

    EditorComponentField fogDistanceField = {};
    fogDistanceField._name = "Fog start/end distances";
    fogDistanceField._data = &_fogStartEnd;
    fogDistanceField._range = { 0.0f, 4096.0f };
    fogDistanceField._type = EditorComponentFieldType::PUSH_TYPE;
    fogDistanceField._readOnly = false;
    fogDistanceField._basicType = GFX::PushConstantType::VEC2;

    getEditorComponent().registerField(MOV(fogDistanceField));
    
    EditorComponentField waterFogField = {};
    waterFogField._name = "Water fog colour";
    waterFogField._data = &_waterDistanceFogColour;
    waterFogField._type = EditorComponentFieldType::PUSH_TYPE;
    waterFogField._readOnly = false;
    waterFogField._basicType = GFX::PushConstantType::FCOLOUR3;

    getEditorComponent().registerField(MOV(waterFogField));
    
    EditorComponentField noiseTileSizeField = {};
    noiseTileSizeField._name = "Noise tile factor";
    noiseTileSizeField._data = &_noiseTile;
    noiseTileSizeField._range = { 0.0f, 1000.0f };
    noiseTileSizeField._type = EditorComponentFieldType::PUSH_TYPE;
    noiseTileSizeField._readOnly = false;
    noiseTileSizeField._basicType = GFX::PushConstantType::VEC2;

    getEditorComponent().registerField(MOV(noiseTileSizeField));

    EditorComponentField noiseFactorField = {};
    noiseFactorField._name = "Noise factor";
    noiseFactorField._data = &_noiseFactor;
    noiseFactorField._range = { 0.0f, 10.0f };
    noiseFactorField._type = EditorComponentFieldType::PUSH_TYPE;
    noiseFactorField._readOnly = false;
    noiseFactorField._basicType = GFX::PushConstantType::VEC2;

    getEditorComponent().registerField(MOV(noiseFactorField));

    EditorComponentField refractionTintField = {};
    refractionTintField._name = "Refraction tint";
    refractionTintField._data = &_refractionTint;
    refractionTintField._type = EditorComponentFieldType::PUSH_TYPE;
    refractionTintField._readOnly = false;
    refractionTintField._basicType = GFX::PushConstantType::FCOLOUR3;

    getEditorComponent().registerField(MOV(refractionTintField));   
    
    EditorComponentField specularShininessField = {};
    specularShininessField._name = "Specular Shininess";
    specularShininessField._data = &_specularShininess;
    specularShininessField._type = EditorComponentFieldType::PUSH_TYPE;
    specularShininessField._readOnly = false;
    specularShininessField._range = { 0.01f, Material::MAX_SHININESS };
    specularShininessField._basicType = GFX::PushConstantType::FLOAT;

    getEditorComponent().registerField(MOV(specularShininessField));

    getEditorComponent().onChangedCbk([this](const std::string_view field) noexcept {onEditorChange(field); });

    _editorDataDirtyState.fill(EditorDataState::QUEUED);
}

WaterPlane::~WaterPlane()
{
    Camera::destroyCamera(_reflectionCam);
}

void WaterPlane::onEditorChange(std::string_view) noexcept {
    _editorDataDirtyState.fill(EditorDataState::QUEUED);

    if (_fogStartEnd.y <= _fogStartEnd.y) {
        _fogStartEnd.y = _fogStartEnd.x + 0.1f;
    }
}

bool WaterPlane::load() {
    if (_plane != nullptr) {
        return false;
    }

    setState(ResourceState::RES_LOADING);

    _reflectionCam = Camera::createCamera<StaticCamera>(resourceName() + "_reflectionCam");

    const Str256& name = resourceName();

    SamplerDescriptor defaultSampler = {};
    defaultSampler.wrapUVW(TextureWrap::REPEAT);
    defaultSampler.minFilter(TextureFilter::LINEAR_MIPMAP_LINEAR);
    defaultSampler.magFilter(TextureFilter::LINEAR);
    defaultSampler.anisotropyLevel(4);

    TextureDescriptor texDescriptor(TextureType::TEXTURE_2D_ARRAY);
    texDescriptor.textureOptions()._alphaChannelTransparency = false;
    std::atomic_uint loadTasks = 0u;

    ResourceDescriptor waterTexture("waterTexture_" + name);
    waterTexture.assetName(ResourcePath{ "terrain_water_NM_old.jpg" });
    waterTexture.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    waterTexture.propertyDescriptor(texDescriptor);
    waterTexture.waitForReady(false);

    Texture_ptr waterNM = CreateResource<Texture>(_parentCache, waterTexture, loadTasks);

    ResourceDescriptor waterMaterial("waterMaterial_" + name);
    Material_ptr waterMat = CreateResource<Material>(_parentCache, waterMaterial);
    waterMat->updatePriorirty(Material::UpdatePriority::Medium);
    waterMat->properties().shadingMode(ShadingMode::BLINN_PHONG);
    waterMat->properties().bumpMethod(BumpMethod::NORMAL);
    waterMat->properties().isStatic(true);
    waterMat->addShaderDefine(ShaderType::COUNT, "ENABLE_TBN");

    WAIT_FOR_CONDITION(loadTasks.load() == 0u);

    waterMat->setTexture(TextureUsage::NORMALMAP, waterNM, defaultSampler.getHash(), TextureOperation::REPLACE);
    waterMat->computeShaderCBK([]([[maybe_unused]] Material* material, const RenderStagePass stagePass) {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "water.glsl";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "water.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._name = "waterColour";
        if (IsDepthPass(stagePass)) {
            if (stagePass._stage == RenderStage::DISPLAY) {
                shaderDescriptor._name = "waterPrePass";
                vertModule._defines.emplace_back("PRE_PASS");
                fragModule._defines.emplace_back("PRE_PASS");
                shaderDescriptor._modules.push_back(vertModule);
                shaderDescriptor._modules.push_back(fragModule);
            } else {
                shaderDescriptor._name = "waterDepthPass";
                shaderDescriptor._modules.push_back(vertModule);
            }
        } else {
            shaderDescriptor._modules.push_back(vertModule);
            shaderDescriptor._modules.push_back(fragModule);
        }

        return shaderDescriptor;
    });


    waterMat->properties().roughness(0.01f);

    setMaterialTpl(waterMat);
    
    ResourceDescriptor waterPlane("waterPlane");
    waterPlane.flag(true);  // No default material
    waterPlane.waitForReady(true);

    _plane = CreateResource<Quad3D>(_parentCache, waterPlane);
    
    const F32 halfWidth = _dimensions.width * 0.5f;
    const F32 halfLength = _dimensions.height * 0.5f;

    setBounds(BoundingBox(vec3<F32>(-halfWidth, -_dimensions.depth, -halfLength), vec3<F32>(halfWidth, 0, halfLength)));

    return SceneNode::load();
}

void WaterPlane::postLoad(SceneGraphNode* sgn) {
    NavigationComponent* nComp = sgn->get<NavigationComponent>();
    if (nComp != nullptr) {
        nComp->navigationContext(NavigationComponent::NavigationContext::NODE_OBSTACLE);
    }

    RigidBodyComponent* rComp = sgn->get<RigidBodyComponent>();
    if (rComp != nullptr) {
        rComp->physicsCollisionGroup(PhysicsGroup::GROUP_STATIC);
    }

    const F32 halfWidth = _dimensions.width * 0.5f;
    const F32 halfLength = _dimensions.height * 0.5f;

    _plane->setCorner(Quad3D::CornerLocation::TOP_LEFT, vec3<F32>(-halfWidth, 0, -halfLength));
    _plane->setCorner(Quad3D::CornerLocation::TOP_RIGHT, vec3<F32>(halfWidth, 0, -halfLength));
    _plane->setCorner(Quad3D::CornerLocation::BOTTOM_LEFT, vec3<F32>(-halfWidth, 0, halfLength));
    _plane->setCorner(Quad3D::CornerLocation::BOTTOM_RIGHT, vec3<F32>(halfWidth, 0, halfLength));
    _plane->setNormal(Quad3D::CornerLocation::CORNER_ALL, WORLD_Y_AXIS);
    _boundingBox.set(vec3<F32>(-halfWidth, -_dimensions.depth, -halfLength), vec3<F32>(halfWidth, 0, halfLength));

    RenderingComponent* renderable = sgn->get<RenderingComponent>();

    // If the reflector is reasonably sized, we should keep LoD fixed so that we always update reflections
    if (sgn->context().config().rendering.lodThresholds.x < std::max(halfWidth, halfLength)) {
        renderable->lockLoD(0u);
    }

    renderable->setReflectionCallback([this](RenderPassManager* passManager, RenderCbkParams& params, GFX::CommandBuffer& commandsInOut) {
        updateReflection(passManager, params, commandsInOut);
    }, ReflectorType::PLANAR);

    renderable->setRefractionCallback([this](RenderPassManager* passManager, RenderCbkParams& params, GFX::CommandBuffer& commandsInOut) {
        updateRefraction(passManager, params, commandsInOut);
    }, RefractorType::PLANAR);

    renderable->toggleRenderOption(RenderingComponent::RenderOptions::CAST_SHADOWS, false);

    SceneNode::postLoad(sgn);
}

void WaterPlane::sceneUpdate(const U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState) {
    for (EditorDataState& state : _editorDataDirtyState) {
        switch (state) {
            case EditorDataState::QUEUED:
                state = EditorDataState::CHANGED;
                break;
            case EditorDataState::PROCESSED:
                state = EditorDataState::IDLE;
                break;
            case EditorDataState::CHANGED:
            case EditorDataState::IDLE:
                break;
        }
    }
    WaterBodyData data;
    data._positionW = sgn->get<TransformComponent>()->getWorldPosition();
    data._extents.xyz = { to_F32(_dimensions.width),
                          to_F32(_dimensions.depth),
                          to_F32(_dimensions.height) };

    sceneState.waterBodies().registerData(data);
    SceneNode::sceneUpdate(deltaTimeUS, sgn, sceneState);
}

void WaterPlane::prepareRender(SceneGraphNode* sgn,
                               RenderingComponent& rComp,
                               const RenderStagePass renderStagePass,
                               const CameraSnapshot& cameraSnapshot,
                               const bool refreshData) {

    EditorDataState& state = _editorDataDirtyState[to_base(renderStagePass._stage)];
    if (state == EditorDataState::CHANGED || state == EditorDataState::PROCESSED) {
        PushConstants& constants = rComp.getPushConstants(renderStagePass);
        constants.set(_ID("_noiseFactor"), GFX::PushConstantType::VEC2, noiseFactor());
        constants.set(_ID("_noiseTile"), GFX::PushConstantType::VEC2, noiseTile());
        constants.set(_ID("_fogStartEndDistances"), GFX::PushConstantType::VEC2, fogStartEnd());
        constants.set(_ID("_refractionTint"), GFX::PushConstantType::FCOLOUR3, refractionTint());
        constants.set(_ID("_waterDistanceFogColour"), GFX::PushConstantType::FCOLOUR3, waterDistanceFogColour());
        constants.set(_ID("_specularShininess"), GFX::PushConstantType::FLOAT, specularShininess());
        if (refreshData) {
            state = EditorDataState::PROCESSED;
        }
    }

    SceneNode::prepareRender(sgn, rComp, renderStagePass, cameraSnapshot, refreshData);
}

bool WaterPlane::PointUnderwater(const SceneGraphNode* sgn, const vec3<F32>& point) noexcept {
    return sgn->get<BoundsComponent>()->getBoundingBox().containsPoint(point);
}

void WaterPlane::buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut, PrimitiveTopology& topologyOut, AttributeMap& vertexFormatInOut) {

    topologyOut = PrimitiveTopology::TRIANGLE_STRIP;
    _plane->geometryBuffer()->populateAttributeMap(vertexFormatInOut);

    GenericDrawCommand cmd = {};
    cmd._cmd.indexCount = to_U32(_plane->geometryBuffer()->getIndexCount());
    cmd._sourceBuffer = _plane->geometryBuffer()->handle();

    cmdsOut.emplace_back(GFX::DrawCommand{ cmd });
    _editorDataDirtyState.fill(EditorDataState::CHANGED);

    SceneNode::buildDrawCommands(sgn, cmdsOut, topologyOut, vertexFormatInOut);
}

/// update water refraction
void WaterPlane::updateRefraction(RenderPassManager* passManager, RenderCbkParams& renderParams, GFX::CommandBuffer& bufferInOut) const {
    static RTClearColourDescriptor clearColourDescriptor;
    clearColourDescriptor._customClearColour[0] = DefaultColours::BLUE;

    // If we are above water, process the plane's refraction.
    // If we are below, we render the scene normally
    const bool underwater = PointUnderwater(renderParams._sgn, renderParams._camera->getEye());
    Plane<F32> refractionPlane;
    updatePlaneEquation(renderParams._sgn, refractionPlane, underwater, refrPlaneOffset());
    refractionPlane._distance += g_reflectionPlaneCorrectionHeight;

    RTClearDescriptor clearDescriptor = {};
    clearDescriptor._customClearColour = &clearColourDescriptor;

    RenderPassParams params = {};
    params._sourceNode = renderParams._sgn;
    params._targetHIZ = {}; // We don't need to HiZ cull refractions
    params._targetOIT = {}; // We don't need to draw refracted transparents using woit 
    params._minExtents.set(1.0f);
    params._stagePass = { RenderStage::REFRACTION, RenderPassType::COUNT, renderParams._passIndex, RenderStagePass::VariantType::VARIANT_0 };
    params._target = renderParams._renderTarget;
    params._clippingPlanes.set(0, refractionPlane);
    params._passName = "Refraction";
    if (!underwater) {
        ClearBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES)));
    }

    GFX::ClearRenderTargetCommand clearMainTarget = {};
    clearMainTarget._target = params._target;
    clearMainTarget._descriptor = clearDescriptor;
    EnqueueCommand(bufferInOut, clearMainTarget);

    passManager->doCustomPass(renderParams._camera, params, bufferInOut);

    const PlatformContext& context = passManager->parent().platformContext();
    const RenderTarget& rt = context.gfx().renderTargetPool().renderTarget(params._target);

    GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
    computeMipMapsCommand._texture = rt.getAttachment(RTAttachmentType::Colour, 0).texture().get();
    EnqueueCommand(bufferInOut, computeMipMapsCommand);
}

/// Update water reflections
void WaterPlane::updateReflection(RenderPassManager* passManager, RenderCbkParams& renderParams, GFX::CommandBuffer& bufferInOut) const {
    static RTClearColourDescriptor clearColourDescriptor;
    clearColourDescriptor._customClearColour[0] = DefaultColours::BLUE;

    // If we are above water, process the plane's refraction.
    // If we are below, we render the scene normally
    const bool underwater = PointUnderwater(renderParams._sgn, renderParams._camera->getEye());
    if (underwater) {
        return;
    }

    Plane<F32> reflectionPlane;
    updatePlaneEquation(renderParams._sgn, reflectionPlane, !underwater, reflPlaneOffset());

    // Reset reflection cam
    renderParams._camera->updateLookAt();
    _reflectionCam->fromCamera(*renderParams._camera);
    if (!underwater) {
        reflectionPlane._distance += g_reflectionPlaneCorrectionHeight;
        _reflectionCam->setReflection(reflectionPlane);
    }

    //Don't clear colour attachment because we'll always draw something for every texel, even if that something is just the sky
    RTClearDescriptor clearDescriptor = {};
    clearDescriptor._customClearColour = &clearColourDescriptor;

    RenderPassParams params = {};
    params._sourceNode = renderParams._sgn;
    params._targetHIZ = RenderTargetUsage::HI_Z_REFLECT;
    params._targetOIT = RenderTargetUsage::OIT_REFLECT;
    params._minExtents.set(1.5f);
    params._stagePass = { RenderStage::REFLECTION, RenderPassType::COUNT, renderParams._passIndex, static_cast<RenderStagePass::VariantType>(ReflectorType::PLANAR) };
    params._target = renderParams._renderTarget;
    params._clippingPlanes.set(0, reflectionPlane);
    params._passName = "Reflection";
    ClearBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES)));

    GFX::ClearRenderTargetCommand clearMainTarget = {};
    clearMainTarget._target = params._target;
    clearMainTarget._descriptor = clearDescriptor;
    EnqueueCommand(bufferInOut, clearMainTarget);

    passManager->doCustomPass(_reflectionCam, params, bufferInOut);

    if (_blurReflections) {
        RenderTarget& reflectTarget = renderParams._context.renderTargetPool().renderTarget(renderParams._renderTarget);
        RenderTargetHandle reflectionTargetHandle(renderParams._renderTarget, &reflectTarget);

        RenderTarget& reflectBlurTarget = renderParams._context.renderTargetPool().renderTarget(RenderTargetUsage::REFLECTION_PLANAR_BLUR);
        RenderTargetHandle reflectionBlurBuffer(RenderTargetUsage::REFLECTION_PLANAR_BLUR, &reflectBlurTarget);

        renderParams._context.blurTarget(reflectionTargetHandle,
                                         reflectionBlurBuffer,
                                         reflectionTargetHandle,
                                         RTAttachmentType::Colour,
                                         0, 
                                         _blurKernelSize,
                                         true,
                                         1,
                                         bufferInOut);
    }

    const PlatformContext& context = passManager->parent().platformContext();
    const RenderTarget& rt = context.gfx().renderTargetPool().renderTarget(params._target);

    GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
    computeMipMapsCommand._texture = rt.getAttachment(RTAttachmentType::Colour, 0).texture().get();
    EnqueueCommand(bufferInOut, computeMipMapsCommand);
}

void WaterPlane::updatePlaneEquation(const SceneGraphNode* sgn, Plane<F32>& plane, const bool reflection, const F32 offset) const {
    const F32 waterLevel = sgn->get<TransformComponent>()->getWorldPosition().y * (reflection ? -1.f : 1.f);
    const Quaternion<F32>& orientation = sgn->get<TransformComponent>()->getWorldOrientation();

    plane.set(Normalized(vec3<F32>(orientation * (reflection ? WORLD_Y_AXIS : WORLD_Y_NEG_AXIS))),  offset + waterLevel);
}

const vec3<U16>& WaterPlane::getDimensions() const noexcept {
    return _dimensions;
}

void WaterPlane::saveToXML(boost::property_tree::ptree& pt) const {
    pt.put("dimensions.<xmlattr>.width", _dimensions.width);
    pt.put("dimensions.<xmlattr>.length", _dimensions.height);
    pt.put("dimensions.<xmlattr>.depth", _dimensions.depth);

    SceneNode::saveToXML(pt);
}

void WaterPlane::loadFromXML(const boost::property_tree::ptree& pt) {
    _dimensions.width = pt.get<U16>("dimensions.<xmlattr>.width", _dimensions.width);
    _dimensions.height = pt.get<U16>("dimensions.<xmlattr>.length", _dimensions.height);
    _dimensions.depth = pt.get<U16>("dimensions.<xmlattr>.depth", _dimensions.depth);

    SceneNode::loadFromXML(pt);
}

}