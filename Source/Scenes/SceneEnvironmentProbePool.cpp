

#include "Headers/SceneEnvironmentProbePool.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Scenes/Headers/Scene.h"
#include "Graphs/Headers/SceneNode.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/PushConstants.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Headers/SceneShaderData.h"

namespace Divide {
namespace {

    constexpr bool s_AlwaysRefreshSkyLight = false;
    constexpr bool s_AlwaysProcessFullProbeRefreshPerFrame = false;

    NO_DESTROY Mutex s_queueLock;
    constexpr U16 s_IrradianceTextureSize = 64u;
    constexpr U16 s_LUTTextureSize = 128u;

    NO_DESTROY eastl::queue<U16> s_computationQueue;
    NO_DESTROY eastl::set<U16> s_computationSet;
    U16 s_queuedLayer = 0u;

    static SceneEnvironmentProbePool::ComputationStages s_queuedStage = SceneEnvironmentProbePool::ComputationStages::COUNT;
}

NO_DESTROY vector<DebugView_ptr> SceneEnvironmentProbePool::s_debugViews;
bool SceneEnvironmentProbePool::s_probesDirty = true;

std::array<SceneEnvironmentProbePool::ProbeSlice, Config::MAX_REFLECTIVE_PROBES_PER_PASS> SceneEnvironmentProbePool::s_availableSlices;
RenderTargetHandle SceneEnvironmentProbePool::s_reflection;
RenderTargetHandle SceneEnvironmentProbePool::s_prefiltered;
RenderTargetHandle SceneEnvironmentProbePool::s_irradiance;
RenderTargetHandle SceneEnvironmentProbePool::s_brdfLUT;
Handle<ShaderProgram> SceneEnvironmentProbePool::s_previewShader = INVALID_HANDLE<ShaderProgram>;
Handle<ShaderProgram> SceneEnvironmentProbePool::s_irradianceComputeShader = INVALID_HANDLE<ShaderProgram>;
Handle<ShaderProgram> SceneEnvironmentProbePool::s_prefilterComputeShader = INVALID_HANDLE<ShaderProgram>;
Handle<ShaderProgram> SceneEnvironmentProbePool::s_lutComputeShader = INVALID_HANDLE<ShaderProgram>;
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

U16 SceneEnvironmentProbePool::AllocateSlice(const bool lock)
{
    static_assert(Config::MAX_REFLECTIVE_PROBES_PER_PASS < I16_MAX);

    for (U16 i = 0; i < Config::MAX_REFLECTIVE_PROBES_PER_PASS; ++i)
    {
        if (s_availableSlices[i]._available)
        {
            s_availableSlices[i] = { false, lock };
            return i;
        }
    }

    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    return Config::MAX_REFLECTIVE_PROBES_PER_PASS;
}

void SceneEnvironmentProbePool::UnlockSlice(const U16 slice) noexcept
{
    s_availableSlices[slice] = { true, false };
}

void SceneEnvironmentProbePool::OnStartup(GFXDevice& context) {
    SkyLightNeedsRefresh(true);

    s_availableSlices.fill({});

    // Reflection Targets
    const SamplerDescriptor reflectionSampler = {
        ._mipSampling = TextureMipSampling::LINEAR,
        ._wrapU = TextureWrap::CLAMP_TO_EDGE,
        ._wrapV = TextureWrap::CLAMP_TO_EDGE,
        ._wrapW = TextureWrap::CLAMP_TO_EDGE,
        ._anisotropyLevel = context.context().config().rendering.maxAnisotropicFilteringLevel
    };

    const U32 reflectRes = to_U32(context.context().config().rendering.reflectionProbeResolution);
    {
        TextureDescriptor environmentDescriptor{};
        environmentDescriptor._texType = TextureType::TEXTURE_CUBE_ARRAY;
        environmentDescriptor._layerCount = Config::MAX_REFLECTIVE_PROBES_PER_PASS + 1u;
        environmentDescriptor._mipMappingState = MipMappingState::MANUAL;

        TextureDescriptor depthDescriptor{};
        depthDescriptor._texType = TextureType::TEXTURE_CUBE_ARRAY;
        depthDescriptor._dataType = GFXDataFormat::UNSIGNED_INT;
        depthDescriptor._baseFormat = GFXImageFormat::RED;
        depthDescriptor._packing = GFXImagePacking::DEPTH;
        depthDescriptor._layerCount = Config::MAX_REFLECTIVE_PROBES_PER_PASS + 1u;
        depthDescriptor._mipMappingState = MipMappingState::MANUAL;

        RenderTargetDescriptor desc = {};
        desc._attachments = 
        {
            InternalRTAttachmentDescriptor{ environmentDescriptor, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0, },
            InternalRTAttachmentDescriptor{ depthDescriptor, reflectionSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 },
        };

        desc._name = "ReflectionEnvMap";
        desc._resolution.set(reflectRes, reflectRes);
        s_reflection = context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor environmentDescriptor{};
        environmentDescriptor._texType = TextureType::TEXTURE_CUBE_ARRAY;
        environmentDescriptor._dataType = GFXDataFormat::FLOAT_16;
        environmentDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        environmentDescriptor._layerCount = Config::MAX_REFLECTIVE_PROBES_PER_PASS + 1u;
        environmentDescriptor._mipMappingState = MipMappingState::MANUAL;
        AddImageUsageFlag( environmentDescriptor, ImageUsage::SHADER_WRITE);

        RenderTargetDescriptor desc = {};
        desc._attachments = 
        {
            InternalRTAttachmentDescriptor{ environmentDescriptor, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
        };
        desc._name = "PrefilteredEnvMap";
        desc._resolution.set(reflectRes, reflectRes);
        s_prefiltered = context.renderTargetPool().allocateRT(desc);
        desc._name = "IrradianceEnvMap";
        desc._resolution.set(s_IrradianceTextureSize, s_IrradianceTextureSize);
        s_irradiance = context.renderTargetPool().allocateRT(desc);
    }
    {
        TextureDescriptor environmentDescriptor{};
        environmentDescriptor._dataType = GFXDataFormat::FLOAT_16;
        environmentDescriptor._baseFormat = GFXImageFormat::RG;
        environmentDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        environmentDescriptor._mipMappingState = MipMappingState::OFF;
        AddImageUsageFlag( environmentDescriptor, ImageUsage::SHADER_WRITE);

        RenderTargetDescriptor desc = {};
        desc._attachments = 
        {
            InternalRTAttachmentDescriptor{ environmentDescriptor, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
        };
        desc._name = "BrdfLUT";
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

        ResourceDescriptor<ShaderProgram> shadowPreviewShader("fbPreview.Cube", shaderDescriptor );
        shadowPreviewShader.waitForReady(true);
        s_previewShader = CreateResource(shadowPreviewShader);
    }
    {
        ShaderModuleDescriptor computeModule = {};
        computeModule._moduleType = ShaderType::COMPUTE;
        computeModule._sourceFile = "irradianceCalc.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back(computeModule);
        shaderDescriptor._globalDefines.emplace_back( "imgSizeX PushData0[0].x" );
        shaderDescriptor._globalDefines.emplace_back( "imgSizeY PushData0[0].y" );
        shaderDescriptor._globalDefines.emplace_back( "layerIndex PushData0[0].z" );

        {
            shaderDescriptor._modules.back()._variant = "Irradiance";

            ResourceDescriptor<ShaderProgram> irradianceShader("IrradianceCalc", shaderDescriptor );
            irradianceShader.waitForReady(true);
            s_irradianceComputeShader = CreateResource(irradianceShader);
        }
        {
            shaderDescriptor._modules.back()._variant = "LUT";
            ResourceDescriptor<ShaderProgram> lutShader("LUTCalc", shaderDescriptor );
            lutShader.waitForReady(true);
            s_lutComputeShader = CreateResource(lutShader);
        }
        {
            shaderDescriptor._modules.back()._variant = "PreFilter";
            shaderDescriptor._globalDefines.emplace_back( "mipLevel uint(PushData0[1].x)" );
            shaderDescriptor._globalDefines.emplace_back( "roughness PushData0[1].y" );
            ResourceDescriptor<ShaderProgram> prefilterShader("PrefilterEnv", shaderDescriptor );
            prefilterShader.waitForReady(true);
            s_prefilterComputeShader = CreateResource(prefilterShader);
        }
    }
    {
        PipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor._stateBlock = context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = s_prefilterComputeShader;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        s_pipelineCalcPrefiltered = context.newPipeline(pipelineDescriptor);
    }
    {
        PipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor._stateBlock = context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = s_irradianceComputeShader;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        s_pipelineCalcIrradiance = context.newPipeline(pipelineDescriptor);
    }
}

void SceneEnvironmentProbePool::OnShutdown(GFXDevice& context) {
    DestroyResource(s_previewShader);
    DestroyResource(s_irradianceComputeShader);
    DestroyResource(s_prefilterComputeShader);
    DestroyResource(s_lutComputeShader);

    if (!context.renderTargetPool().deallocateRT(s_reflection) ||
        !context.renderTargetPool().deallocateRT(s_prefiltered) ||
        !context.renderTargetPool().deallocateRT(s_irradiance) ||
        !context.renderTargetPool().deallocateRT(s_brdfLUT)) 
    {
        DIVIDE_UNEXPECTED_CALL();
    }

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

const EnvironmentProbeList& SceneEnvironmentProbePool::sortAndGetLocked(const float3& position) {
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
        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_name = "DO_REFLECTION_PROBE_CLEAR";
        renderPassCmd->_target = s_reflection._targetID;
        renderPassCmd->_clearDescriptor[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
        renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;
        renderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;


        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);
        ProbesDirty(false);
    }
}

void SceneEnvironmentProbePool::UpdateSkyLight(GFXDevice& context, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

    if (s_lutTextureDirty)
    {

        PROFILE_SCOPE("Upadate LUT", Profiler::Category::Graphics);

        PipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor._stateBlock = context.get2DStateBlock();
        pipelineDescriptor._shaderProgramHandle = s_lutComputeShader;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;

        const Pipeline* pipelineCalcLut = context.newPipeline(pipelineDescriptor);

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = pipelineCalcLut;

        Handle<Texture> brdfLutTexture = SceneEnvironmentProbePool::BRDFLUTTarget()._rt->getAttachment(RTAttachmentType::COLOUR)->texture();
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;

        const ImageView targetView = Get(brdfLutTexture)->getView( TextureType::TEXTURE_2D, { 0u, 1u }, { 0u, 1u });
        
        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_textureLayoutChanges.emplace_back(TextureLayoutChange
        {
            ._targetView = targetView,
            ._sourceLayout = ImageUsage::SHADER_READ,
            ._targetLayout = ImageUsage::SHADER_WRITE
        });

        DescriptorSetBinding& binding = AddBinding( cmd->_set, 12u, ShaderStageVisibility::COMPUTE );
        Set(binding._data, targetView, ImageUsage::SHADER_WRITE);

        const U32 groupsX = to_U32(CEIL(s_LUTTextureSize / to_F32(8)));
        const U32 groupsY = to_U32(CEIL(s_LUTTextureSize / to_F32(8)));
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(bufferInOut)->_workGroupSize = { groupsX, groupsY, 1 };

        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_textureLayoutChanges.emplace_back(TextureLayoutChange
        {
            ._targetView = targetView,
            ._sourceLayout = ImageUsage::SHADER_WRITE,
            ._targetLayout = ImageUsage::SHADER_READ
        });

        GFX::ComputeMipMapsCommand computeMipMapsCommand{};
        computeMipMapsCommand._texture = brdfLutTexture;
        computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
        GFX::EnqueueCommand( bufferInOut, computeMipMapsCommand );

        s_lutTextureDirty = false;
    }

    if (SkyLightNeedsRefresh() && s_queuedLayer != SkyProbeLayerIndex())
    {
        PROFILE_SCOPE( "Upadate Sky Probe", Profiler::Category::Graphics );

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "SkyLight Pass";

        RenderPassParams params = {};

        params._target = SceneEnvironmentProbePool::ReflectionTarget()._targetID;
        params._stagePass = 
        {
            ._stage = RenderStage::REFLECTION,
            ._passType = RenderPassType::COUNT,
            ._index = SkyProbeLayerIndex(),
            ._variant = static_cast<RenderStagePass::VariantType>(ReflectorType::CUBE)
        };

        params._drawMask &= ~(1u << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES));
        params._drawMask &= ~(1u << to_base(RenderPassParams::Flags::DRAW_STATIC_NODES));
        params._drawMask &= ~(1u << to_base(RenderPassParams::Flags::DRAW_TRANSLUCENT_NODES));

        params._targetDescriptorPrePass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = false;
        params._clearDescriptorPrePass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;

        params._targetDescriptorMainPass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
        params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::BLUE, true };

        context.generateCubeMap(params,
                                SkyProbeLayerIndex(),
                                VECTOR3_ZERO,
                                float2(0.1f, 100.f),
                                bufferInOut,
                                memCmdInOut);


        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

        ProcessEnvironmentMap(SkyProbeLayerIndex(), false);

        SkyLightNeedsRefresh(false);
    }
    {
        LockGuard<Mutex> w_lock(s_queueLock);
        if (s_queuedStage == ComputationStages::COUNT && !s_computationQueue.empty())
        {
            s_queuedLayer = s_computationQueue.front();
            s_queuedStage = ComputationStages::MIP_MAP_SOURCE;
            s_computationQueue.pop();
            s_computationSet.erase(s_queuedLayer);
        }

        while (s_queuedStage != ComputationStages::COUNT)
        {
            ProcessEnvironmentMapInternal(s_queuedLayer, s_queuedStage, bufferInOut);
            if constexpr ( !s_AlwaysProcessFullProbeRefreshPerFrame )
            {
                break;
            }
        }
    }
    {
        PROFILE_SCOPE( "Upadate Descriptor Sets", Profiler::Category::Graphics );

        RTAttachment* prefiltered = SceneEnvironmentProbePool::PrefilteredTarget()._rt->getAttachment(RTAttachmentType::COLOUR);
        RTAttachment* irradiance = SceneEnvironmentProbePool::IrradianceTarget()._rt->getAttachment(RTAttachmentType::COLOUR);
        RTAttachment* brdfLut = SceneEnvironmentProbePool::BRDFLUTTarget()._rt->getAttachment(RTAttachmentType::COLOUR);

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_FRAME;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::COMPUTE );
            Set( binding._data, prefiltered->texture(), prefiltered->_descriptor._sampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::COMPUTE );
            Set( binding._data, irradiance->texture(), irradiance->_descriptor._sampler );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 2u, ShaderStageVisibility::COMPUTE );
            Set( binding._data, brdfLut->texture(), brdfLut->_descriptor._sampler );
        }
    }
}

