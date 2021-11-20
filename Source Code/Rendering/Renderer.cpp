#include "stdafx.h"

#include "Headers/Renderer.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Rendering/PostFX/Headers/PostFX.h"

#include "Managers/Headers/SceneManager.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {
    
vec3<U8> Renderer::CLUSTER_SIZE {
    Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS,
    Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS,
    24u
};

Renderer::Renderer(PlatformContext& context, ResourceCache* cache)
    : PlatformContextComponent(context)
{
    DIVIDE_ASSERT(CLUSTER_SIZE.x % Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS == 0);
    DIVIDE_ASSERT(CLUSTER_SIZE.y % Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS == 0);
    DIVIDE_ASSERT(CLUSTER_SIZE.z % Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS == 0);
    _computeWorkgroupSize.set(
        CLUSTER_SIZE.x / Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS,
        CLUSTER_SIZE.y / Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS,
        CLUSTER_SIZE.z / Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS
    );

    const Configuration& config = context.config();
    const U32 numClusters = to_U32(CLUSTER_SIZE.x) * CLUSTER_SIZE.y * CLUSTER_SIZE.z;

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
        pipelineDescriptor._shaderProgramHandle = _lightCullComputeShader->getGUID();
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
        pipelineDescriptor._shaderProgramHandle = _lightCounterResetComputeShader->getGUID();
        _lightResetCounterPipelineCmd._pipeline = _context.gfx().newPipeline(pipelineDescriptor);
    }
    {
        computeDescriptor._sourceFile = "lightBuildClusteredAABBs.glsl";
        ShaderProgramDescriptor buildDescritpor = {};
        buildDescritpor._modules.push_back(computeDescriptor);
        ResourceDescriptor buildShaderDesc("lightBuildClusteredAABBs");
        buildShaderDesc.propertyDescriptor(buildDescritpor);
        _lightBuildClusteredAABBsComputeShader = CreateResource<ShaderProgram>(cache, buildShaderDesc);

        PipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor._shaderProgramHandle = _lightBuildClusteredAABBsComputeShader->getGUID();
        _lightBuildClusteredAABBsPipelineCmd._pipeline = _context.gfx().newPipeline(pipelineDescriptor);
    }
  
    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
    bufferDescriptor._ringBufferLength = 1;
    bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;
    bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_R_GPU_W;
    bufferDescriptor._bufferParams._initialData = { nullptr, 0 };

    { //Light Index Buffer
        const U32 totalLights = numClusters * to_U32(config.rendering.numLightsPerCluster);
        bufferDescriptor._name = "LIGHT_INDEX_SSBO";
        bufferDescriptor._bufferParams._elementCount = totalLights;
        bufferDescriptor._bufferParams._elementSize = sizeof(U32);
        _lightIndexBuffer = _context.gfx().newSB(bufferDescriptor);
        _lightIndexBuffer->bind(ShaderBufferLocation::LIGHT_INDICES);
    }
    { // Cluster AABBs
        bufferDescriptor._name = "GLOBAL_CLUSTER_AABB_SSBO";
        bufferDescriptor._bufferParams._elementCount = numClusters;
        bufferDescriptor._bufferParams._elementSize = 2 * (4 * sizeof(F32));
        _lightClusterAABBsBuffer = _context.gfx().newSB(bufferDescriptor);
        _lightClusterAABBsBuffer->bind(ShaderBufferLocation::LIGHT_CLUSTER_AABBS);
    }
    { // Light Grid Buffer
        bufferDescriptor._name = "LIGHT_GRID_SSBO";
        bufferDescriptor._bufferParams._elementCount = numClusters;
        bufferDescriptor._bufferParams._elementSize = sizeof(vec4<U32>);
        _lightGridBuffer = _context.gfx().newSB(bufferDescriptor);
        _lightGridBuffer->bind(ShaderBufferLocation::LIGHT_GRID);
    }

    { // Global Index Count
        bufferDescriptor._name = "GLOBAL_INDEX_COUNT_SSBO";
        bufferDescriptor._bufferParams._elementCount = 1u;
        bufferDescriptor._bufferParams._elementSize =  sizeof(vec4<U32>);
        _globalIndexCountBuffer = _context.gfx().newSB(bufferDescriptor);
        _globalIndexCountBuffer->bind(ShaderBufferLocation::LIGHT_INDEX_COUNT);
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
                               const mat4<F32>& projectionMatrix,
                               GFX::CommandBuffer& bufferInOut)
{
    assert(stage != RenderStage::SHADOW);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Renderer Cull Lights" });
    {
        context().kernel().sceneManager()->getActiveScene().lightPool()->uploadLightData(stage, bufferInOut);

        if (_previousProjMatrix != projectionMatrix) {
            _previousProjMatrix = projectionMatrix;

            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Renderer Rebuild Light Grid" });
            {
                GFX::EnqueueCommand(bufferInOut, _lightBuildClusteredAABBsPipelineCmd);
                GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ _computeWorkgroupSize });
                GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_base(MemoryBarrierType::SHADER_STORAGE) });
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        }
        
        GFX::EnqueueCommand(bufferInOut, _lightResetCounterPipelineCmd);
        GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ 1u, 1u, 1u }); 
        GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_base(MemoryBarrierType::SHADER_STORAGE) });

        GFX::EnqueueCommand(bufferInOut, _lightCullPipelineCmd);
        GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ _computeWorkgroupSize });
        GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_base(MemoryBarrierType::SHADER_STORAGE) });

    }
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void Renderer::idle() const {
    OPTICK_EVENT();

    _postFX->idle(_context.config());
}

void Renderer::updateResolution(const U16 newWidth, const U16 newHeight) const {
    _postFX->updateResolution(newWidth, newHeight);
}
}