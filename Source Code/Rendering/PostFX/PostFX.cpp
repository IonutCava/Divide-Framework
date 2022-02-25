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
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_SCREEN %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_SCREEN)).c_str(), true);
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_NOISE %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_NOISE)).c_str(), true);
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_BORDER %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_BORDER)).c_str(), true);
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_UNDERWATER %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_UNDERWATER)).c_str(), true);
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_SSR %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_SSR)).c_str(), true);
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_SCENE_DATA %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_SCENE_DATA)).c_str(), true);
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_SCENE_VELOCITY %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_SCENE_VELOCITY)).c_str(), true);
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_LINDEPTH %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_LINDEPTH)).c_str(), true);
    fragModule._defines.emplace_back(Util::StringFormat("TEX_BIND_POINT_DEPTH %d", to_base(TexOperatorBindPoint::TEX_BIND_POINT_DEPTH)).c_str(), true);

    ShaderProgramDescriptor postFXShaderDescriptor = {};
    postFXShaderDescriptor._modules.push_back(vertModule);
    postFXShaderDescriptor._modules.push_back(fragModule);

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
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        _drawPipeline = context.gfx().newPipeline(pipelineDescriptor);
    });

    WAIT_FOR_CONDITION(loadTasks.load() == 0);
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
    _setCameraCmd._cameraSnapshot = Camera::utilityCamera(Camera::UtilityCamera::_2D)->snapshot();
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
    static GFX::BeginRenderPassCommand s_beginRenderPassCmd{};
    static GFX::BindDescriptorSetsCommand s_descriptorSetCmd{};

    static size_t s_samplerHash = 0u;
    if (s_samplerHash == 0u) {
        SamplerDescriptor defaultSampler = {};
        defaultSampler.wrapUVW(TextureWrap::REPEAT);
        s_samplerHash = defaultSampler.getHash();

        s_beginRenderPassCmd._target = RenderTargetUsage::SCREEN;
        s_beginRenderPassCmd._descriptor = _postFXTarget;
        s_beginRenderPassCmd._name = "DO_POSTFX_PASS";

        TextureDataContainer& textureContainer = s_descriptorSetCmd._set._textureData;
        textureContainer.add(TextureEntry{ _underwaterTexture->data(),   s_samplerHash,       to_U8(TexOperatorBindPoint::TEX_BIND_POINT_UNDERWATER) });
        textureContainer.add(TextureEntry{ _noise->data(),               s_samplerHash,       to_U8(TexOperatorBindPoint::TEX_BIND_POINT_NOISE) });
        textureContainer.add(TextureEntry{ _screenBorder->data(),        s_samplerHash,       to_U8(TexOperatorBindPoint::TEX_BIND_POINT_BORDER) });
    }

    GFX::EnqueueCommand(bufferInOut, s_beginScopeCmd);
    GFX::EnqueueCommand(bufferInOut, _setCameraCmd);

    _preRenderBatch.execute(idx, cameraSnapshot, _filterStack | _overrideFilterStack, bufferInOut);

    GFX::EnqueueCommand(bufferInOut, s_beginRenderPassCmd);
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
    const auto& linDepthDataAtt = rtPool.renderTarget(RenderTargetUsage::LINEAR_DEPTH).getAttachment(RTAttachmentType::Colour, 0);
    const auto& ssrDataAtt = rtPool.renderTarget(RenderTargetUsage::SSR_RESULT).getAttachment(RTAttachmentType::Colour, 0);
    const auto& sceneDataAtt = rtPool.renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Colour, to_base(GFXDevice::ScreenTargets::NORMALS));
    const auto& velocityAtt = rtPool.renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Colour, to_base(GFXDevice::ScreenTargets::VELOCITY));
    const auto& depthAtt = rtPool.renderTarget(RenderTargetUsage::SCREEN).getAttachment(RTAttachmentType::Depth, 0);

    TextureDataContainer& textureContainer = s_descriptorSetCmd._set._textureData;
    textureContainer.add(TextureEntry{ prbAtt.texture()->data(),           prbAtt.samplerHash(),to_U8(TexOperatorBindPoint::TEX_BIND_POINT_SCREEN) });
    textureContainer.add(TextureEntry{ linDepthDataAtt.texture()->data(),  s_samplerHash,       to_U8(TexOperatorBindPoint::TEX_BIND_POINT_LINDEPTH) });
    textureContainer.add(TextureEntry{ depthAtt.texture()->data(),         s_samplerHash,       to_U8(TexOperatorBindPoint::TEX_BIND_POINT_DEPTH) });
    textureContainer.add(TextureEntry{ ssrDataAtt.texture()->data(),       s_samplerHash,       to_U8(TexOperatorBindPoint::TEX_BIND_POINT_SSR) });
    textureContainer.add(TextureEntry{ sceneDataAtt.texture()->data(),     s_samplerHash,       to_U8(TexOperatorBindPoint::TEX_BIND_POINT_SCENE_DATA) });
    textureContainer.add(TextureEntry{ velocityAtt.texture()->data(),      s_samplerHash,       to_U8(TexOperatorBindPoint::TEX_BIND_POINT_SCENE_VELOCITY) });
    GFX::EnqueueCommand(bufferInOut, s_descriptorSetCmd);

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