void SceneEnvironmentProbePool::ProcessEnvironmentMap(const U16 layerID, [[maybe_unused]] const bool highPriority)
{
    LockGuard<Mutex> w_lock(s_queueLock);
    if (s_computationSet.insert(layerID).second)
    {
        s_computationQueue.push(layerID);
    }
}

void SceneEnvironmentProbePool::ProcessEnvironmentMapInternal(const U16 layerID, ComputationStages& stage, GFX::CommandBuffer& bufferInOut)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    // This entire sequence is based on this awesome blog post by Bruno Opsenica: https://bruop.github.io/ibl/
    switch (stage)
    {
        case ComputationStages::MIP_MAP_SOURCE:
        {
            PROFILE_SCOPE( "Generate Mipmaps", Profiler::Category::Graphics );

            auto scopeCmd  = GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut );
            Util::StringFormatTo( scopeCmd->_scopeName, "Process environment map #{}-MipMapsSource", layerID);
            RTAttachment* sourceAtt = SceneEnvironmentProbePool::ReflectionTarget()._rt->getAttachment(RTAttachmentType::COLOUR);

            GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
            computeMipMapsCommand._layerRange = { layerID, 1 };
            computeMipMapsCommand._texture = sourceAtt->texture();
            computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
            GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

            stage = ComputationStages::PREFILTER_MAP;
        } break;
        case ComputationStages::PREFILTER_MAP:
        {
            PrefilterEnvMap(layerID, bufferInOut);
            stage = ComputationStages::IRRADIANCE_CALC;
        } break;
        case ComputationStages::IRRADIANCE_CALC:
        {
            ComputeIrradianceMap(layerID, bufferInOut);
            stage = ComputationStages::COUNT;
            s_queuedLayer = 0u;
        } break;
        default:
        case ComputationStages::COUNT: 
            DIVIDE_UNEXPECTED_CALL();
            break;

    }
}

