#include "stdafx.h"

#include "Headers/SceneEnvironmentProbePool.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Scenes/Headers/Scene.h"
#include "Graphs/Headers/SceneNode.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/PushConstants.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"
#include "ECS/Components/Headers/EnvironmentProbeComponent.h"

#include "Headers/SceneShaderData.h"

namespace Divide {
namespace {
    SharedMutex s_queueLock;
    constexpr U16 s_IrradianceTextureSize = 64u;
    constexpr U16 s_LUTTextureSize = 128u;

    eastl::queue<U16> s_computationQueue;
    eastl::set<U16> s_computationSet;
    U16 s_queuedLayer = 0u;

    static SceneEnvironmentProbePool::ComputationStages s_queuedStage = SceneEnvironmentProbePool::ComputationStages::COUNT;
    static U8 s_faceID = 0u;
}

vector<DebugView_ptr> SceneEnvironmentProbePool::s_debugViews;
vector<Camera*> SceneEnvironmentProbePool::s_probeCameras;
bool SceneEnvironmentProbePool::s_probesDirty = true;

std::array<SceneEnvironmentProbePool::ProbeSlice, Config::MAX_REFLECTIVE_PROBES_PER_PASS> SceneEnvironmentProbePool::s_availableSlices;
RenderTargetHandle SceneEnvironmentProbePool::s_reflection;
RenderTargetHandle SceneEnvironmentProbePool::s_prefiltered;
RenderTargetHandle SceneEnvironmentProbePool::s_irradiance;
RenderTargetHandle SceneEnvironmentProbePool::s_brdfLUT;
ShaderProgram_ptr SceneEnvironmentProbePool::s_previewShader;
ShaderProgram_ptr SceneEnvironmentProbePool::s_irradianceComputeShader;
ShaderProgram_ptr SceneEnvironmentProbePool::s_prefilterComputeShader;
ShaderProgram_ptr SceneEnvironmentProbePool::s_lutComputeShader;
Pipeline* SceneEnvironmentProbePool::s_pipelineCalcPrefiltered = nullptr;
Pipeline* SceneEnvironmentProbePool::s_pipelineCalcIrradiance = nullptr;
bool SceneEnvironmentProbePool::s_debuggingSkyLight = false;
bool SceneEnvironmentProbePool::s_skyLightNeedsRefresh = true;
bool SceneEnvironmentProbePool::s_lutTextureDirty = true;

SceneEnvironmentProbePool::SceneEnvironmentProbePool(Scene& parentScene) noexcept
    : SceneComponent(parentScene)
{

}

SceneEnvironmentProbePool::~SceneEnvironmentProbePool() 
{
}

I16 SceneEnvironmentProbePool::AllocateSlice(const bool lock) {
    static_assert(Config::MAX_REFLECTIVE_PROBES_PER_PASS < I16_MAX);

    for (U32 i = 0; i < Config::MAX_REFLECTIVE_PROBES_PER_PASS; ++i) {
        if (s_availableSlices[i]._available) {
            s_availableSlices[i] = { false, lock };
            return to_I16(i);
        }
    }

    if_constexpr (Config::Build::IS_DEBUG_BUILD) {
        DIVIDE_UNEXPECTED_CALL();
    }

    return -1;
}

void SceneEnvironmentProbePool::UnlockSlice(const I16 slice) noexcept {
    s_availableSlices[slice] = { true, false };
}

void SceneEnvironmentProbePool::OnStartup(GFXDevice& context) {
    SkyLightNeedsRefresh(true);

    for (U32 i = 0; i < 6; ++i) {
        s_probeCameras.emplace_back(Camera::CreateCamera<FreeFlyCamera>(Util::StringFormat("ProbeCamera_%d", i)));
    }

    s_availableSlices.fill({});

    // Reflection Targets
    SamplerDescriptor reflectionSampler = {};
    reflectionSampler.wrapUVW(TextureWrap::CLAMP_TO_EDGE);
    reflectionSampler.mipSampling(TextureMipSampling::LINEAR);
    reflectionSampler.anisotropyLevel(context.context().config().rendering.maxAnisotropicFilteringLevel);
    const size_t samplerHash = reflectionSampler.getHash();


    const U32 reflectRes = to_U32(context.context().config().rendering.reflectionProbeResolution);
    {
        TextureDescriptor environmentDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA);
        environmentDescriptor.layerCount(Config::MAX_REFLECTIVE_PROBES_PER_PASS + 1u);
        environmentDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);

