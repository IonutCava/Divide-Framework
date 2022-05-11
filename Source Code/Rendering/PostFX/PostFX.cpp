#include "stdafx.h"

#include "Headers/PostFX.h"
#include "Headers/PreRenderOperator.h"

#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ApplicationTimer.h"

#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Rendering/Camera/Headers/Camera.h"

namespace Divide {

const char* PostFX::FilterName(const FilterType filter) noexcept {
    switch (filter) {
        case FilterType::FILTER_SS_ANTIALIASING:  return "SS_ANTIALIASING";
        case FilterType::FILTER_SS_REFLECTIONS:  return "SS_REFLECTIONS";
        case FilterType::FILTER_SS_AMBIENT_OCCLUSION:  return "SS_AMBIENT_OCCLUSION";
        case FilterType::FILTER_DEPTH_OF_FIELD:  return "DEPTH_OF_FIELD";
        case FilterType::FILTER_MOTION_BLUR:  return "MOTION_BLUR";
        case FilterType::FILTER_BLOOM: return "BLOOM";
        case FilterType::FILTER_LUT_CORECTION:  return "LUT_CORRECTION";
        case FilterType::FILTER_UNDERWATER: return "UNDERWATER";
        case FilterType::FILTER_NOISE: return "NOISE";
        case FilterType::FILTER_VIGNETTE: return "VIGNETTE";
        default: break;
    }

    return "Unknown";
};

PostFX::PostFX(PlatformContext& context, ResourceCache* cache)
    : PlatformContextComponent(context),
     _preRenderBatch(context.gfx(), *this, cache)
{
    std::atomic_uint loadTasks = 0u;

    context.paramHandler().setParam<bool>(_ID("postProcessing.enableVignette"), false);

    DisableAll(_postFXTarget._drawMask);
    SetEnabled(_postFXTarget._drawMask, RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO), true);

    Console::printfn(Locale::Get(_ID("START_POST_FX")));

    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "baseVertexShaders.glsl";
    vertModule._variant = "FullScreenQuad";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "postProcessing.glsl";
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_SCREEN %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_SCREEN)));
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_NOISE %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_NOISE)));
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_BORDER %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_BORDER)));
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_UNDERWATER %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_UNDERWATER)));
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_SSR %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_SSR)));
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_SCENE_DATA %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_SCENE_DATA)));
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_SCENE_VELOCITY %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_SCENE_VELOCITY)));
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_LINDEPTH %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_LINDEPTH)));
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_DEPTH %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_DEPTH)));

    ShaderProgramDescriptor postFXShaderDescriptor = {};
    postFXShaderDescriptor._modules.push_back(vertModule);
    postFXShaderDescriptor._modules.push_back(fragModule);
    postFXShaderDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

    _drawConstantsCmd._constants.set(_ID("_noiseTile"),   GFX::PushConstantType::FLOAT, 0.1f);
    _drawConstantsCmd._constants.set(_ID("_noiseFactor"), GFX::PushConstantType::FLOAT, 0.02f);
    _drawConstantsCmd._constants.set(_ID("_fadeActive"),  GFX::PushConstantType::BOOL,  false);
    _drawConstantsCmd._constants.set(_ID("_zPlanes"),     GFX::PushConstantType::VEC2,  vec2<F32>(0.01f, 500.0f));

    TextureDescriptor texDescriptor(TextureType::TEXTURE_2D);

    ImageTools::ImportOptions options;
    options._isNormalMap = true;
    options._useDDSCache = true;
    options._outputSRGB = false;
    options._alphaChannelTransparency = false;

    texDescriptor.textureOptions(options);

    ResourceDescriptor textureWaterCaustics("Underwater Normal Map");
    textureWaterCaustics.assetName(ResourcePath("terrain_water_NM.jpg"));
    textureWaterCaustics.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    textureWaterCaustics.propertyDescriptor(texDescriptor);
    textureWaterCaustics.waitForReady(false);
    _underwaterTexture = CreateResource<Texture>(cache, textureWaterCaustics, loadTasks);

    options._isNormalMap = false;
    texDescriptor.textureOptions(options);
    ResourceDescriptor noiseTexture("noiseTexture");
    noiseTexture.assetName(ResourcePath("bruit_gaussien.jpg"));
    noiseTexture.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    noiseTexture.propertyDescriptor(texDescriptor);
    noiseTexture.waitForReady(false);
    _noise = CreateResource<Texture>(cache, noiseTexture, loadTasks);

    ResourceDescriptor borderTexture("borderTexture");
    borderTexture.assetName(ResourcePath("vignette.jpeg"));
    borderTexture.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    borderTexture.propertyDescriptor(texDescriptor);
    borderTexture.waitForReady(false);
    _screenBorder = CreateResource<Texture>(cache, borderTexture), loadTasks;

    _noiseTimer = 0.0;
    _tickInterval = 1.0f / 24.0f;
    _randomNoiseCoefficient = 0;
    _randomFlashCoefficient = 0;

    ResourceDescriptor postFXShader("postProcessing");
    postFXShader.propertyDescriptor(postFXShaderDescriptor);
    _postProcessingShader = CreateResource<ShaderProgram>(cache, postFXShader, loadTasks);
    _postProcessingShader->addStateCallback(ResourceState::RES_LOADED, [this, &context](CachedResource*) {
        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateHash = context.gfx().get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = _postProcessingShader->handle();

        _drawPipeline = context.gfx().newPipeline(pipelineDescriptor);
    });

    WAIT_FOR_CONDITION(loadTasks.load() == 0);
}