void SceneEnvironmentProbePool::PrefilterEnvMap(const U16 layerID, GFX::CommandBuffer& bufferInOut)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    RTAttachment* sourceAtt = SceneEnvironmentProbePool::ReflectionTarget()._rt->getAttachment(RTAttachmentType::COLOUR);
    RTAttachment* destinationAtt = SceneEnvironmentProbePool::PrefilteredTarget()._rt->getAttachment(RTAttachmentType::COLOUR);
    const ResourcePtr<Texture> sourceTex = Get(sourceAtt->texture());

    auto scopeCmd = GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut);
    Util::StringFormatTo( scopeCmd->_scopeName, "PreFilter environment map #{}", layerID);

    GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = s_pipelineCalcPrefiltered;

    // width is the width/length of a single face of our cube map
    const U16 width = sourceTex->width();
    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::COMPUTE );
        Set( binding._data, sourceTex->getView(), sourceAtt->_descriptor._sampler );
    }

    ImageView destinationImage = Get(destinationAtt->texture())->getView(TextureType::TEXTURE_CUBE_ARRAY, { 0u, 1u }, { 0u , U16_MAX });

    const F32 fWidth = to_F32(width);

    PushConstantsStruct fastData{};
    fastData.data[0]._vec[0].xyz.set(fWidth, fWidth, to_F32(layerID));

    const F32 maxMipLevel = to_F32(std::log2(fWidth));
    for (F32 mipLevel = 0u; mipLevel <= maxMipLevel; ++mipLevel) 
    {
        destinationImage._subRange._mipLevels = { to_U8(mipLevel), 1u };

        const F32 roughness = mipLevel / maxMipLevel;
        fastData.data[0]._vec[1].xy.set(mipLevel, roughness);
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_fastData = fastData;
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;

        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_textureLayoutChanges.emplace_back(TextureLayoutChange
        {
            ._targetView = destinationImage,
            ._sourceLayout = ImageUsage::SHADER_READ,
            ._targetLayout = ImageUsage::SHADER_WRITE
        });

        DescriptorSetBinding& binding = AddBinding( cmd->_set, 12u, ShaderStageVisibility::COMPUTE );
        Set(binding._data, destinationImage, ImageUsage::SHADER_WRITE);

        // Dispatch enough groups to cover the entire _mipped_ face
        const U16 mipWidth = width / to_U16(std::pow(2.f, mipLevel));
        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(bufferInOut)->_workGroupSize = { std::max(1u, mipWidth / 8u), std::max(1u, mipWidth / 8u), 1 };

        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_textureLayoutChanges.emplace_back(TextureLayoutChange
        {
            ._targetView = destinationImage,
            ._sourceLayout = ImageUsage::SHADER_WRITE,
            ._targetLayout = ImageUsage::SHADER_READ
        });
    }

    GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
    computeMipMapsCommand._layerRange = { layerID, 1 };
    computeMipMapsCommand._texture = destinationAtt->texture();
    computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
    GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void SceneEnvironmentProbePool::ComputeIrradianceMap( const U16 layerID, GFX::CommandBuffer& bufferInOut)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    RTAttachment* sourceAtt = SceneEnvironmentProbePool::ReflectionTarget()._rt->getAttachment(RTAttachmentType::COLOUR);
    RTAttachment* destinationAtt = SceneEnvironmentProbePool::IrradianceTarget()._rt->getAttachment(RTAttachmentType::COLOUR);

    auto scopeCmd = GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut);
    Util::StringFormatTo( scopeCmd->_scopeName, "Compute Irradiance #{}", layerID);

    GFX::EnqueueCommand<GFX::BindPipelineCommand>(bufferInOut)->_pipeline = s_pipelineCalcIrradiance;

    ImageView destinationView = Get(destinationAtt->texture())->getView( TextureType::TEXTURE_CUBE_ARRAY, { 0u, 1u }, { 0u , U16_MAX });

    GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_textureLayoutChanges.emplace_back( TextureLayoutChange
    {
        ._targetView = destinationView,
        ._sourceLayout = ImageUsage::SHADER_READ,
        ._targetLayout = ImageUsage::SHADER_WRITE
    });

    auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
    cmd->_usage = DescriptorSetUsage::PER_DRAW;
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::COMPUTE );
        Set( binding._data, sourceAtt->texture(), sourceAtt->_descriptor._sampler );
    }
    {
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 12u, ShaderStageVisibility::COMPUTE );
        Set(binding._data, destinationView, ImageUsage::SHADER_WRITE );
    }

    PushConstantsStruct& fastData = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_fastData;
    fastData.data[0]._vec[0].xyz.set(to_F32(s_IrradianceTextureSize), to_F32(s_IrradianceTextureSize), to_F32(layerID));

    const U32 groupsX = to_U32(CEIL(s_IrradianceTextureSize / to_F32(8)));
    const U32 groupsY = to_U32(CEIL(s_IrradianceTextureSize / to_F32(8)));
    GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>(bufferInOut)->_workGroupSize = { groupsX, groupsY, 1 };

    GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_textureLayoutChanges.emplace_back( TextureLayoutChange
    {
        ._targetView = destinationView,
        ._sourceLayout = ImageUsage::SHADER_WRITE,
        ._targetLayout = ImageUsage::SHADER_READ
    });

    GFX::ComputeMipMapsCommand computeMipMapsCommand = {};
    computeMipMapsCommand._layerRange = { layerID, 1 };
    computeMipMapsCommand._texture = destinationAtt->texture();
    computeMipMapsCommand._usage = ImageUsage::SHADER_READ;
    GFX::EnqueueCommand(bufferInOut, computeMipMapsCommand);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void SceneEnvironmentProbePool::lockProbeList() const noexcept
{
    _probeLock.lock();
}

