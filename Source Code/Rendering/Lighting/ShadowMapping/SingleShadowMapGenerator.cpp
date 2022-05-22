#include "stdafx.h"

#include "Headers/SingleShadowMapGenerator.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Scenes/Headers/SceneState.h"
#include "Managers/Headers/SceneManager.h"
#include "Managers/Headers/RenderPassManager.h"

#include "Rendering/Camera/Headers/Camera.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include "ECS/Components/Headers/SpotLightComponent.h"

namespace Divide {

namespace {
    Configuration::Rendering::ShadowMapping g_shadowSettings;
};

SingleShadowMapGenerator::SingleShadowMapGenerator(GFXDevice& context)
    : ShadowMapGenerator(context, ShadowType::SINGLE)
{

    Console::printfn(Locale::Get(_ID("LIGHT_CREATE_SHADOW_FB")), "Single Shadow Map");

    g_shadowSettings = context.context().config().rendering.shadowMapping;
    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor geomModule = {};
        geomModule._moduleType = ShaderType::GEOMETRY;
        geomModule._sourceFile = "blur.glsl";
        geomModule._variant = "GaussBlur";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "blur.glsl";
        fragModule._variant = "GaussBlur.Layered";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(geomModule);
        shaderDescriptor._modules.push_back(fragModule);
        shaderDescriptor._globalDefines.emplace_back(Util::StringFormat("GS_MAX_INVOCATIONS %d", Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS));

        ResourceDescriptor blurDepthMapShader(Util::StringFormat("GaussBlur_%d_invocations", Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS).c_str());
        blurDepthMapShader.waitForReady(true);
        blurDepthMapShader.propertyDescriptor(shaderDescriptor);

        _blurDepthMapShader = CreateResource<ShaderProgram>(context.parent().resourceCache(), blurDepthMapShader);
        _blurDepthMapShader->addStateCallback(ResourceState::RES_LOADED, [this](CachedResource*) {
            PipelineDescriptor pipelineDescriptor = {};
            pipelineDescriptor._stateHash = _context.get2DStateBlock();
            pipelineDescriptor._shaderProgramHandle = _blurDepthMapShader->handle();
            pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

            _blurPipeline = _context.newPipeline(pipelineDescriptor);
        });
    }

    _shaderConstants.set(_ID("layerCount"), GFX::PushConstantType::INT, 1);
    _shaderConstants.set(_ID("layerOffsetRead"), GFX::PushConstantType::INT, (I32)0);
    _shaderConstants.set(_ID("layerOffsetWrite"), GFX::PushConstantType::INT, (I32)0);

    std::array<vec2<F32>, Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT> blurSizes;
    blurSizes.fill(vec2<F32>(1.0f / g_shadowSettings.spot.shadowMapResolution));
    for (U8 i = 1; i < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++i) {
        blurSizes[i] = blurSizes[i - 1] / 2;
    }

    _shaderConstants.set(_ID("blurSizes"), GFX::PushConstantType::VEC2, blurSizes);
    SamplerDescriptor sampler = {};
    sampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    sampler.mipSampling(TextureMipSampling::NONE);
    sampler.anisotropyLevel(0);
    const size_t samplerHash = sampler.getHash();

    const RenderTarget* rt = ShadowMap::getShadowMap(_type)._rt;
    const TextureDescriptor& texDescriptor = rt->getAttachment(RTAttachmentType::Colour, 0)->texture()->descriptor();

    //Draw FBO
    {
        RenderTargetDescriptor desc = {};
        desc._resolution = rt->getResolution();

        TextureDescriptor colourDescriptor(TextureType::TEXTURE_2D, texDescriptor.baseFormat(), texDescriptor.dataType());
        colourDescriptor.msaaSamples(g_shadowSettings.spot.MSAASamples);
        colourDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        TextureDescriptor depthDescriptor(TextureType::TEXTURE_2D, GFXImageFormat::DEPTH_COMPONENT, GFXDataFormat::UNSIGNED_INT);
        depthDescriptor.msaaSamples(g_shadowSettings.spot.MSAASamples);
        depthDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        InternalRTAttachmentDescriptors att {
            InternalRTAttachmentDescriptor{ colourDescriptor, samplerHash, RTAttachmentType::Colour },
            InternalRTAttachmentDescriptor{ depthDescriptor, samplerHash, RTAttachmentType::Depth_Stencil }
        };

        desc._name = "Single_ShadowMap_Draw";
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();
        desc._msaaSamples = g_shadowSettings.spot.MSAASamples;

        _drawBufferDepth = context.renderTargetPool().allocateRT(desc);
    }