        TextureDescriptor depthDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXDataFormat::UNSIGNED_INT, GFXImageFormat::DEPTH_COMPONENT);
        depthDescriptor.layerCount(Config::MAX_REFLECTIVE_PROBES_PER_PASS + 1u);
        depthDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);

        InternalRTAttachmentDescriptors att {
            InternalRTAttachmentDescriptor{ environmentDescriptor, samplerHash, RTAttachmentType::Colour, 0u, DefaultColours::WHITE },
            InternalRTAttachmentDescriptor{ depthDescriptor, samplerHash, RTAttachmentType::Depth_Stencil },
        };

        RenderTargetDescriptor desc = {};
        desc._name = "ReflectionEnvMap";
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();
        desc._resolution.set(reflectRes, reflectRes);
        s_reflection = context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor environmentDescriptor(TextureType::TEXTURE_CUBE_ARRAY, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA);
        environmentDescriptor.layerCount(Config::MAX_REFLECTIVE_PROBES_PER_PASS + 1u);
        environmentDescriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
        environmentDescriptor.rwFlag(ImageFlag::READ_WRITE);
        InternalRTAttachmentDescriptors att{
            InternalRTAttachmentDescriptor{ environmentDescriptor, samplerHash, RTAttachmentType::Colour, 0u, DefaultColours::WHITE },
        };
        RenderTargetDescriptor desc = {};
        desc._name = "PrefilteredEnvMap";
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();
        desc._resolution.set(reflectRes, reflectRes);
        s_prefiltered = context.renderTargetPool().allocateRT(desc);
        desc._name = "IrradianceEnvMap";
        desc._resolution.set(s_IrradianceTextureSize, s_IrradianceTextureSize);
        s_irradiance = context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor environmentDescriptor(TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RG);
        environmentDescriptor.mipMappingState(TextureDescriptor::MipMappingState::AUTO);
        environmentDescriptor.rwFlag(ImageFlag::READ_WRITE);
        InternalRTAttachmentDescriptors att {
            InternalRTAttachmentDescriptor{ environmentDescriptor, samplerHash, RTAttachmentType::Colour, 0u, DefaultColours::WHITE },
        };
        RenderTargetDescriptor desc = {};
        desc._name = "BrdfLUT";
        desc._attachmentCount = to_U8(att.size());
        desc._attachments = att.data();
        desc._resolution.set(s_LUTTextureSize, s_LUTTextureSize);
        s_brdfLUT = context.renderTargetPool().allocateRT(desc);
    }
    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "baseVertexShaders.glsl";
        vertModule._variant = "FullScreenQuad";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "fbPreview.glsl";
        fragModule._variant = "Cube";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(vertModule);
        shaderDescriptor._modules.push_back(fragModule);

        ResourceDescriptor shadowPreviewShader("fbPreview.Cube");
        shadowPreviewShader.propertyDescriptor(shaderDescriptor);
        shadowPreviewShader.waitForReady(true);
        s_previewShader = CreateResource<ShaderProgram>(context.parent().resourceCache(), shadowPreviewShader);
    }
    {
        ShaderModuleDescriptor computeModule = {};
        computeModule._moduleType = ShaderType::COMPUTE;
        computeModule._sourceFile = "irradianceCalc.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(computeModule);

        {
            shaderDescriptor._modules.back()._variant = "Irradiance";
            ResourceDescriptor irradianceShader("IrradianceCalc");
            irradianceShader.propertyDescriptor(shaderDescriptor);
            irradianceShader.waitForReady(true);
            s_irradianceComputeShader = CreateResource<ShaderProgram>(context.parent().resourceCache(), irradianceShader);
        }
        {
            shaderDescriptor._modules.back()._variant = "LUT";
            ResourceDescriptor lutShader("LUTCalc");
            lutShader.propertyDescriptor(shaderDescriptor);
            lutShader.waitForReady(true);
            s_lutComputeShader = CreateResource<ShaderProgram>(context.parent().resourceCache(), lutShader);
        }
        {
            shaderDescriptor._modules.back()._variant = "PreFilter";
            ResourceDescriptor prefilterShader("PrefilterEnv");
            prefilterShader.propertyDescriptor(shaderDescriptor);
            prefilterShader.waitForReady(true);
            s_prefilterComputeShader = CreateResource<ShaderProgram>(context.parent().resourceCache(), prefilterShader);
        }
    }
    {
        PipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor._stateHash = context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = s_prefilterComputeShader->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        s_pipelineCalcPrefiltered = context.newPipeline(pipelineDescriptor);
    }
    {
        PipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor._stateHash = context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = s_irradianceComputeShader->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        s_pipelineCalcIrradiance = context.newPipeline(pipelineDescriptor);
    }
}