void SceneEnvironmentProbePool::unlockProbeList() const noexcept
{
    _probeLock.unlock();
}

const EnvironmentProbeList& SceneEnvironmentProbePool::getLocked() const noexcept
{
    return _envProbes;
}

void SceneEnvironmentProbePool::registerProbe(EnvironmentProbeComponent* probe)
{
    DebuggingSkyLight(false);

    LockGuard<SharedMutex> w_lock(_probeLock);

    assert(_envProbes.size() < U16_MAX - 2u);
    _envProbes.emplace_back(probe);
    probe->poolIndex(to_U16(_envProbes.size() - 1u));
}

void SceneEnvironmentProbePool::unregisterProbe(const EnvironmentProbeComponent* probe)
{
    if (probe != nullptr) {
        LockGuard<SharedMutex> w_lock(_probeLock);
        auto it = _envProbes.erase(eastl::remove_if(begin(_envProbes), end(_envProbes),
                                                    [probeGUID = probe->getGUID()](const auto& p) noexcept { return p->getGUID() == probeGUID; }),
                                   end(_envProbes));
        while (it != cend(_envProbes)) {
            (*it)->poolIndex((*it)->poolIndex() - 1);
            it++;
        }
    }

    if (probe == _debugProbe)
    {
        _debugProbe = nullptr;
    }
}

