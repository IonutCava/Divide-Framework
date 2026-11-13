// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/ParticleEmitter.h"


#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Scenes/Headers/Scene.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Core/Headers/PlatformContext.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Scenes/Headers/SceneState.h"
#include "Geometry/Material/Headers/Material.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/GPUBuffer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Divide
{
namespace
{
    constexpr U32 g_particleGeometryBuffer = 0;
    constexpr U32 g_particlePositionBuffer = g_particleGeometryBuffer + 1;
    constexpr U32 g_particleColourBuffer = g_particlePositionBuffer + 1;
    //constexpr U32 g_particleIndexBuffer = g_particleColourBuffer + 1;

    [[nodiscard]] vector<U32> ParticleGeometryIndices(const vector<float3>& vertices, const vector<U32>& indices)
    {
        if (!indices.empty())
        {
            return indices;
        }

        vector<U32> result(vertices.size());
        std::iota(result.begin(), result.end(), 0u);
        return result;
    }
}

ParticleEmitter::ParticleEmitter( const ResourceDescriptor<ParticleEmitter>& descriptor )
    : SceneNode(descriptor,
                GetSceneNodeType<ParticleEmitter>(),
                to_base(ComponentType::TRANSFORM) |
                to_base(ComponentType::BOUNDS) |
                to_base(ComponentType::RENDERING))
{
}

ParticleEmitter::~ParticleEmitter()
{ 
    assert(_particles == nullptr);
}

bool ParticleEmitter::load( PlatformContext& context )
{
    _geometryBuffer = context.gfx().newGPUBuffer(1u, Util::StringFormat("{}_geometry", resourceName()).c_str());
    _indexBuffer = context.gfx().newGPUBuffer(1u, Util::StringFormat("{}_indices", resourceName()).c_str());

    return SceneNode::load( context );
}

ParticleEmitter::ParticleUpload& ParticleEmitter::getUpload(const U64 frame, const float3& eye)
{
    // Vulkan waits for this main-window frame slot before scene preparation.
    // Within it, never recycle a view's upload while commands are still being recorded.
    ParticleFrameUploads& uploads = _frameUploads[frame % _frameUploads.size()];
    if (uploads._frame != frame)
    {
        uploads._frame = frame;
        uploads._used = 0u;
    }
    for (size_t i = 0u; i < uploads._used; ++i)
    {
        if (uploads._slots[i]->_eye == eye)
        {
            return *uploads._slots[i];
        }
    }
    if (uploads._used == uploads._slots.size())
    {
        uploads._slots.emplace_back(std::make_unique<ParticleUpload>());
    }
    ParticleUpload& upload = *uploads._slots[uploads._used++];
    upload._eye = eye;
    upload._uploaded = false;
    return upload;
}

bool ParticleEmitter::initData(const std::shared_ptr<ParticleData>& particleData)
{
    if ( !DIVIDE_ASSERT(particleData != nullptr, "ParticleEmitter::initData: invalid particle data") )
    {
        return false;
    }

    const U32 particleCount = particleData->totalCount();
    const vector<float3>& geometry = particleData->particleGeometryVertices();
    const vector<U32> indices = ParticleGeometryIndices(geometry, particleData->particleGeometryIndices());
    const bool validIndices = std::all_of(indices.begin(), indices.end(), [&geometry](const U32 index)
    {
        return index == U32_MAX || index < geometry.size();
    });
    if ( !DIVIDE_ASSERT(particleCount > 0u &&
                        particleData->_position.size() >= particleCount &&
                        particleData->_acceleration.size() >= particleCount &&
                        particleData->_colour.size() >= particleCount &&
                        particleData->_misc.size() >= particleCount &&
                        (!geometry.empty() || indices.empty()) && validIndices,
                        "ParticleEmitter::initData: invalid capacity, particle properties, or geometry") )
    {
        return false;
    }

    _particles = particleData;
    _geometryIndexCount = to_U32(indices.size());

    if (!geometry.empty())
    {
        GPUBuffer::SetBufferParams vbParams = {};
        vbParams._elementCount = to_U32(geometry.size());
        vbParams._elementSize = sizeof(float3);
        vbParams._updateFrequency = BufferUpdateFrequency::ONCE;
        vbParams._usageType = BufferUsageType::VERTEX_BUFFER;
        vbParams._bindIdx = g_particleGeometryBuffer;
        vbParams._initialData = { (bufferPtr)geometry.data(), geometry.size() * vbParams._elementSize};
        const BufferLock lock = _geometryBuffer->setBuffer(vbParams);
        DIVIDE_UNUSED(lock);
    }
    if (!indices.empty())
    {
        GPUBuffer::SetBufferParams ibParams = {};
        ibParams._elementCount = to_U32(indices.size());
        ibParams._elementSize = sizeof(U32);
        ibParams._updateFrequency = BufferUpdateFrequency::ONCE;
        ibParams._usageType = BufferUsageType::INDEX_BUFFER;
        ibParams._initialData = { (bufferPtr)indices.data(), indices.size() * ibParams._elementSize };
        const BufferLock lock = _indexBuffer->setBuffer(ibParams);
        DIVIDE_UNUSED(lock);
    }

    for ( U32 i = 0; i < particleCount; ++i )
    {
        // Distance to camera (squared)
        _particles->_misc[i].w = -1.0f;
    }

    const PrimitiveTopology topology = _particles->particleGeometryType();
    AttributeMap vertexFormat{};
    {
        AttributeDescriptor& desc = vertexFormat._attributes[to_base(AttribLocation::POSITION)];
        desc._vertexBindingIndex = g_particleGeometryBuffer;
        desc._componentsPerElement = 3u;
        desc._dataType = GFXDataFormat::FLOAT_32;

        auto& vertBinding = vertexFormat._vertexBindings.emplace_back();
        vertBinding._bufferBindIndex = desc._vertexBindingIndex;
        vertBinding._strideInBytes = 3 * sizeof(F32);
    }
    {
        AttributeDescriptor& desc = vertexFormat._attributes[to_base(AttribLocation::NORMAL)];
        desc._vertexBindingIndex = g_particlePositionBuffer;
        desc._componentsPerElement = 4u;
        desc._dataType = GFXDataFormat::FLOAT_32;
        desc._normalized = false;
        desc._strideInBytes = 0u;
        
        auto& vertBinding = vertexFormat._vertexBindings.emplace_back();
        vertBinding._bufferBindIndex = desc._vertexBindingIndex;
        vertBinding._strideInBytes = sizeof( float4 );
        vertBinding._perVertexInputRate = false;
    }
    {
        AttributeDescriptor& desc = vertexFormat._attributes[to_base(AttribLocation::COLOR)];
        desc._vertexBindingIndex = g_particleColourBuffer;
        desc._componentsPerElement = 4u;
        desc._dataType = GFXDataFormat::UNSIGNED_BYTE;
        desc._normalized = true;
        desc._strideInBytes = 0u;

        auto& vertBinding = vertexFormat._vertexBindings.emplace_back();
        vertBinding._bufferBindIndex = desc._vertexBindingIndex;
        vertBinding._strideInBytes = sizeof( UColour4 );
        vertBinding._perVertexInputRate = false;
    }

    const bool useTexture = !_particles->_textureFileName.empty();
    Handle<Material> matHandle = CreateResource(ResourceDescriptor<Material>("Material_particles_" + resourceName()));
    Material* mat = Get(matHandle);

    mat->setPipelineLayout(topology, vertexFormat);
    mat->properties().isInstanced(true);
    mat->computeRenderStateCBK([]([[maybe_unused]] Material* material, const RenderStagePass stagePass, RenderStateBlock& blockInOut)
    {
        blockInOut._cullMode = CullMode::NONE;
        if (stagePass._stage == RenderStage::DISPLAY && stagePass._passType == RenderPassType::PRE_PASS)
        {
            // Leave scene depth and G-buffer data untouched for soft particles.
            blockInOut._depthWriteEnabled = false;
            blockInOut._colourWrite.b[0] = blockInOut._colourWrite.b[1] =
                blockInOut._colourWrite.b[2] = blockInOut._colourWrite.b[3] = false;
        }
    });

    mat->computeShaderCBK([useTexture]([[maybe_unused]] Material* material, const RenderStagePass stagePass)
    {
        ShaderModuleDescriptor vertModule{ ShaderType::VERTEX, "particles.glsl", useTexture ? "WithTexture" : "NoTexture" };
        ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "particles.glsl", useTexture ? "WithTexture" : "NoTexture" };

        if (useTexture)
        {
            fragModule._defines.emplace_back("HAS_TEXTURE");
        }

        ShaderProgramDescriptor particleShaderDescriptor = {};
        particleShaderDescriptor._name = useTexture ? "particles_WithTexture" : "particles_NoTexture";
        particleShaderDescriptor._modules.push_back(vertModule);

        if (stagePass._stage == RenderStage::DISPLAY)
        {
            if (IsDepthPass(stagePass))
            {
                fragModule._variant = "PrePass";
                fragModule._defines.emplace_back("PRE_PASS");
                particleShaderDescriptor._name = useTexture ? "particles_prePass_WithTexture" : "particles_prePass_NoTexture";
            }
            particleShaderDescriptor._modules.push_back(fragModule);
        }
        else if (IsDepthPass(stagePass))
        {
            if (stagePass._stage == RenderStage::SHADOW)
            {
                fragModule._variant = "Shadow.VSM";
                particleShaderDescriptor._modules.push_back(fragModule);
                particleShaderDescriptor._name = "particles_VSM";
            }
            else
            {
                particleShaderDescriptor._name = "particles_DepthPass";
            }
        }

        return particleShaderDescriptor;
    });

    if ( useTexture )
    {
        ResourceDescriptor<Texture> texture( _particles->_textureFileName );
        TextureDescriptor& textureDescriptor = texture._propertyDescriptor;
        textureDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
        textureDescriptor._packing = GFXImagePacking::NORMALIZED_SRGB;

        mat->setTexture(TextureSlot::UNIT0, texture, {}, TextureOperation::NONE, true);
    }

    setMaterialTemplate( matHandle, vertexFormat );

    return true;
}