PostFX::~PostFX()
{
}

void PostFX::updateResolution(const U16 newWidth, const U16 newHeight) {
    if (_resolutionCache.width == newWidth &&
        _resolutionCache.height == newHeight|| 
        newWidth < 1 || newHeight < 1)
    {
        return;
    }

    _resolutionCache.set(newWidth, newHeight);

    _preRenderBatch.reshape(newWidth, newHeight);
    _setCameraCmd._cameraSnapshot = Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot();
}

void PostFX::prePass(const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut) {
    static GFX::BeginDebugScopeCommand s_beginScopeCmd{ "PostFX: PrePass" };
    GFX::EnqueueCommand(bufferInOut, s_beginScopeCmd);
    GFX::EnqueueCommand<GFX::PushCameraCommand>(bufferInOut)->_cameraSnapshot = _setCameraCmd._cameraSnapshot;

    _preRenderBatch.prePass(idx, cameraSnapshot, _filterStack | _overrideFilterStack, bufferInOut);

    GFX::EnqueueCommand<GFX::PopCameraCommand>(bufferInOut);
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void PostFX::apply(const PlayerIndex idx, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut) {
    static GFX::BeginDebugScopeCommand s_beginScopeCmd{ "PostFX: Apply" };

    GFX::EnqueueCommand(bufferInOut, s_beginScopeCmd);
    GFX::EnqueueCommand(bufferInOut, _setCameraCmd);

    _preRenderBatch.execute(idx, cameraSnapshot, _filterStack | _overrideFilterStack, bufferInOut);

    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._target = RenderTargetNames::SCREEN;
    beginRenderPassCmd._descriptor = _postFXTarget;
    beginRenderPassCmd._name = "DO_POSTFX_PASS";
    GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);
    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _drawPipeline });

    if (_filtersDirty) {
        _drawConstantsCmd._constants.set(_ID("vignetteEnabled"),      GFX::PushConstantType::BOOL, getFilterState(FilterType::FILTER_VIGNETTE));
        _drawConstantsCmd._constants.set(_ID("noiseEnabled"),         GFX::PushConstantType::BOOL, getFilterState(FilterType::FILTER_NOISE));
        _drawConstantsCmd._constants.set(_ID("underwaterEnabled"),    GFX::PushConstantType::BOOL, getFilterState(FilterType::FILTER_UNDERWATER));
        _drawConstantsCmd._constants.set(_ID("lutCorrectionEnabled"), GFX::PushConstantType::BOOL, getFilterState(FilterType::FILTER_LUT_CORECTION));
        _filtersDirty = false;
    };

    _drawConstantsCmd._constants.set(_ID("_zPlanes"), GFX::PushConstantType::VEC2, cameraSnapshot._zPlanes);
    _drawConstantsCmd._constants.set(_ID("_invProjectionMatrix"), GFX::PushConstantType::VEC2, cameraSnapshot._invProjectionMatrix);

    GFX::EnqueueCommand(bufferInOut, _drawConstantsCmd);
    const auto& rtPool = context().gfx().renderTargetPool();
    const auto& prbAtt = _preRenderBatch.getOutput(false)._rt->getAttachment(RTAttachmentType::Colour, 0);
    const auto& linDepthDataAtt =_preRenderBatch.getLinearDepthRT()._rt->getAttachment(RTAttachmentType::Colour, 0);
    const auto& ssrDataAtt = rtPool.getRenderTarget(RenderTargetNames::SSR_RESULT)->getAttachment(RTAttachmentType::Colour, 0);
    const auto& sceneDataAtt = rtPool.getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::Colour, to_base(GFXDevice::ScreenTargets::NORMALS));
    const auto& velocityAtt = rtPool.getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::Colour, to_base(GFXDevice::ScreenTargets::VELOCITY));
    const auto& depthAtt = rtPool.getRenderTarget(RenderTargetNames::SCREEN)->getAttachment(RTAttachmentType::Depth, 0);

    SamplerDescriptor defaultSampler = {};
    defaultSampler.wrapUVW(TextureWrap::REPEAT);
    const size_t samplerHash = defaultSampler.getHash();

    DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
    set._usage = DescriptorSetUsage::PER_DRAW_SET;
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_SCREEN);
        binding._data._combinedImageSampler._image = prbAtt.texture()->data();
        binding._data._combinedImageSampler._samplerHash = prbAtt.samplerHash();
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_DEPTH);
        binding._data._combinedImageSampler._image = depthAtt.texture()->data();
        binding._data._combinedImageSampler._samplerHash = samplerHash;
    }   
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_LINDEPTH);
        binding._data._combinedImageSampler._image = linDepthDataAtt.texture()->data();
        binding._data._combinedImageSampler._samplerHash = samplerHash;
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_SSR);
        binding._data._combinedImageSampler._image = ssrDataAtt.texture()->data();
        binding._data._combinedImageSampler._samplerHash = samplerHash;
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_SCENE_DATA);
        binding._data._combinedImageSampler._image = sceneDataAtt.texture()->data();
        binding._data._combinedImageSampler._samplerHash = samplerHash;
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_SCENE_VELOCITY);
        binding._data._combinedImageSampler._image = velocityAtt.texture()->data();
        binding._data._combinedImageSampler._samplerHash = samplerHash;
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_UNDERWATER);
        binding._data._combinedImageSampler._image = _underwaterTexture->data();
        binding._data._combinedImageSampler._samplerHash = samplerHash;
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_NOISE);
        binding._data._combinedImageSampler._image = _noise->data();
        binding._data._combinedImageSampler._samplerHash = samplerHash;
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TexOperatorBindPoint::TEX_BIND_POINT_BORDER);
        binding._data._combinedImageSampler._image = _screenBorder->data();
        binding._data._combinedImageSampler._samplerHash = samplerHash;
    }

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);

    GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void PostFX::idle(const Configuration& config) {
    OPTICK_EVENT();

    // Update states
    if (getFilterState(FilterType::FILTER_NOISE)) {
        _noiseTimer += Time::Game::ElapsedMilliseconds();
        if (_noiseTimer > _tickInterval) {
            _noiseTimer = 0.0;
            _randomNoiseCoefficient = Random(1000) * 0.001f;
            _randomFlashCoefficient = Random(1000) * 0.001f;
        }

        _drawConstantsCmd._constants.set(_ID("randomCoeffNoise"), GFX::PushConstantType::FLOAT, _randomNoiseCoefficient);
        _drawConstantsCmd._constants.set(_ID("randomCoeffFlash"), GFX::PushConstantType::FLOAT, _randomFlashCoefficient);
    }
}