namespace
{
    constexpr I16 g_debugViewBase = 10;
}

void SceneEnvironmentProbePool::prepareDebugData()
{
    const bool enableSkyLightDebug = DebuggingSkyLight();
    const bool enableProbeDebugging = _debugProbe != nullptr;
    const I16 skyLightGroupID = g_debugViewBase + SkyProbeLayerIndex();
    const I16 probeID = enableProbeDebugging ? g_debugViewBase + _debugProbe->rtLayerIndex() : -1;

    bool addSkyLightViews = true, addProbeViews = true;
    for (const DebugView_ptr& view : s_debugViews)
    {
        if (view->_groupID == skyLightGroupID)
        {
            addSkyLightViews = false;
            view->_enabled = enableSkyLightDebug;
        }
        else if (enableProbeDebugging && view->_groupID == probeID)
        {
            addProbeViews = false;
            view->_enabled = true;
        }
        else
        {
            view->_enabled = false;
        }
    }

    if (enableSkyLightDebug && addSkyLightViews)
    {
        createDebugView(SkyProbeLayerIndex());
    }
    if (enableProbeDebugging && addProbeViews)
    {
        createDebugView(_debugProbe->rtLayerIndex());
    }
}

void SceneEnvironmentProbePool::createDebugView(const U16 layerIndex)
{
    for (U32 i = 0u; i < 18u; ++i)
    {
        DebugView_ptr& probeView = s_debugViews.emplace_back(std::make_shared<DebugView>(to_I16(I16_MAX - layerIndex - i)));
        probeView->_cycleMips = true;

        if (i > 11)
        {
            probeView->_texture = PrefilteredTarget()._rt->getAttachment(RTAttachmentType::COLOUR)->texture();
            probeView->_sampler = PrefilteredTarget()._rt->getAttachment(RTAttachmentType::COLOUR)->_descriptor._sampler;
        }
        else if (i > 5)
        {
            probeView->_texture = IrradianceTarget()._rt->getAttachment(RTAttachmentType::COLOUR)->texture();
            probeView->_sampler = IrradianceTarget()._rt->getAttachment(RTAttachmentType::COLOUR)->_descriptor._sampler;
        }
        else
        {
            probeView->_texture = ReflectionTarget()._rt->getAttachment(RTAttachmentType::COLOUR)->texture();
            probeView->_sampler = ReflectionTarget()._rt->getAttachment(RTAttachmentType::COLOUR)->_descriptor._sampler;
        }

        probeView->_shader = s_previewShader;
        probeView->_shaderData.set(_ID("layer"), PushConstantType::INT, layerIndex);
        probeView->_shaderData.set(_ID("face"), PushConstantType::INT, i % 6u);
        if (i > 11)
        {
            Util::StringFormatTo( probeView->_name, "Probe_{}_Filtered_face_{}", layerIndex, i % 6u );
        }
        else if (i > 5) 
        {
            Util::StringFormatTo( probeView->_name, "Probe_{}_Irradiance_face_{}", layerIndex, i % 6u );
        }
        else
        {
            Util::StringFormatTo( probeView->_name, "Probe_{}_Reference_face_{}", layerIndex, i % 6u );
        }

        probeView->_groupID = g_debugViewBase + layerIndex;
        probeView->_enabled = true;
        parentScene().context().gfx().addDebugView(probeView);
    }
}