void SceneEnvironmentProbePool::OnShutdown(GFXDevice& context) {
    s_previewShader.reset();
    s_irradianceComputeShader.reset();
    s_prefilterComputeShader.reset();
    s_lutComputeShader.reset();
    if (!context.renderTargetPool().deallocateRT(s_reflection) ||
        !context.renderTargetPool().deallocateRT(s_prefiltered) ||
        !context.renderTargetPool().deallocateRT(s_irradiance) ||
        !context.renderTargetPool().deallocateRT(s_brdfLUT)) 
    {
        DIVIDE_UNEXPECTED_CALL();
    }
    for (auto& camera : s_probeCameras) {
        Camera::DestroyCamera(camera);
    }
    s_probeCameras.clear();
    // Remove old views
    if (!s_debugViews.empty()) {
        for (const DebugView_ptr& view : s_debugViews) {
            context.removeDebugView(view.get());
        }
        s_debugViews.clear();
    }
    s_pipelineCalcPrefiltered = nullptr;
    s_pipelineCalcIrradiance = nullptr;

    s_computationQueue = {};
    s_computationSet.clear();
    s_queuedLayer = 0u;
    s_queuedStage = SceneEnvironmentProbePool::ComputationStages::COUNT;
    s_faceID = 0u;
}

RenderTargetHandle SceneEnvironmentProbePool::ReflectionTarget() noexcept {
    return s_reflection;
}

RenderTargetHandle SceneEnvironmentProbePool::PrefilteredTarget() noexcept {
    return s_prefiltered;
}

RenderTargetHandle SceneEnvironmentProbePool::IrradianceTarget() noexcept {
    return s_irradiance;
}

RenderTargetHandle SceneEnvironmentProbePool::BRDFLUTTarget() noexcept {
    return s_brdfLUT;
}

const EnvironmentProbeList& SceneEnvironmentProbePool::sortAndGetLocked(const vec3<F32>& position) {
    eastl::sort(begin(_envProbes),
                end(_envProbes),
                [&position](const auto& a, const auto& b) noexcept -> bool {
                    return a->distanceSqTo(position) < b->distanceSqTo(position);
                });

    return _envProbes;
}

void SceneEnvironmentProbePool::Prepare(GFX::CommandBuffer& bufferInOut) {
    for (U16 i = 0u; i < Config::MAX_REFLECTIVE_PROBES_PER_PASS; ++i) {
        if (!s_availableSlices[i]._locked) {
            s_availableSlices[i]._available = true;
        }
    }

    if (ProbesDirty()) {
        GFX::ClearRenderTargetCommand clearMainTarget = {};
        clearMainTarget._target = s_reflection._targetID;
        clearMainTarget._descriptor._clearDepth = true;
        clearMainTarget._descriptor._clearColours = false;
        clearMainTarget._descriptor._resetToDefault = true;
        EnqueueCommand(bufferInOut, clearMainTarget);

        ProbesDirty(false);
    }
}