void PostFX::update(const U64 deltaTimeUSFixed, const U64 deltaTimeUSApp) {
    OPTICK_EVENT();

    if (_fadeActive) {
        _currentFadeTimeMS += Time::MicrosecondsToMilliseconds<D64>(deltaTimeUSApp);
        F32 fadeStrength = to_F32(std::min(_currentFadeTimeMS / _targetFadeTimeMS , 1.0));
        if (!_fadeOut) {
            fadeStrength = 1.0f - fadeStrength;
        }

        if (fadeStrength > 0.99) {
            if (_fadeWaitDurationMS < std::numeric_limits<D64>::epsilon()) {
                if (_fadeOutComplete) {
                    _fadeOutComplete();
                    _fadeOutComplete = DELEGATE<void>();
                }
            } else {
                _fadeWaitDurationMS -= Time::MicrosecondsToMilliseconds<D64>(deltaTimeUSApp);
            }
        }

        _drawConstantsCmd._constants.set(_ID("_fadeStrength"), GFX::PushConstantType::FLOAT, fadeStrength);
        
        _fadeActive = fadeStrength > std::numeric_limits<D64>::epsilon();
        if (!_fadeActive) {
            _drawConstantsCmd._constants.set(_ID("_fadeActive"), GFX::PushConstantType::BOOL, false);
            if (_fadeInComplete) {
                _fadeInComplete();
                _fadeInComplete = DELEGATE<void>();
            }
        }
    }

    _preRenderBatch.update(deltaTimeUSApp);
}

