#include "stdafx.h"

#include "Headers/Renderer.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Rendering/PostFX/Headers/PostFX.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

Renderer::Renderer(PlatformContext& context, ResourceCache* cache)
    : PlatformContextComponent(context)
{
    const Configuration& config = context.config();
    const U32 numClusters = to_U32(config.rendering.lightClusteredSizes.x) *
                            to_U32(config.rendering.lightClusteredSizes.y) *
                            to_U32(config.rendering.lightClusteredSizes.z);

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
    { //Light Index Buffer
        const U32 totalLights = numClusters * to_U32(config.rendering.numLightsPerCluster);
        bufferDescriptor._name = "LIGHT_INDEX_SSBO";
        bufferDescriptor._bufferParams._elementCount = totalLights;
        bufferDescriptor._bufferParams._elementSize = sizeof(U32);
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_R_GPU_W;
        bufferDescriptor._bufferParams._initialData = { nullptr, 0 };
        _lightIndexBuffer = _context.gfx().newSB(bufferDescriptor);
        _lightIndexBuffer->bind(ShaderBufferLocation::LIGHT_INDICES);
    }
    { // Light Grid Buffer
        bufferDescriptor._name = "LIGHT_GRID_SSBO";
        bufferDescriptor._bufferParams._elementCount = numClusters;
        bufferDescriptor._bufferParams._elementSize = 3 * sizeof(U32);
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_R_GPU_W;
        bufferDescriptor._bufferParams._initialData = { nullptr, 0 };
        _lightGridBuffer = _context.gfx().newSB(bufferDescriptor);
        _lightGridBuffer->bind(ShaderBufferLocation::LIGHT_GRID);
    }
    { // Global Index Count
        bufferDescriptor._name = "GLOBAL_INDEX_COUNT_SSBO";
        bufferDescriptor._bufferParams._elementSize = sizeof(U32);
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_R_GPU_W;
        bufferDescriptor._bufferParams._initialData = { nullptr, 0 };
        _globalIndexCountBuffer = _context.gfx().newSB(bufferDescriptor);
        _globalIndexCountBuffer->bind(ShaderBufferLocation::LIGHT_INDEX_COUNT);
    }
    { // Cluster AABBs
        bufferDescriptor._bufferParams._elementCount = numClusters;
        bufferDescriptor._bufferParams._elementSize = 2 * (4 * sizeof(F32));
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::ONCE;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::GPU_R_GPU_W;
        bufferDescriptor._bufferParams._initialData = { nullptr, 0 };
        _lightClusterAABBsBuffer = _context.gfx().newSB(bufferDescriptor);
        _lightClusterAABBsBuffer->bind(ShaderBufferLocation::LIGHT_CLUSTER_AABBS);
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

void Renderer::preRender(RenderStagePass stagePass,
                         const Texture_ptr& hizColourTexture,
                         [[maybe_unused]] const size_t samplerHash,
                         LightPool& lightPool,
                         const Camera* camera,
                         GFX::CommandBuffer& bufferInOut)
{
    if (stagePass._stage == RenderStage::SHADOW) {
        return;
    }

    lightPool.uploadLightData(stagePass._stage, bufferInOut);
    if (!hizColourTexture) {
        return;
    }

    const U32 zThreads = to_U32(_context.config().rendering.lightClusteredSizes.z) / Config::Lighting::ClusteredForward::CLUSTER_Z_THREADS;

    const mat4<F32>& projectionMatrix = camera->projectionMatrix();
    if (_previousProjMatrix != projectionMatrix) {
        _previousProjMatrix = projectionMatrix;

        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Renderer Rebuild Light Grid" });
        GFX::EnqueueCommand(bufferInOut, _lightBuildClusteredAABBsPipelineCmd);
        GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ 1u, 1u, zThreads });
        GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_base(MemoryBarrierType::SHADER_STORAGE) });
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Renderer Cull Lights" });
    GFX::EnqueueCommand(bufferInOut, _lightCullPipelineCmd);
    GFX::EnqueueCommand(bufferInOut, GFX::DispatchComputeCommand{ 1u, 1u, zThreads });
    GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{ to_base(MemoryBarrierType::BUFFER_UPDATE) });
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