    //Blur FBO
    {
        TextureDescriptor blurMapDescriptor(TextureType::TEXTURE_2D, texDescriptor.baseFormat(), texDescriptor.dataType());
        blurMapDescriptor.layerCount(1u);
        blurMapDescriptor.mipMappingState(TextureDescriptor::MipMappingState::OFF);

        InternalRTAttachmentDescriptors att {
            InternalRTAttachmentDescriptor{ blurMapDescriptor, samplerHash, RTAttachmentType::Colour }
        };

        RenderTargetDescriptor desc = {};
        desc._name = "Single_Blur";
        desc._resolution = rt->getResolution();
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();

        _blurBuffer = _context.renderTargetPool().allocateRT(desc);
    }

    WAIT_FOR_CONDITION(_blurPipeline != nullptr);
}

SingleShadowMapGenerator::~SingleShadowMapGenerator()
{
    if (!_context.renderTargetPool().deallocateRT(_drawBufferDepth)) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void SingleShadowMapGenerator::render([[maybe_unused]] const Camera& playerCamera, Light& light, U16 lightIndex, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    OPTICK_EVENT();

    const SpotLightComponent& spotLight = static_cast<SpotLightComponent&>(light);

    const vec3<F32> lightPos = light.positionCache();
    const F32 farPlane = light.range() * 1.2f;

    auto& shadowCameras = ShadowMap::shadowCameras(ShadowType::SINGLE);
    const mat4<F32> viewMatrix = shadowCameras[0]->lookAt(lightPos, lightPos + light.directionCache() * farPlane);
    const mat4<F32> projectionMatrix = shadowCameras[0]->setProjection(1.0f, 90.0f, vec2<F32>(0.01f, farPlane));
    shadowCameras[0]->updateLookAt();

    mat4<F32> lightVP = light.getShadowVPMatrix(0);
    mat4<F32>::Multiply(viewMatrix, projectionMatrix, lightVP);
    light.setShadowLightPos(0, lightPos);
    light.setShadowFloatValue(0, shadowCameras[0]->getZPlanes().y);
    light.setShadowVPMatrix(0, mat4<F32>::Multiply(lightVP, MAT4_BIAS));

    RenderPassParams params = {};
    params._sourceNode = light.getSGN();
    params._stagePass = { RenderStage::SHADOW, RenderPassType::COUNT, lightIndex, static_cast<RenderStagePass::VariantType>(light.getLightType()) };
    params._target = _drawBufferDepth._targetID;
    params._passName = "SingleShadowMap";
    params._maxLoD = -1;

    RTClearDescriptor clearDescriptor = {};
    clearDescriptor._clearDepth = true;
    clearDescriptor._clearColours = true;
    clearDescriptor._resetToDefault = true;

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Single Shadow Pass Light: [ %d ]", lightIndex).c_str(), lightIndex));
    GFX::EnqueueCommand<GFX::SetClippingStateCommand>(bufferInOut)->_negativeOneToOneDepth = false;

    GFX::ClearRenderTargetCommand clearMainTarget = {};
    clearMainTarget._target = params._target;
    clearMainTarget._descriptor = clearDescriptor;
    GFX::EnqueueCommand(bufferInOut, clearMainTarget);

    _context.parent().renderPassManager()->doCustomPass(shadowCameras[0], params, bufferInOut, memCmdInOut);

    postRender(spotLight, bufferInOut);

    GFX::EnqueueCommand<GFX::SetClippingStateCommand>(bufferInOut)->_negativeOneToOneDepth = true;
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void SingleShadowMapGenerator::postRender(const SpotLightComponent& light, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    const RenderTargetHandle& handle = ShadowMap::getShadowMap(_type);

    const RenderTarget* shadowMapRT = handle._rt;
    const auto& shadowAtt = shadowMapRT->getAttachment(RTAttachmentType::Colour, 0);

    const U16 layerOffset = light.getShadowArrayOffset();
    constexpr I32 layerCount = 1;

    GFX::BlitRenderTargetCommand blitRenderTargetCommand = {};
    blitRenderTargetCommand._source = _drawBufferDepth._targetID;
    blitRenderTargetCommand._destination = handle._targetID;
    blitRenderTargetCommand._blitColours[0].set(0u, 0u, 0u, layerOffset);
    EnqueueCommand(bufferInOut, blitRenderTargetCommand);


    // Now we can either blur our target or just skip to mipmap computation
    if (g_shadowSettings.spot.enableBlurring) {
        _shaderConstants.set(_ID("layerCount"), GFX::PushConstantType::INT, layerCount);

        GFX::BindDescriptorSetsCommand descriptorSetCmd{};
        GFX::BeginRenderPassCommand beginRenderPassCmd{};
        GFX::SendPushConstantsCommand pushConstantsCommand{};

        // Blur horizontally
        beginRenderPassCmd._target = _blurBuffer._targetID;
        beginRenderPassCmd._name = "DO_CSM_BLUR_PASS_HORIZONTAL";

        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ _blurPipeline });
        {
            DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
            set._usage = DescriptorSetUsage::PER_DRAW_SET;
            auto& binding = set._bindings.emplace_back();
            binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
            binding._resourceSlot = to_U8(TextureUsage::UNIT0);
            binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
            binding._data.As<DescriptorCombinedImageSampler>() = { shadowAtt->texture()->data(), shadowAtt->descriptor()._samplerHash };
        }

        _shaderConstants.set(_ID("layered"), GFX::PushConstantType::BOOL, true);
        _shaderConstants.set(_ID("verticalBlur"), GFX::PushConstantType::BOOL, false);
        _shaderConstants.set(_ID("layerOffsetRead"), GFX::PushConstantType::INT, layerOffset);
        _shaderConstants.set(_ID("layerOffsetWrite"), GFX::PushConstantType::INT, 0);

        pushConstantsCommand._constants = _shaderConstants;
        GFX::EnqueueCommand(bufferInOut, pushConstantsCommand);
        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

        // Blur vertically
        const auto& blurAtt = _blurBuffer._rt->getAttachment(RTAttachmentType::Colour, 0);
        {
            DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
            set._usage = DescriptorSetUsage::PER_DRAW_SET;
            auto& binding = set._bindings.emplace_back();
            binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
            binding._resourceSlot = to_U8(TextureUsage::UNIT0);
            binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
            binding._data.As<DescriptorCombinedImageSampler>() = { blurAtt->texture()->data(), blurAtt->descriptor()._samplerHash };
        }

        beginRenderPassCmd._target = handle._targetID;
        beginRenderPassCmd._descriptor = {};
        beginRenderPassCmd._name = "DO_CSM_BLUR_PASS_VERTICAL";
        GFX::EnqueueCommand(bufferInOut, beginRenderPassCmd);

        pushConstantsCommand._constants.set(_ID("verticalBlur"), GFX::PushConstantType::BOOL, true);
        pushConstantsCommand._constants.set(_ID("layerOffsetRead"), GFX::PushConstantType::INT, 0);
        pushConstantsCommand._constants.set(_ID("layerOffsetWrite"), GFX::PushConstantType::INT, layerOffset);

        GFX::EnqueueCommand(bufferInOut, pushConstantsCommand);
        GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
    }
}

void SingleShadowMapGenerator::updateMSAASampleCount(const U8 sampleCount) {
    if (_context.context().config().rendering.shadowMapping.spot.MSAASamples != sampleCount) {
        _context.context().config().rendering.shadowMapping.spot.MSAASamples = sampleCount;
        _drawBufferDepth._rt->updateSampleCount(sampleCount);
    }
}
};
