#include "stdafx.h"

#include "Headers/Renderer.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "Managers/Headers/SceneManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

Renderer::Renderer(PlatformContext& context, ResourceCache* cache)
    : PlatformContextComponent(context)
{
    const Configuration& config = context.config();
    constexpr U32 numClusters = to_U32(Config::Lighting::ClusteredForward::CLUSTERS_X) * 
                                       Config::Lighting::ClusteredForward::CLUSTERS_Y *
                                       Config::Lighting::ClusteredForward::CLUSTERS_Z;

    ShaderModuleDescriptor computeDescriptor = {};
    computeDescriptor._moduleType = ShaderType::COMPUTE;

    {
        computeDescriptor._sourceFile = "lightCull.glsl";
        ShaderProgramDescriptor cullDescritpor = {};
        cullDescritpor._modules.push_back(computeDescriptor);

        ResourceDescriptor cullShaderDesc("lightCull");
        cullShaderDesc.propertyDescriptor(cullDescritpor);
        _lightCullComputeShader = CreateResource<ShaderProgram>(cache, cullShaderDesc);

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._shaderProgramHandle = _lightCullComputeShader->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        _lightCullPipelineCmd._pipeline = _context.gfx().newPipeline(pipelineDescriptor);
    }
    {
        ShaderProgramDescriptor cullDescritpor = {};
        computeDescriptor._variant = "ResetCounter";
        cullDescritpor._modules.push_back(computeDescriptor);

        ResourceDescriptor cullShaderDesc("lightCounterReset");
        cullShaderDesc.propertyDescriptor(cullDescritpor);
        _lightCounterResetComputeShader = CreateResource<ShaderProgram>(cache, cullShaderDesc);

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._shaderProgramHandle = _lightCounterResetComputeShader->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        _lightResetCounterPipelineCmd._pipeline = _context.gfx().newPipeline(pipelineDescriptor);
    }
    {
        computeDescriptor._sourceFile = "lightBuildClusteredAABBs.glsl";
        computeDescriptor._variant = "";
        ShaderProgramDescriptor buildDescritpor = {};
        buildDescritpor._modules.push_back(computeDescriptor);
        buildDescritpor._globalDefines.emplace_back( "inverseProjectionMatrix PushData0" );
        buildDescritpor._globalDefines.emplace_back( "viewport ivec4(PushData1[0])" );
        buildDescritpor._globalDefines.emplace_back( "_zPlanes PushData1[1].xy" );
        ResourceDescriptor buildShaderDesc("lightBuildClusteredAABBs");
        buildShaderDesc.propertyDescriptor(buildDescritpor);
        _lightBuildClusteredAABBsComputeShader = CreateResource<ShaderProgram>(cache, buildShaderDesc);

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._shaderProgramHandle = _lightBuildClusteredAABBsComputeShader->handle();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        _lightBuildClusteredAABBsPipelineCmd._pipeline = _context.gfx().newPipeline(pipelineDescriptor);
    }
  
    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
    bufferDescriptor._ringBufferLength = 1;
    bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;
    bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_R_GPU_W;

    { //Light Index Buffer
        const U32 totalLights = numClusters * to_U32(config.rendering.numLightsPerCluster);
        bufferDescriptor._bufferParams._elementCount = totalLights;
        bufferDescriptor._bufferParams._elementSize = sizeof(U32);
        for (U8 i = 0u; i < to_base(RenderStage::COUNT) - 1; ++i) {
            bufferDescriptor._name = Util::StringFormat("LIGHT_INDEX_SSBO_%s", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
            _lightDataPerStage[i]._lightIndexBuffer = _context.gfx().newSB(bufferDescriptor);
        }
    }
    { // Cluster AABBs
        bufferDescriptor._bufferParams._elementCount = numClusters;
        bufferDescriptor._bufferParams._elementSize = 2 * (4 * sizeof(F32));
        for (U8 i = 0u; i < to_base(RenderStage::COUNT) - 1; ++i) {
            bufferDescriptor._name = Util::StringFormat("GLOBAL_CLUSTER_AABB_SSBO_%s", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
            _lightDataPerStage[i]._lightClusterAABBsBuffer = _context.gfx().newSB(bufferDescriptor);
        }
    }
    { // Light Grid Buffer
        bufferDescriptor._bufferParams._elementCount = numClusters;
        bufferDescriptor._bufferParams._elementSize = sizeof(vec4<U32>);
        for (U8 i = 0u; i < to_base(RenderStage::COUNT) - 1; ++i) {
            bufferDescriptor._name = Util::StringFormat("LIGHT_GRID_SSBO_%s", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
            _lightDataPerStage[i]._lightGridBuffer = _context.gfx().newSB(bufferDescriptor);
        }
    }

    { // Global Index Count
        bufferDescriptor._bufferParams._elementCount = 1u;
        bufferDescriptor._bufferParams._elementSize =  sizeof(vec4<U32>);
        for (U8 i = 0u; i < to_base(RenderStage::COUNT) - 1; ++i) {
            bufferDescriptor._name = Util::StringFormat("GLOBAL_INDEX_COUNT_SSBO_%s", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
            _lightDataPerStage[i]._globalIndexCountBuffer = _context.gfx().newSB(bufferDescriptor);
        }
    }

    _postFX = eastl::make_unique<PostFX>(context, cache);

    if (config.rendering.postFX.postAA.qualityLevel > 0) {
        _postFX->pushFilter(FilterType::FILTER_SS_ANTIALIASING);
    }
    if (config.rendering.postFX.ssr.enabled) {
        _postFX->pushFilter(FilterType::FILTER_SS_REFLECTIONS);
    }
    if (config.rendering.postFX.ssao.enable) {
        _postFX->pushFilter(FilterType::FILTER_SS_AMBIENT_OCCLUSION);
    }
    if (config.rendering.postFX.dof.enabled) {
        _postFX->pushFilter(FilterType::FILTER_DEPTH_OF_FIELD);
    }
    if (config.rendering.postFX.motionBlur.enablePerObject) {
        _postFX->pushFilter(FilterType::FILTER_MOTION_BLUR);
    }
    if (config.rendering.postFX.bloom.enabled) {
        _postFX->pushFilter(FilterType::FILTER_BLOOM);
    }

    WAIT_FOR_CONDITION(_lightCullPipelineCmd._pipeline != nullptr);
    WAIT_FOR_CONDITION(_lightBuildClusteredAABBsPipelineCmd._pipeline != nullptr);
}

Renderer::~Renderer()
{
    // Destroy our post processing system
    Console::printfn(Locale::Get(_ID("STOP_POST_FX")));
}

void Renderer::prepareLighting(const RenderStage stage,
                               const Rect<I32>& viewport,
                               const CameraSnapshot& cameraSnapshot,
                               GFX::CommandBuffer& bufferInOut)
{
    if (stage == RenderStage::SHADOW) {
        // Nothing to do in the shadow pass
        return;
    }

    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Renderer Cull Lights" });
    {
        PerRenderStageData& data = _lightDataPerStage[to_base(stage)];
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_PASS;

            const auto& pool = context().kernel().sceneManager()->getActiveScene().lightPool();

            const size_t stageIndex = to_size( stage );
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 8u, ShaderStageVisibility::COMPUTE_AND_DRAW );
                Set(binding._data, pool->sceneBuffer(), {stageIndex, 1u});
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 9u, ShaderStageVisibility::COMPUTE_AND_DRAW );
                Set(binding._data, pool->lightBuffer(), {stageIndex * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME, Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME } );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 10u, ShaderStageVisibility::COMPUTE_AND_DRAW );
                Set(binding._data, data._lightIndexBuffer.get(), { 0u, data._lightIndexBuffer->getPrimitiveCount() } );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 11u, ShaderStageVisibility::COMPUTE_AND_DRAW );
                Set(binding._data, data._lightGridBuffer.get(), { 0u, data._lightGridBuffer->getPrimitiveCount() } );
            }
        }
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 0u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, data._globalIndexCountBuffer.get(), { 0u, data._globalIndexCountBuffer->getPrimitiveCount() } );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_bindings, 1u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, data._lightClusterAABBsBuffer.get(), { 0u, data._lightClusterAABBsBuffer->getPrimitiveCount() } );
            }
        }
        GFX::EnqueueCommand( bufferInOut, _lightResetCounterPipelineCmd );
        GFX::EnqueueCommand( bufferInOut, GFX::DispatchComputeCommand{ 1u, 1u, 1u } );

        bool needRebuild = data._invalidated;
        if (!needRebuild )
        {
            PerRenderStageData::GridBuildData tempData;
            tempData._invProjectionMatrix = cameraSnapshot._invProjectionMatrix;
            tempData._viewport = viewport;
            tempData._zPlanes = cameraSnapshot._zPlanes;

             needRebuild = data._gridData != tempData;
             if (needRebuild)
             {
                 data._gridData = tempData;
             }
        }

        if ( needRebuild )
        {
            data._invalidated = false;

            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Renderer Rebuild Light Grid" });
            
            PushConstantsStruct pushConstants{};
            pushConstants.data[0] = data._gridData._invProjectionMatrix;
            pushConstants.data[1]._vec[0] = data._gridData._viewport;
            pushConstants.data[1]._vec[1].xy = data._gridData._zPlanes;

            GFX::EnqueueCommand(bufferInOut, _lightBuildClusteredAABBsPipelineCmd);
            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(bufferInOut)->_constants.set(pushConstants);
            GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand
            { 
                Config::Lighting::ClusteredForward::CLUSTERS_X,
                Config::Lighting::ClusteredForward::CLUSTERS_Y,
                Config::Lighting::ClusteredForward::CLUSTERS_Z
            });
            
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }

        GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_base(MemoryBarrierType::SHADER_STORAGE) });

        GFX::EnqueueCommand(bufferInOut, _lightCullPipelineCmd);
        GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{
            Config::Lighting::ClusteredForward::CLUSTERS_X / Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS,
            Config::Lighting::ClusteredForward::CLUSTERS_Y / Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS,
            Config::Lighting::ClusteredForward::CLUSTERS_Z / Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS
        });
        GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_base(MemoryBarrierType::SHADER_STORAGE) });
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void Renderer::idle() const {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    _postFX->idle(_context.config());
}

void Renderer::updateResolution(const U16 newWidth, const U16 newHeight) const {
    _postFX->updateResolution(newWidth, newHeight);
}

[[nodiscard]] bool Renderer::PerRenderStageData::GridBuildData::operator!=( const Renderer::PerRenderStageData::GridBuildData& other ) const noexcept
{
    return _zPlanes != other._zPlanes ||
           _viewport != other._viewport ||
           _invProjectionMatrix != other._invProjectionMatrix;
}

}