void PostFX::setFadeOut(const UColour3& targetColour, const D64 durationMS, const D64 waitDurationMS, DELEGATE<void> onComplete) {
    _drawConstantsCmd._constants.set(_ID("_fadeColour"), GFX::PushConstantType::VEC4, Util::ToFloatColour(targetColour));
    _drawConstantsCmd._constants.set(_ID("_fadeActive"), GFX::PushConstantType::BOOL, true);
    _targetFadeTimeMS = durationMS;
    _currentFadeTimeMS = 0.0;
    _fadeWaitDurationMS = waitDurationMS;
    _fadeOut = true;
    _fadeActive = true;
    _fadeOutComplete = MOV(onComplete);
}

// clear any fading effect currently active over the specified time interval
// set durationMS to instantly clear the fade effect
void PostFX::setFadeIn(const D64 durationMS, DELEGATE<void> onComplete) {
    _targetFadeTimeMS = durationMS;
    _currentFadeTimeMS = 0.0;
    _fadeOut = false;
    _fadeActive = true;
    _drawConstantsCmd._constants.set(_ID("_fadeActive"), GFX::PushConstantType::BOOL, true);
    _fadeInComplete = MOV(onComplete);
}

void PostFX::setFadeOutIn(const UColour3& targetColour, const D64 durationFadeOutMS, const D64 waitDurationMS) {
    if (waitDurationMS > 0.0) {
        setFadeOutIn(targetColour, waitDurationMS * 0.5, waitDurationMS * 0.5, durationFadeOutMS);
    }
}

void PostFX::setFadeOutIn(const UColour3& targetColour, const D64 durationFadeOutMS, const D64 durationFadeInMS, const D64 waitDurationMS) {
    setFadeOut(targetColour, durationFadeOutMS, waitDurationMS, [this, durationFadeInMS]() {setFadeIn(durationFadeInMS); });
}

};