void SceneEnvironmentProbePool::UpdateSkyLight(GFXDevice& context, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    OPTICK_EVENT();
    {
        SharedLock<SharedMutex> r_lock(s_queueLock);
        if (s_queuedStage == ComputationStages::COUNT && !s_computationQueue.empty()) {
            s_queuedLayer = s_computationQueue.front();
            s_queuedStage = ComputationStages::MIP_MAP_SOURCE;
            s_computationQueue.pop();
            s_computationSet.erase(s_queuedLayer);
        }

        if (s_queuedStage != ComputationStages::COUNT)
        //while (queuedStage != ComputationStages::COUNT) 
        {
            ProcessEnvironmentMapInternal(context, s_queuedLayer, s_queuedStage, bufferInOut);
        }
    }
    if (s_lutTextureDirty) {
        PipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor._stateHash = context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = s_lutComputeShader->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;

        const Pipeline* pipelineCalcLut = context.newPipeline(pipelineDescriptor);

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ pipelineCalcLut });

        Texture* brdfLutTexture = SceneEnvironmentProbePool::BRDFLUTTarget()._rt->getAttachment(RTAttachmentType::Colour, 0)->texture().get();
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;

        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
        binding._slot = 0;
        binding._data.As<ImageView>() = brdfLutTexture->getView(TextureType::TEXTURE_2D, {0u, 1u}, { 0u, 1u }, ImageFlag::WRITE);

        const U32 groupsX = to_U32(std::ceil(s_LUTTextureSize / to_F32(8)));
        const U32 groupsY = to_U32(std::ceil(s_LUTTextureSize / to_F32(8)));
        GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ groupsX, groupsY, 1 });

        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>(bufferInOut)->_barrierMask = to_base(MemoryBarrierType::TEXTURE_FETCH);

        GFX::EnqueueCommand<GFX::ComputeMipMapsCommand>(bufferInOut)->_texture = brdfLutTexture;
        s_lutTextureDirty = false;
    }
    {
        RTAttachment* prefiltered = SceneEnvironmentProbePool::PrefilteredTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);
        RTAttachment* irradiance = SceneEnvironmentProbePool::IrradianceTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);
        RTAttachment* brdfLut = SceneEnvironmentProbePool::BRDFLUTTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_FRAME;
        {
            auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
            binding._slot = 0;
            binding._data.As<DescriptorCombinedImageSampler>() = { prefiltered->texture()->defaultView(), prefiltered->descriptor()._samplerHash };
        }
        {
            auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
            binding._slot = 1;
            binding._data.As<DescriptorCombinedImageSampler>() = { irradiance->texture()->defaultView(), irradiance->descriptor()._samplerHash };
        }
        {
            auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
            binding._slot = 2;
            binding._data.As<DescriptorCombinedImageSampler>() = { brdfLut->texture()->defaultView(), brdfLut->descriptor()._samplerHash };
        }
    }

    if (!SkyLightNeedsRefresh() || s_queuedLayer == SkyProbeLayerIndex()) {
        return;
    }

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand("SkyLight Pass"));

    vector<Camera*>& probeCameras = ProbeCameras();
    std::array<Camera*, 6> cameras = {};
    std::copy_n(std::begin(probeCameras), std::min(cameras.size(), probeCameras.size()), std::begin(cameras));

    RenderPassParams params = {};
    params._target = SceneEnvironmentProbePool::ReflectionTarget()._targetID;
    params._stagePass = { RenderStage::REFLECTION, RenderPassType::COUNT, Config::MAX_REFLECTIVE_NODES_IN_VIEW + SkyProbeLayerIndex(), static_cast<RenderStagePass::VariantType>(ReflectorType::CUBE) };

    ClearBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES)));
    ClearBit(params._drawMask, to_U8(1u << to_base(RenderPassParams::Flags::DRAW_STATIC_NODES)));
    SetBit(params._drawMask,   to_U8(1u << to_base(RenderPassParams::Flags::DRAW_SKY_NODES)));

    context.generateCubeMap(params,
                            SkyProbeLayerIndex(),
                            VECTOR3_ZERO,
                            vec2<F32>(0.1f, 10000.f),
                            bufferInOut,
                            memCmdInOut,
                            cameras);

    ProcessEnvironmentMap(context, SkyProbeLayerIndex(), false, bufferInOut);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    SkyLightNeedsRefresh(false);
}

void SceneEnvironmentProbePool::ProcessEnvironmentMap(GFXDevice& context, const U16 layerID, const bool highPriority, GFX::CommandBuffer& bufferInOut) {
    ScopedLock<SharedMutex> w_lock(s_queueLock);
    if (s_computationSet.insert(layerID).second) {
        s_computationQueue.push(layerID);
    }

}

