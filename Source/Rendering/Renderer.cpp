

#include "Headers/Renderer.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "Managers/Headers/ProjectManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

    namespace
    {
        constexpr bool g_rebuildLightGridEachFrame = false;
    }

Renderer::Renderer(PlatformContext& context)
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

        ResourceDescriptor<ShaderProgram> cullShaderDesc("lightCull", cullDescritpor );
        _lightCullComputeShader = CreateResource(cullShaderDesc);

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._shaderProgramHandle = _lightCullComputeShader;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        _lightCullPipelineCmd._pipeline = _context.gfx().newPipeline(pipelineDescriptor);
    }
    {
        ShaderProgramDescriptor cullDescritpor = {};
        computeDescriptor._variant = "ResetCounter";
        cullDescritpor._modules.push_back(computeDescriptor);

        ResourceDescriptor<ShaderProgram> cullShaderDesc("lightCounterReset", cullDescritpor );
        _lightCounterResetComputeShader = CreateResource(cullShaderDesc);

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._shaderProgramHandle = _lightCounterResetComputeShader;
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
        _lightBuildClusteredAABBsComputeShader = CreateResource( ResourceDescriptor<ShaderProgram>( "lightBuildClusteredAABBs", buildDescritpor ) );

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._shaderProgramHandle = _lightBuildClusteredAABBsComputeShader;
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
        _lightBuildClusteredAABBsPipelineCmd._pipeline = _context.gfx().newPipeline(pipelineDescriptor);
    }
  
    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._ringBufferLength = 1;
    bufferDescriptor._bufferParams._flags._usageType = BufferUsageType::UNBOUND_BUFFER;
    bufferDescriptor._bufferParams._flags._updateFrequency = BufferUpdateFrequency::ONCE;
    bufferDescriptor._bufferParams._flags._updateUsage = BufferUpdateUsage::GPU_TO_GPU;

    { //Light Index Buffer
        const U32 totalLights = numClusters * to_U32(config.rendering.numLightsPerCluster);
        bufferDescriptor._bufferParams._elementCount = totalLights;
        bufferDescriptor._bufferParams._elementSize = sizeof(U32);
        for (U8 i = 0u; i < to_base(RenderStage::COUNT) - 1; ++i) {
            Util::StringFormat( bufferDescriptor._name, "LIGHT_INDEX_SSBO_{}", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
            _lightDataPerStage[i]._lightIndexBuffer = _context.gfx().newSB(bufferDescriptor);
        }
    }
    { // Cluster AABBs
        bufferDescriptor._bufferParams._elementCount = numClusters;
        bufferDescriptor._bufferParams._elementSize = 2 * (4 * sizeof(F32));
        for (U8 i = 0u; i < to_base(RenderStage::COUNT) - 1; ++i) {
            Util::StringFormat( bufferDescriptor._name, "GLOBAL_CLUSTER_AABB_SSBO_{}", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
            _lightDataPerStage[i]._lightClusterAABBsBuffer = _context.gfx().newSB(bufferDescriptor);
        }
    }
    { // Light Grid Buffer
        bufferDescriptor._bufferParams._elementCount = numClusters;
        bufferDescriptor._bufferParams._elementSize = sizeof(vec4<U32>);
        for (U8 i = 0u; i < to_base(RenderStage::COUNT) - 1; ++i) {
            Util::StringFormat( bufferDescriptor._name, "LIGHT_GRID_SSBO_{}", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
            _lightDataPerStage[i]._lightGridBuffer = _context.gfx().newSB(bufferDescriptor);
        }
    }

    { // Global Index Count
        bufferDescriptor._bufferParams._elementCount = 1u;
        bufferDescriptor._bufferParams._elementSize =  sizeof(vec4<U32>);
        for (U8 i = 0u; i < to_base(RenderStage::COUNT) - 1; ++i) {
            Util::StringFormat( bufferDescriptor._name, "GLOBAL_INDEX_COUNT_SSBO_{}", TypeUtil::RenderStageToString(static_cast<RenderStage>(i)));
            _lightDataPerStage[i]._globalIndexCountBuffer = _context.gfx().newSB(bufferDescriptor);
        }
    }

    _postFX = std::make_unique<PostFX>(context);

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
    DestroyResource( _lightCullComputeShader );
    DestroyResource( _lightCounterResetComputeShader );
    DestroyResource( _lightBuildClusteredAABBsComputeShader );
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

    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Renderer Cull Lights";
    {
        PerRenderStageData& data = _lightDataPerStage[to_base(stage)];
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_PASS;

            const size_t stageIndex = to_size( stage );
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 8u, ShaderStageVisibility::COMPUTE_AND_DRAW );
                Set(binding._data, LightPool::SceneBuffer(), {stageIndex, 1u});
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 9u, ShaderStageVisibility::COMPUTE_AND_DRAW );
                Set(binding._data, LightPool::LightBuffer(), {stageIndex * Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME, Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME } );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 10u, ShaderStageVisibility::COMPUTE_AND_DRAW );
                Set(binding._data, data._lightIndexBuffer.get(), { 0u, data._lightIndexBuffer->getPrimitiveCount() } );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 11u, ShaderStageVisibility::COMPUTE_AND_DRAW );
                Set(binding._data, data._lightGridBuffer.get(), { 0u, data._lightGridBuffer->getPrimitiveCount() } );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 12u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, data._lightClusterAABBsBuffer.get(), { 0u, data._lightClusterAABBsBuffer->getPrimitiveCount() } );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 13u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, data._globalIndexCountBuffer.get(), { 0u, data._globalIndexCountBuffer->getPrimitiveCount() } );
            }
        }
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(bufferInOut);
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
        }

        {
            GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Renderer Reset Global Index Count";

            GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_bufferLocks.emplace_back( BufferLock
            {
                ._range = { 0u, U32_MAX },
                ._type = BufferSyncUsage::GPU_READ_TO_GPU_WRITE,
                ._buffer = data._globalIndexCountBuffer->getBufferImpl()
            });
            GFX::EnqueueCommand( bufferInOut, _lightResetCounterPipelineCmd );
            GFX::EnqueueCommand<GFX::DispatchComputeCommand>( bufferInOut )->_computeGroupSize = { 1u, 1u, 1u };
            GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_bufferLocks.emplace_back( BufferLock
            {
                ._range = { 0u, U32_MAX },
                ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE,
                ._buffer = data._globalIndexCountBuffer->getBufferImpl()
            });

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
        }

        bool needRebuild = data._invalidated || g_rebuildLightGridEachFrame;
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

            GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Renderer Rebuild Light Grid";

            GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_bufferLocks.emplace_back( BufferLock
            {
                ._range = { 0u, U32_MAX },
                ._type = BufferSyncUsage::GPU_READ_TO_GPU_WRITE,
                ._buffer = data._lightClusterAABBsBuffer->getBufferImpl()
            });

            GFX::EnqueueCommand(bufferInOut, _lightBuildClusteredAABBsPipelineCmd);
            PushConstantsStruct& pushConstants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData;
            pushConstants.data[0] = data._gridData._invProjectionMatrix;
            pushConstants.data[1]._vec[0] = data._gridData._viewport;
            pushConstants.data[1]._vec[1].xy = data._gridData._zPlanes;
            GFX::EnqueueCommand<GFX::DispatchComputeCommand>(bufferInOut)->_computeGroupSize =
            { 
                Config::Lighting::ClusteredForward::CLUSTERS_X,
                Config::Lighting::ClusteredForward::CLUSTERS_Y,
                Config::Lighting::ClusteredForward::CLUSTERS_Z
            };

            GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_bufferLocks.emplace_back( BufferLock
            {
                ._range = { 0u, U32_MAX },
                ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
                ._buffer = data._lightClusterAABBsBuffer->getBufferImpl()
            });

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
        }


        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_bufferLocks.emplace_back( BufferLock
        {
            ._range = { 0u, U32_MAX },
            ._type = BufferSyncUsage::GPU_READ_TO_GPU_WRITE,
            ._buffer = data._lightGridBuffer->getBufferImpl()
        });

        GFX::EnqueueCommand(bufferInOut, _lightCullPipelineCmd);
        GFX::EnqueueCommand<GFX::DispatchComputeCommand>(bufferInOut)->_computeGroupSize = 
        {
            Config::Lighting::ClusteredForward::CLUSTERS_X / Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS,
            Config::Lighting::ClusteredForward::CLUSTERS_Y / Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS,
            Config::Lighting::ClusteredForward::CLUSTERS_Z / Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS
        };

        {
            auto memCmd = GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut );
            memCmd->_bufferLocks.emplace_back( BufferLock
            {
                ._range = { 0u, U32_MAX },
                ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
                ._buffer = data._lightGridBuffer->getBufferImpl()
            }); 
            memCmd->_bufferLocks.emplace_back( BufferLock
            {
                ._range = { 0u, U32_MAX },
                ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
                ._buffer = data._lightIndexBuffer->getBufferImpl()
            });
            memCmd->_bufferLocks.emplace_back( BufferLock
            {
                ._range = { 0u, U32_MAX },
                ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
                ._buffer = data._globalIndexCountBuffer->getBufferImpl()
            }); 
        }
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void Renderer::idle(const U64 deltaTimeUSGame) const
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    _postFX->idle(_context.config(), deltaTimeUSGame);
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

} //namespace Divide