void SceneEnvironmentProbePool::onNodeUpdated(const SceneGraphNode& node) noexcept
{
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const BoundingSphere& bSphere = node.get<BoundsComponent>()->getBoundingSphere();
    lockProbeList();
    const EnvironmentProbeList& probes = getLocked();
    for (const auto& probe : probes)
    {
        if (probe->checkCollisionAndQueueUpdate(bSphere))
        {
            NOP();
        }
    }

    unlockProbeList();
    if (node.getNode().type() == SceneNodeType::TYPE_SKY) {
        SkyLightNeedsRefresh(true);
    }
}

void SceneEnvironmentProbePool::OnTimeOfDayChange(const SceneEnvironmentProbePool& probePool) noexcept
{
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    probePool.lockProbeList();
    const EnvironmentProbeList& probes = probePool.getLocked();
    for (const auto& probe : probes)
    {
        if (probe->updateType() != EnvironmentProbeComponent::UpdateType::ONCE)
        {
            probe->dirty(true);
        }
    }
    probePool.unlockProbeList();
    SkyLightNeedsRefresh(true);
}

bool SceneEnvironmentProbePool::DebuggingSkyLight() noexcept
{
    return s_debuggingSkyLight;
}

void SceneEnvironmentProbePool::DebuggingSkyLight(const bool state) noexcept
{
    s_debuggingSkyLight = state;
}

bool SceneEnvironmentProbePool::SkyLightNeedsRefresh() noexcept
{
    return s_skyLightNeedsRefresh || s_AlwaysRefreshSkyLight;
}

void SceneEnvironmentProbePool::SkyLightNeedsRefresh(const bool state) noexcept
{
    s_skyLightNeedsRefresh = state;
}

U16 SceneEnvironmentProbePool::SkyProbeLayerIndex() noexcept
{
    return Config::MAX_REFLECTIVE_PROBES_PER_PASS;
}
} //namespace Divide