void SceneEnvironmentProbePool::ProcessEnvironmentMapInternal(GFXDevice& context, const U16 layerID, ComputationStages& stage, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    // This entire sequence is based on this awesome blog post by Bruno Opsenica: https://bruop.github.io/ibl/
    switch (stage) {
        case ComputationStages::MIP_MAP_SOURCE: {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Process environment map #%d-MipMapsSource", layerID).c_str()));
            RTAttachment* sourceAtt = SceneEnvironmentProbePool::ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);

            GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
            computeMipMapsCommand._layerRange = { layerID, 1 };
            computeMipMapsCommand._texture = sourceAtt->texture().get();
            GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

            s_faceID = 0u;
            stage = ComputationStages::PREFILTER_MAP;
        }break;
        case ComputationStages::PREFILTER_MAP: {
            PrefilterEnvMap(context, layerID, 5u - s_faceID, bufferInOut);
            if (++s_faceID == 6u) {
                stage = ComputationStages::IRRADIANCE_CALC;
                s_faceID = 0u;
            }
        }break;
        case ComputationStages::IRRADIANCE_CALC: {
            ComputeIrradianceMap(context, layerID, 5u - s_faceID, bufferInOut);
            if (++s_faceID == 6u) {
                stage = ComputationStages::MIP_MAP_PREFILTER;
                s_faceID = 0u;
            }
        }break;
        case ComputationStages::MIP_MAP_PREFILTER: {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Process environment map #%d-MipMapsPrefilter", layerID).c_str()));

            RTAttachment* destPrefilteredAtt = SceneEnvironmentProbePool::PrefilteredTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);

            GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
            computeMipMapsCommand._layerRange = { layerID, 1 };
            computeMipMapsCommand._texture = destPrefilteredAtt->texture().get();
            GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

            stage = ComputationStages::MIP_MAP_IRRADIANCE;
        }break;
        case ComputationStages::MIP_MAP_IRRADIANCE: {
            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Process environment map #%d-MipMapsIrradiance", layerID).c_str()));

            RTAttachment* destIrradianceAtt = SceneEnvironmentProbePool::IrradianceTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);

            GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
            computeMipMapsCommand._layerRange = { layerID, 1 };
            computeMipMapsCommand._texture = destIrradianceAtt->texture().get();
            GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

            stage = ComputationStages::COUNT;
            s_queuedLayer = 0u;
        }break;
    };
}

void SceneEnvironmentProbePool::PrefilterEnvMap(GFXDevice& context, const U16 layerID, const U8 faceIndex, GFX::CommandBuffer& bufferInOut) {

    RTAttachment* sourceAtt = SceneEnvironmentProbePool::ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);
    RTAttachment* destinationAtt = SceneEnvironmentProbePool::PrefilteredTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);
    const Texture* sourceTex = sourceAtt->texture().get();

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("PreFilter environment map #%d-%d", layerID, faceIndex).c_str()));

    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ s_pipelineCalcPrefiltered });

    // width is the width/length of a single face of our cube map
    const U16 width = sourceTex->width();
    const F32 fWidth = to_F32(width);

    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
        binding._slot = 0;
        binding._data.As<DescriptorCombinedImageSampler>() = { sourceTex->defaultView(), sourceAtt->descriptor()._samplerHash };
    }
    {
        PushConstants& constants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants;
        constants.set(_ID("cubeFace"), GFX::PushConstantType::UINT, to_U32(faceIndex));
        constants.set(_ID("layerIndex"), GFX::PushConstantType::UINT, to_U32(layerID));
        constants.set(_ID("imgSize"), GFX::PushConstantType::VEC2, vec2<F32>{fWidth, fWidth});
    }

    ImageView destinationImage = destinationAtt->texture()->getView(TextureType::TEXTURE_2D, { 0u, 0u }, { to_U8((layerID * 6) + faceIndex) , 1u }, ImageFlag::WRITE);

    const F32 maxMipLevel = to_F32(std::log2(fWidth));
    for (F32 mipLevel = 0u; mipLevel <= maxMipLevel; ++mipLevel) {
        const U16 mipWidth = width / to_U16(std::pow(2.f, mipLevel));
        const F32 roughness = mipLevel / maxMipLevel;
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(_ID("u_params"), GFX::PushConstantType::VEC2, vec2<F32>{ roughness, mipLevel });

        destinationImage._mipLevels = { to_U8(mipLevel), 1u };
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;

        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
        binding._slot = 1u;
        binding._data.As<ImageView>() = destinationImage;

        // Dispatch enough groups to cover the entire _mipped_ face
        GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ std::max(1u, mipWidth / 8u), std::max(1u, mipWidth / 8u), 1 });
    }
    GFX::EnqueueCommand<GFX::MemoryBarrierCommand>(bufferInOut)->_barrierMask = to_base(MemoryBarrierType::TEXTURE_FETCH);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void SceneEnvironmentProbePool::ComputeIrradianceMap(GFXDevice& context, const U16 layerID, const U8 faceIndex, GFX::CommandBuffer& bufferInOut) {
    RTAttachment* sourceAtt = SceneEnvironmentProbePool::ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);
    RTAttachment* destinationAtt = SceneEnvironmentProbePool::IrradianceTarget()._rt->getAttachment(RTAttachmentType::Colour, 0);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Compute Irradiance #%d-%d", layerID, faceIndex).c_str()));

    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ s_pipelineCalcIrradiance });

    PushConstants& constants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants;
    constants.set(_ID("imgSize"), GFX::PushConstantType::VEC2, vec2<F32>{s_IrradianceTextureSize, s_IrradianceTextureSize});
    constants.set(_ID("layerIndex"), GFX::PushConstantType::UINT, to_U32(layerID));
    constants.set(_ID("cubeFace"), GFX::PushConstantType::UINT, to_U32(faceIndex));

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_DRAW;
    {
        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
        binding._slot = 0u;
        binding._data.As<DescriptorCombinedImageSampler>() = { sourceAtt->texture()->defaultView(), sourceAtt->descriptor()._samplerHash };
    }
    {
        auto& binding = cmd->_bindings.emplace_back(ShaderStageVisibility::COMPUTE);
        binding._slot = 1u;
        binding._data.As<ImageView>() = destinationAtt->texture()->getView(TextureType::TEXTURE_2D, { 0u, 1u }, { to_U8((layerID * 6) + faceIndex) , 1u }, ImageFlag::WRITE);
    }
    const U32 groupsX = to_U32(std::ceil(s_IrradianceTextureSize / to_F32(8)));
    const U32 groupsY = to_U32(std::ceil(s_IrradianceTextureSize / to_F32(8)));
    GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ groupsX, groupsY, 1 });

    GFX::EnqueueCommand<GFX::MemoryBarrierCommand>(bufferInOut)->_barrierMask = to_base(MemoryBarrierType::TEXTURE_FETCH);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void SceneEnvironmentProbePool::lockProbeList() const noexcept {
    _probeLock.lock();
}