bool ParticleEmitter::unload()
{
    WAIT_FOR_CONDITION(getState() == ResourceState::RES_LOADED);
    _particles.reset();

    return SceneNode::unload();
}

void ParticleEmitter::buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut)
{
    if (_geometryIndexCount > 0u)
    {

        GenericDrawCommand& cmd = cmdsOut.emplace_back();
        toggleOption(cmd, CmdRenderOptions::RENDER_INDIRECT);

        cmd._cmd.indexCount = _geometryIndexCount;
    }

    SceneNode::buildDrawCommands(sgn, cmdsOut);
}

void ParticleEmitter::prepareRender(SceneGraphNode* sgn,
                                    RenderingComponent& rComp,
                                    RenderPackage& pkg,
                                    GFX::MemoryBarrierCommand& postDrawMemCmd,
                                    const RenderStagePass renderStagePass,
                                    const CameraSnapshot& cameraSnapshot,
                                    const bool refreshData)
{

    if (_enabled && _geometryIndexCount > 0u && getAliveParticleCount() > 0u)
    {
        LockGuard<Mutex> renderLock(_particleRenderLock);
        ParticleUpload& upload = getUpload(GFXDevice::FrameCount(), cameraSnapshot._eye);
        rComp.setIndexBufferElementOffset(_indexBuffer->firstIndexOffsetCount());

        if (!upload._uploaded)
        {
            const float3& eyePos = cameraSnapshot._eye;
            const U32 aliveCount = getAliveParticleCount();

            vector<float4>& misc = _particles->_misc;
            vector<float4>& pos = _particles->_position;

            for (U32 i = 0u; i < aliveCount; ++i)
            {
                misc[i].w = pos[i].xyz.distanceSquared(eyePos);
            }

            _particles->sort();
            if (upload._positions == nullptr)
            {
                GFXDevice& gfx = sgn->context().gfx();
                upload._positions = gfx.newGPUBuffer(1u, Util::StringFormat("{}_positions", resourceName()).c_str());
                upload._colours = gfx.newGPUBuffer(1u, Util::StringFormat("{}_colours", resourceName()).c_str());

                GPUBuffer::SetBufferParams params{};
                params._elementCount = _particles->totalCount();
                params._usageType = BufferUsageType::VERTEX_BUFFER;
                params._updateFrequency = BufferUpdateFrequency::OFTEN;
                params._elementSize = sizeof(float4);
                params._bindIdx = g_particlePositionBuffer;
                const BufferLock positionLock = upload._positions->setBuffer(params);
                params._elementSize = sizeof(UColour4);
                params._bindIdx = g_particleColourBuffer;
                const BufferLock colourLock = upload._colours->setBuffer(params);
                DIVIDE_UNUSED(positionLock);
                DIVIDE_UNUSED(colourLock);
                upload._handles = { _geometryBuffer->_handle, upload._positions->_handle,
                                    upload._colours->_handle, _indexBuffer->_handle };
            }

            upload._locks[0] = upload._positions->updateBuffer(0u, aliveCount, _particles->_renderingPositions.data());
            upload._locks[1] = upload._colours->updateBuffer(0u, aliveCount, _particles->_renderingColours.data());
            RenderingComponent::DrawCommands& cmds = rComp.drawCommands();
            SharedLock<SharedMutex> r_lock(cmds._dataLock);
            upload._commands = cmds._data;
            for (GenericDrawCommand& cmd : upload._commands)
            {
                cmd._cmd.instanceCount = aliveCount;
                cmd._sourceBuffers = upload._handles.data();
                cmd._sourceBuffersCount = to_U16(upload._handles.size());
            }
            upload._uploaded = true;
        }
        pkg.drawCommandsOverride(&upload._commands);
        // Every consuming pass extends the OpenGL fence to its last use.
        for (const BufferLock& lock : upload._locks)
        {
            postDrawMemCmd._bufferLocks.push_back(lock);
        }
    }

    SceneNode::prepareRender(sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData);
}


