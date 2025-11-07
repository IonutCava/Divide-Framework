

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

namespace Divide
{
namespace
{
    // 3 should always be enough for round-robin GPU updates to avoid stalls:
    // 1 in ram, 1 in driver and 1 in VRAM
    constexpr U32 g_particleBufferSizeFactor = 3;
    constexpr U32 g_particleGeometryBuffer = 0;
    constexpr U32 g_particlePositionBuffer = g_particleGeometryBuffer + 1;
    constexpr U32 g_particleColourBuffer = g_particlePositionBuffer* + 2;
    constexpr U32 g_particleIndexBuffer = g_particleColourBuffer* + 2;

    constexpr U64 g_updateInterval = Time::MillisecondsToMicroseconds(33);
}

ParticleEmitter::ParticleEmitter( const ResourceDescriptor<ParticleEmitter>& descriptor )
    : SceneNode(descriptor,
                GetSceneNodeType<ParticleEmitter>(),
                to_base(ComponentType::TRANSFORM) |
                to_base(ComponentType::BOUNDS) |
                to_base(ComponentType::RENDERING))
{
    _buffersDirty.fill(false);
}

ParticleEmitter::~ParticleEmitter()
{ 
    assert(_particles == nullptr);
}

bool ParticleEmitter::load( PlatformContext& context )
{
    for ( U8 i = 0u; i < s_MaxPlayerBuffers; ++i )
    {
        for ( U8 j = 0u; j < to_base( RenderStage::COUNT ); ++j )
        {
            ParticleBuffers& buffers = _particleGPUBuffers[i][j];
            buffers[g_particleGeometryBuffer] = context.gfx().newGPUBuffer(1u, Util::StringFormat("{}_buffer_vb_{}_{}", resourceName(), i, j).c_str());
            buffers[g_particlePositionBuffer] = context.gfx().newGPUBuffer(g_particleBufferSizeFactor, Util::StringFormat("{}_buffer_vb_{}_{}", resourceName(), i, j).c_str());
            buffers[g_particleColourBuffer] = context.gfx().newGPUBuffer(g_particleBufferSizeFactor, Util::StringFormat("{}_buffer_vb_{}_{}", resourceName(), i, j).c_str());
            buffers[g_particleIndexBuffer] = context.gfx().newGPUBuffer(1u, Util::StringFormat("{}_buffer_idx_{}_{}", resourceName(), i, j).c_str());
        }
    }

    return SceneNode::load( context );
}

ParticleEmitter::ParticleBufferRet ParticleEmitter::getDataBuffer(const RenderStage stage, const PlayerIndex idx)
{
    return ParticleBufferRet
    {
        ._buffers = _particleGPUBuffers[idx % s_MaxPlayerBuffers][to_U32(stage)].data(),
        ._handles = _particleGPUBufferHandles[idx % s_MaxPlayerBuffers][to_U32(stage)].data()
    };
}

bool ParticleEmitter::initData(const std::shared_ptr<ParticleData>& particleData)
{
    // assert if double init!
    DIVIDE_ASSERT(particleData != nullptr, "ParticleEmitter::updateData error: Invalid particle data!");
    _particles = particleData;
    const vector<float3>& geometry = particleData->particleGeometryVertices();
    const vector<U32>& indices = particleData->particleGeometryIndices();

    for (U8 i = 0u; i < s_MaxPlayerBuffers; ++i)
    {
        for (U8 j = 0u; j < to_base(RenderStage::COUNT); ++j)
        {
            ParticleBufferRet buffers = getDataBuffer(static_cast<RenderStage>(j), i);

            GPUBuffer_ptr& positionBuffer = buffers._buffers[g_particleGeometryBuffer];
            {
                GPUBuffer::SetBufferParams vbParams = {};
                vbParams._elementCount = to_U32(geometry.size());
                vbParams._elementSize = sizeof(float3);
                vbParams._updateFrequency = BufferUpdateFrequency::ONCE;
                vbParams._usageType = BufferUsageType::VERTEX_BUFFER;
                vbParams._bindIdx = g_particleGeometryBuffer;
                vbParams._initialData = { (bufferPtr)geometry.data(), geometry.size() * vbParams._elementSize};

                const BufferLock lock = positionBuffer->setBuffer(vbParams);
                DIVIDE_UNUSED(lock);
                buffers._handles[g_particleGeometryBuffer] = positionBuffer->_handle;
            }

            if (!indices.empty())
            {
                GPUBuffer_ptr& indexBuffer = buffers._buffers[g_particleIndexBuffer];

                GPUBuffer::SetBufferParams ibParams = {};
                ibParams._elementCount = to_U32(indices.size());
                ibParams._elementSize = sizeof(U32);
                ibParams._updateFrequency = BufferUpdateFrequency::ONCE;
                ibParams._usageType = BufferUsageType::INDEX_BUFFER;
                ibParams._initialData = { (bufferPtr)indices.data(), indices.size() * ibParams._elementSize };

                const BufferLock lock = indexBuffer->setBuffer(ibParams);
                DIVIDE_UNUSED(lock);
                buffers._handles[g_particleIndexBuffer] = indexBuffer->_handle;
            }
        }
    }

    const U32 particleCount = _particles->totalCount();

    for ( U8 i = 0; i < s_MaxPlayerBuffers; ++i )
    {
        for ( U8 j = 0; j < to_base( RenderStage::COUNT ); ++j )
        {
            ParticleBufferRet buffer = getDataBuffer( static_cast<RenderStage>(j), i );
            GPUBuffer::SetBufferParams vbParams = {};
            vbParams._elementCount = particleCount;
            vbParams._usageType = BufferUsageType::VERTEX_BUFFER;
            vbParams._updateFrequency = BufferUpdateFrequency::OCCASIONAL;

            
            GPUBuffer_ptr& positionBuffer = buffer._buffers[g_particlePositionBuffer];
            vbParams._elementSize = sizeof( float4 );
            vbParams._bindIdx = g_particlePositionBuffer;
            BufferLock lock = positionBuffer->setBuffer( vbParams );
            DIVIDE_UNUSED( lock );
            buffer._handles[g_particlePositionBuffer] = positionBuffer->_handle;

            GPUBuffer_ptr& colourBuffer = buffer._buffers[g_particleColourBuffer];
            vbParams._elementSize = sizeof( UColour4 );
            vbParams._bindIdx = g_particleColourBuffer;
            lock = colourBuffer->setBuffer(vbParams);
            DIVIDE_UNUSED( lock );
            buffer._handles[g_particleColourBuffer] = colourBuffer->_handle;
        }
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
        vertBinding._strideInBytes = 3 * sizeof( F32 );
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
        vertBinding._strideInBytes = 3 * sizeof( F32 );
    }

    const bool useTexture = !_particles->_textureFileName.empty();
    Handle<Material> matHandle = CreateResource(ResourceDescriptor<Material>(useTexture ? "Material_particles_Texture" : "Material_particles"));
    Material* mat = Get(matHandle);

    mat->setPipelineLayout(topology, vertexFormat);
    mat->computeRenderStateCBK([]([[maybe_unused]] Material* material, [[maybe_unused]] const RenderStagePass stagePass, RenderStateBlock& blockInOut)
    {
        blockInOut._cullMode = CullMode::NONE;
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

        mat->setTexture(TextureSlot::UNIT0, texture, {}, TextureOperation::NONE);
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
    const U32 idxCount = to_U32( _particles->particleGeometryIndices().size() );
    if (idxCount > 0)
    {

        GenericDrawCommand& cmd = cmdsOut.emplace_back();
        toggleOption(cmd, CmdRenderOptions::RENDER_INDIRECT);

        cmd._cmd.indexCount = idxCount;
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

    if ( _enabled &&  getAliveParticleCount() > 0)
    {
        sgn->context().taskPool(TaskPoolType::HIGH_PRIORITY).wait(*_bufferUpdate);

        ParticleBufferRet buffers = getDataBuffer(renderStagePass._stage, 0);

        rComp.setIndexBufferElementOffset(buffers._buffers[g_particleIndexBuffer]->firstIndexOffsetCount());

        if (refreshData && _buffersDirty[to_U32(renderStagePass._stage)])
        {
            postDrawMemCmd._bufferLocks.emplace_back(buffers._buffers[g_particlePositionBuffer]->updateBuffer(0u, to_U32(_particles->_renderingPositions.size()), _particles->_renderingPositions.data()));
            postDrawMemCmd._bufferLocks.emplace_back(buffers._buffers[g_particleColourBuffer]->updateBuffer(0u, to_U32(_particles->_renderingColours.size()), _particles->_renderingColours.data()));
            for (U8 i = 0u; i < g_particleBufferSizeFactor + 1u; ++i)
            {
                buffers._buffers[i]->incQueue();
            }

            _buffersDirty[to_U32(renderStagePass._stage)] = false;
        }
 
        RenderingComponent::DrawCommands& cmds = rComp.drawCommands();
        {
            LockGuard<SharedMutex> w_lock(cmds._dataLock);
            GenericDrawCommand& cmd = cmds._data.front();
            cmd._cmd.instanceCount = to_U32(_particles->_renderingPositions.size());
            cmd._sourceBuffers = buffers._handles;
            cmd._sourceBuffersCount = g_particleBufferSizeFactor + 1u;
        }

        if (renderStagePass._passType == RenderPassType::PRE_PASS)
        {
            const float3& eyePos = cameraSnapshot._eye;
            const U32 aliveCount = getAliveParticleCount();

            vector<float4>& misc = _particles->_misc;
            vector<float4>& pos = _particles->_position;


            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = aliveCount;
            descriptor._partitionSize = 1000u;
            Parallel_For( sgn->context().taskPool( TaskPoolType::HIGH_PRIORITY ), descriptor, [&eyePos, &misc, &pos](const Task*, const U32 start, const U32 end)
            {
                for (U32 i = start; i < end; ++i)
                {
                    misc[i].w = pos[i].xyz.distanceSquared(eyePos);
                }
            });

            _bufferUpdate = CreateTask(
                [this, &renderStagePass](const Task&)
                {
                    // invalidateCache means that the existing particle data is no longer partially sorted
                    _particles->sort();
                    _buffersDirty[to_U32(renderStagePass._stage)] = true;
                });

            sgn->context().taskPool(TaskPoolType::HIGH_PRIORITY).enqueue(*_bufferUpdate);
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

    if (_enabled)
    {
        U32 aliveCount = getAliveParticleCount();
        renderState().drawState(aliveCount > 0);

        const TransformComponent* transform = sgn->get<TransformComponent>();

        const float3& pos = transform->getWorldPosition();
        const quatf& rot = transform->getWorldOrientation();

        F32 averageEmitRate = 0;
        for (const std::shared_ptr<ParticleSource>& source : _sources)
        {
            source->updateTransform(pos, rot);
            source->emit(g_updateInterval, _particles);
            averageEmitRate += source->emitRate();
        }
        averageEmitRate /= _sources.size();

        aliveCount = getAliveParticleCount();


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
            up->update(g_updateInterval, data);
        }

        sgn->context().taskPool(TaskPoolType::HIGH_PRIORITY).wait(*_bbUpdate);

        _bbUpdate = CreateTask([this, aliveCount, averageEmitRate](const Task&)
        {
            BoundingBox aabb{};
            for (U32 i = 0; i < aliveCount; i += to_U32(averageEmitRate) / 4)
            {
                aabb.add(_particles->_position[i]);
            }
            setBounds(aabb);
        });
        sgn->context().taskPool(TaskPoolType::HIGH_PRIORITY).enqueue(*_bbUpdate);
    }

    SceneNode::sceneUpdate(deltaTimeUS, sgn, sceneState);
}

U32 ParticleEmitter::getAliveParticleCount() const noexcept
{
    return _particles ? _particles->aliveCount() : 0u;
}

}