void SceneEnvironmentProbePool::unlockProbeList() const noexcept {
    _probeLock.unlock();
}

const EnvironmentProbeList& SceneEnvironmentProbePool::getLocked() const noexcept {
    return _envProbes;
}

void SceneEnvironmentProbePool::registerProbe(EnvironmentProbeComponent* probe) {
    DebuggingSkyLight(false);

    ScopedLock<SharedMutex> w_lock(_probeLock);

    assert(_envProbes.size() < U16_MAX - 2u);
    _envProbes.emplace_back(probe);
    probe->poolIndex(to_U16(_envProbes.size() - 1u));
}

void SceneEnvironmentProbePool::unregisterProbe(const EnvironmentProbeComponent* probe) {
    if (probe != nullptr) {
        ScopedLock<SharedMutex> w_lock(_probeLock);
        auto it = _envProbes.erase(eastl::remove_if(begin(_envProbes), end(_envProbes),
                                                    [probeGUID = probe->getGUID()](const auto& p) noexcept { return p->getGUID() == probeGUID; }),
                                   end(_envProbes));
        while (it != cend(_envProbes)) {
            (*it)->poolIndex((*it)->poolIndex() - 1);
            it++;
        }
    }

    if (probe == _debugProbe) {
        _debugProbe = nullptr;
    }
}

namespace {
    constexpr I16 g_debugViewBase = 10;
}

void SceneEnvironmentProbePool::prepareDebugData() {
    const bool enableSkyLightDebug = DebuggingSkyLight();
    const bool enableProbeDebugging = _debugProbe != nullptr;
    const I16 skyLightGroupID = g_debugViewBase + SkyProbeLayerIndex();
    const I16 probeID = enableProbeDebugging ? g_debugViewBase + _debugProbe->rtLayerIndex() : -1;

    bool addSkyLightViews = true, addProbeViews = true;
    for (const DebugView_ptr& view : s_debugViews) {
        if (view->_groupID == skyLightGroupID) {
            addSkyLightViews = false;
            view->_enabled = enableSkyLightDebug;
        } else if (enableProbeDebugging && view->_groupID == probeID) {
            addProbeViews = false;
            view->_enabled = true;
        } else {
            view->_enabled = false;
        }
    }

    if (enableSkyLightDebug && addSkyLightViews) {
        createDebugView(SkyProbeLayerIndex());
    }
    if (enableProbeDebugging && addProbeViews) {
        createDebugView(_debugProbe->rtLayerIndex());
    }
}