/// Pre-process particles
void ParticleEmitter::sceneUpdate(const U64 deltaTimeUS,
                                  SceneGraphNode* sgn,
                                  SceneState& sceneState)
{
    constexpr U32 s_particlesPerThread = 1024;

    if (_enabled && _particles != nullptr)
    {
        const TransformComponent* transform = sgn->get<TransformComponent>();

        const float3& pos = transform->getWorldPosition();
        const quatf& rot = transform->getWorldOrientation();

        for (const std::shared_ptr<ParticleSource>& source : _sources)
        {
            source->updateTransform(pos, rot);
            source->emit(deltaTimeUS, _particles);
        }

        U32 aliveCount = getAliveParticleCount();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = aliveCount;
        descriptor._partitionSize = s_particlesPerThread;
        Parallel_For( sgn->context().taskPool( TaskPoolType::HIGH_PRIORITY ), descriptor, [this](const Task*, const U32 start, const U32 end)
        {
            for (U32 i = start; i < end; ++i)
            {
                _particles->_position[i].w = _particles->_misc[i].z;
                _particles->_acceleration[i].set(0.0f);
            }
        });

        ParticleData& data = *_particles;
        for (const std::shared_ptr<ParticleUpdater>& up : _updaters)
        {
            up->update(deltaTimeUS, data);
        }

        aliveCount = getAliveParticleCount();
        renderState().drawState(aliveCount > 0u);
        if (aliveCount > 0u)
        {
            F32 geometryRadius = 0.0f;
            for (const float3& vertex : _particles->particleGeometryVertices())
            {
                geometryRadius = std::max(geometryRadius, std::sqrt(vertex.x * vertex.x + vertex.y * vertex.y));
            }

            BoundingBox aabb{};
            aabb.reset();
            for (U32 i = 0u; i < aliveCount; ++i)
            {
                const float4& particle = _particles->_position[i];
                const F32 radius = geometryRadius * std::abs(particle.w);
                const float3 extent{radius, radius, radius};
                aabb.add(particle.xyz - extent);
                aabb.add(particle.xyz + extent);
            }
            setBounds(aabb);
        }
    }
    else
    {
        renderState().drawState(false);
    }

    SceneNode::sceneUpdate(deltaTimeUS, sgn, sceneState);
}

U32 ParticleEmitter::getAliveParticleCount() const noexcept
{
    return _particles ? _particles->aliveCount() : 0u;
}

}