void SceneEnvironmentProbePool::createDebugView(const U16 layerIndex) {
    for (U32 i = 0u; i < 18u; ++i) {
        DebugView_ptr& probeView = s_debugViews.emplace_back(std::make_shared<DebugView>(to_I16(I16_MAX - layerIndex - i)));
        probeView->_cycleMips = true;

        if (i > 11) {
            probeView->_texture = PrefilteredTarget()._rt->getAttachment(RTAttachmentType::Colour, 0)->texture();
            probeView->_samplerHash = PrefilteredTarget()._rt->getAttachment(RTAttachmentType::Colour, 0)->descriptor()._samplerHash;
        } else if (i > 5) {
            probeView->_texture = IrradianceTarget()._rt->getAttachment(RTAttachmentType::Colour, 0)->texture();
            probeView->_samplerHash = IrradianceTarget()._rt->getAttachment(RTAttachmentType::Colour, 0)->descriptor()._samplerHash;
        } else {
            probeView->_texture = ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0)->texture();
            probeView->_samplerHash = ReflectionTarget()._rt->getAttachment(RTAttachmentType::Colour, 0)->descriptor()._samplerHash;
        }
        probeView->_shader = s_previewShader;
        probeView->_shaderData.set(_ID("layer"), GFX::PushConstantType::INT, layerIndex);
        probeView->_shaderData.set(_ID("face"), GFX::PushConstantType::INT, i % 6u);
        if (i > 11) {
            probeView->_name = Util::StringFormat("Probe_%d_Filtered_face_%d", layerIndex, i % 6u);
        } else if (i > 5) {
            probeView->_name = Util::StringFormat("Probe_%d_Irradiance_face_%d", layerIndex, i % 6u);
        } else {
            probeView->_name = Util::StringFormat("Probe_%d_Reference_face_%d", layerIndex, i % 6u);
        }
        probeView->_groupID = g_debugViewBase + layerIndex;
        probeView->_enabled = true;
        parentScene().context().gfx().addDebugView(probeView);
    }
}

void SceneEnvironmentProbePool::onNodeUpdated(const SceneGraphNode& node) noexcept {
    OPTICK_EVENT();

    const BoundingSphere& bSphere = node.get<BoundsComponent>()->getBoundingSphere();
    lockProbeList();
    const EnvironmentProbeList& probes = getLocked();
    for (const auto& probe : probes) {
        if (probe->checkCollisionAndQueueUpdate(bSphere)) {
            NOP();
        }
    }
    unlockProbeList();
    if (node.getNode().type() == SceneNodeType::TYPE_SKY) {
        SkyLightNeedsRefresh(true);
    }
}

void SceneEnvironmentProbePool::OnTimeOfDayChange(const SceneEnvironmentProbePool& probePool) noexcept {
    OPTICK_EVENT();

    probePool.lockProbeList();
    const EnvironmentProbeList& probes = probePool.getLocked();
    for (const auto& probe : probes) {
        if (probe->updateType() != EnvironmentProbeComponent::UpdateType::ONCE) {
            probe->dirty(true);
        }
    }
    probePool.unlockProbeList();
    SkyLightNeedsRefresh(true);
}

bool SceneEnvironmentProbePool::DebuggingSkyLight() noexcept {
    return s_debuggingSkyLight;
}

void SceneEnvironmentProbePool::DebuggingSkyLight(const bool state) noexcept {
    s_debuggingSkyLight = state;
}

bool SceneEnvironmentProbePool::SkyLightNeedsRefresh() noexcept {
    return s_skyLightNeedsRefresh;
}

void SceneEnvironmentProbePool::SkyLightNeedsRefresh(const bool state) noexcept {
    s_skyLightNeedsRefresh = state;
}

U16 SceneEnvironmentProbePool::SkyProbeLayerIndex() noexcept {
    return Config::MAX_REFLECTIVE_PROBES_PER_PASS;
}
} //namespace Divide