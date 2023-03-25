#include "stdafx.h"

#include "Headers/ParticleEmitter.h"


#include "Core/Headers/Configuration.h"
#include "Core/Headers/StringHelper.h"
#include "Scenes/Headers/Scene.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Scenes/Headers/SceneState.h"
#include "Geometry/Material/Headers/Material.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"

namespace Divide {
namespace {
    // 3 should always be enough for round-robin GPU updates to avoid stalls:
    // 1 in ram, 1 in driver and 1 in VRAM
    constexpr U32 g_particleBufferSizeFactor = 3;
    constexpr U32 g_particleGeometryBuffer = 0;
    constexpr U32 g_particlePositionBuffer = g_particleGeometryBuffer + 1;
    constexpr U32 g_particleColourBuffer = g_particlePositionBuffer* + 2;

    constexpr U64 g_updateInterval = Time::MillisecondsToMicroseconds(33);
}

ParticleEmitter::ParticleEmitter(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name)
    : SceneNode(parentCache,
                descriptorHash,
                name,
                ResourcePath(name),
                {},
                SceneNodeType::TYPE_PARTICLE_EMITTER,
                to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS) | to_base(ComponentType::RENDERING)),
      _context(context)
{
    for (U8 i = 0u; i < s_MaxPlayerBuffers; ++i) {
        for (U8 j = 0u; j < to_base(RenderStage::COUNT); ++j) {
            _particleGPUBuffers[i][j] = _context.newGVD(g_particleBufferSizeFactor, Util::StringFormat("%s_buffer_%d_%d", name.c_str(), i,j).c_str());
        }
    }

    _buffersDirty.fill(false);
}

ParticleEmitter::~ParticleEmitter()
{ 
    assert(_particles == nullptr);
}

GenericVertexData& ParticleEmitter::getDataBuffer(const RenderStage stage, const PlayerIndex idx) {
    return *_particleGPUBuffers[idx % s_MaxPlayerBuffers][to_U32(stage)];
}

bool ParticleEmitter::initData(const std::shared_ptr<ParticleData>& particleData) {
    // assert if double init!
    DIVIDE_ASSERT(particleData != nullptr, "ParticleEmitter::updateData error: Invalid particle data!");
    _particles = particleData;
    const vector<vec3<F32>>& geometry = particleData->particleGeometryVertices();
    const vector<U32>& indices = particleData->particleGeometryIndices();

    for (U8 i = 0u; i < s_MaxPlayerBuffers; ++i) {
        for (U8 j = 0u; j < to_base(RenderStage::COUNT); ++j) {
            GenericVertexData& buffer = getDataBuffer(static_cast<RenderStage>(j), i);

            GenericVertexData::SetBufferParams params = {};
            params._bindConfig = { g_particleGeometryBuffer, g_particleGeometryBuffer };
            params._bufferParams._elementCount = to_U32(geometry.size());
            params._bufferParams._elementSize = sizeof(vec3<F32>);
            params._bufferParams._flags._updateFrequency = BufferUpdateFrequency::ONCE;
            params._bufferParams._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;
            params._initialData = { (Byte*)geometry.data(), geometry.size() * params._bufferParams._elementSize};
            params._useRingBuffer = false;

            {
                const BufferLock lock = buffer.setBuffer(params);
                DIVIDE_UNUSED(lock);
            }

            if (!indices.empty()) {
                GenericVertexData::IndexBuffer idxBuff{};
                idxBuff.smallIndices = false;
                idxBuff.count = to_U32(indices.size());
                idxBuff.data = (Byte*)indices.data();
                idxBuff.dynamic = false;

                const BufferLock lock = buffer.setIndexBuffer(idxBuff);
                DIVIDE_UNUSED(lock);
            }
        }
    }

    if (!updateData()) {
        return false;
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

    const bool useTexture = _particleTexture != nullptr;
    Material_ptr mat = CreateResource<Material>(_parentCache, ResourceDescriptor(useTexture ? "Material_particles_Texture" : "Material_particles"));
    mat->setPipelineLayout(topology, vertexFormat);

    mat->computeRenderStateCBK([]([[maybe_unused]] Material* material, const RenderStagePass stagePass, RenderStateBlock& blockInOut) {
        blockInOut.setCullMode(CullMode::NONE);
        blockInOut.setZFunc(IsDepthPass(stagePass) ? ComparisonFunction::LEQUAL : ComparisonFunction::EQUAL);
    });

    mat->computeShaderCBK([useTexture]([[maybe_unused]] Material* material, const RenderStagePass stagePass) {
        ShaderModuleDescriptor vertModule{ ShaderType::VERTEX, "particles.glsl", useTexture ? "WithTexture" : "NoTexture" };
        ShaderModuleDescriptor fragModule{ ShaderType::FRAGMENT, "particles.glsl", useTexture ? "WithTexture" : "NoTexture" };

        if (useTexture) {
            fragModule._defines.emplace_back("HAS_TEXTURE");
        }

        ShaderProgramDescriptor particleShaderDescriptor = {};
        particleShaderDescriptor._name = useTexture ? "particles_WithTexture" : "particles_NoTexture";
        particleShaderDescriptor._modules.push_back(vertModule);

        if (stagePass._stage == RenderStage::DISPLAY) {
            if (IsDepthPass(stagePass)) {
                fragModule._variant = "PrePass";
                fragModule._defines.emplace_back("PRE_PASS");
                particleShaderDescriptor._name = useTexture ? "particles_prePass_WithTexture" : "particles_prePass_NoTexture";
            }
            particleShaderDescriptor._modules.push_back(fragModule);
        } else if (IsDepthPass(stagePass)) {
            if (stagePass._stage == RenderStage::SHADOW) {
                fragModule._variant = "Shadow.VSM";
                particleShaderDescriptor._modules.push_back(fragModule);
                particleShaderDescriptor._name = "particles_VSM";
            } else {
                particleShaderDescriptor._name = "particles_DepthPass";
            }
        }

        return particleShaderDescriptor;
    });

    if (_particleTexture) {
        SamplerDescriptor textureSampler = {};
        mat->setTexture(TextureSlot::UNIT0, _particleTexture, textureSampler.getHash(), TextureOperation::NONE);
    }

    setMaterialTpl(mat);

    return true;
}

bool ParticleEmitter::updateData() {
    const U32 particleCount = _particles->totalCount();

    for (U8 i = 0; i < s_MaxPlayerBuffers; ++i) {
        for (U8 j = 0; j < to_base(RenderStage::COUNT); ++j) {
            GenericVertexData& buffer = getDataBuffer(static_cast<RenderStage>(j), i);

            GenericVertexData::SetBufferParams params = {};
            params._bindConfig = { g_particlePositionBuffer, g_particlePositionBuffer };
            params._useRingBuffer = true;

            params._bufferParams._elementCount = particleCount;
            params._bufferParams._elementSize = sizeof(vec4<F32>);
            params._bufferParams._flags._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
            params._bufferParams._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;
            BufferLock lock = buffer.setBuffer(params);
            DIVIDE_UNUSED( lock );

            params._bindConfig = { g_particleColourBuffer, g_particleColourBuffer };
            params._bufferParams._elementCount = particleCount;
            params._bufferParams._elementSize = sizeof(UColour4);

            lock = buffer.setBuffer(params);
            DIVIDE_UNUSED( lock );
        }
    }

    for (U32 i = 0; i < particleCount; ++i) {
        // Distance to camera (squared)
        _particles->_misc[i].w = -1.0f;
    }

    if (!_particles->_textureFileName.empty()) {
        TextureDescriptor textureDescriptor(TextureType::TEXTURE_2D_ARRAY);
        textureDescriptor.srgb(true);

        ResourceDescriptor texture(_particles->_textureFileName);
        texture.propertyDescriptor(textureDescriptor);

        _particleTexture = CreateResource<Texture>(_parentCache, texture);
    }

    return true;
}

bool ParticleEmitter::unload() {
    WAIT_FOR_CONDITION(getState() == ResourceState::RES_LOADED);
    
    _particles.reset();

    return SceneNode::unload();
}

void ParticleEmitter::buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut) {
    GenericDrawCommand cmd = {};
    cmd._cmd.indexCount = to_U32(_particles->particleGeometryIndices().size());
    if (cmd._cmd.indexCount == 0) {
        cmd._cmd.indexCount = to_U32(_particles->particleGeometryVertices().size());
    }
    cmdsOut.emplace_back(GFX::DrawCommand{ cmd });

    SceneNode::buildDrawCommands(sgn, cmdsOut);
}

void ParticleEmitter::prepareRender(SceneGraphNode* sgn,
                                    RenderingComponent& rComp,
                                    RenderPackage& pkg,
                                    GFX::MemoryBarrierCommand& postDrawMemCmd,
                                    const RenderStagePass renderStagePass,
                                    const CameraSnapshot& cameraSnapshot,
                                    const bool refreshData) {

    if ( _enabled &&  getAliveParticleCount() > 0)
    {
        Wait(*_bufferUpdate, _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));
        if (refreshData && _buffersDirty[to_U32(renderStagePass._stage)])
        {
            GenericVertexData& buffer = getDataBuffer(renderStagePass._stage, 0);
            postDrawMemCmd._bufferLocks.emplace_back(buffer.updateBuffer(g_particlePositionBuffer, 0u, to_U32(_particles->_renderingPositions.size()), _particles->_renderingPositions.data()));
            postDrawMemCmd._bufferLocks.emplace_back(buffer.updateBuffer(g_particleColourBuffer, 0u, to_U32(_particles->_renderingColours.size()), _particles->_renderingColours.data()));

            buffer.incQueue();
            _buffersDirty[to_U32(renderStagePass._stage)] = false;
        }

        RenderingComponent::DrawCommands& cmds = rComp.drawCommands();
        {
            LockGuard<SharedMutex> w_lock(cmds._dataLock);
            GenericDrawCommand& cmd = cmds._data.front()._drawCommands.front();
            cmd._cmd.instanceCount = to_U32(_particles->_renderingPositions.size());
            cmd._sourceBuffer = getDataBuffer(renderStagePass._stage, 0).handle();
        }

        if (renderStagePass._passType == RenderPassType::PRE_PASS) {
            const vec3<F32>& eyePos = cameraSnapshot._eye;
            const U32 aliveCount = getAliveParticleCount();

            vector<vec4<F32>>& misc = _particles->_misc;
            vector<vec4<F32>>& pos = _particles->_position;


            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = aliveCount;
            descriptor._partitionSize = 1000u;
            descriptor._cbk = [&eyePos, &misc, &pos](const Task*, const U32 start, const U32 end) {
                for (U32 i = start; i < end; ++i) {
                    misc[i].w = pos[i].xyz.distanceSquared(eyePos);
                }
            };

            parallel_for(_context.context(), descriptor);

            _bufferUpdate = CreateTask(
                [this, &renderStagePass](const Task&) {
                // invalidateCache means that the existing particle data is no longer partially sorted
                _particles->sort();
                _buffersDirty[to_U32(renderStagePass._stage)] = true;
            });

            Start(*_bufferUpdate, _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));
        }
    }

    SceneNode::prepareRender(sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData);
}


/// Pre-process particles
void ParticleEmitter::sceneUpdate(const U64 deltaTimeUS,
                                  SceneGraphNode* sgn,
                                  SceneState& sceneState) {

    constexpr U32 s_particlesPerThread = 1024;

    if (_enabled) {
        U32 aliveCount = getAliveParticleCount();
        renderState().drawState(aliveCount > 0);

        const TransformComponent* transform = sgn->get<TransformComponent>();

        const vec3<F32>& pos = transform->getWorldPosition();
        const Quaternion<F32>& rot = transform->getWorldOrientation();

        F32 averageEmitRate = 0;
        for (const std::shared_ptr<ParticleSource>& source : _sources) {
            source->updateTransform(pos, rot);
            source->emit(g_updateInterval, _particles);
            averageEmitRate += source->emitRate();
        }
        averageEmitRate /= _sources.size();

        aliveCount = getAliveParticleCount();


        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = aliveCount;
        descriptor._partitionSize = s_particlesPerThread;
        descriptor._cbk = [this](const Task*, const U32 start, const U32 end) {
            for (U32 i = start; i < end; ++i) {
                _particles->_position[i].w = _particles->_misc[i].z;
                _particles->_acceleration[i].set(0.0f);
            }
        };

        parallel_for(_context.context(), descriptor);

        ParticleData& data = *_particles;
        for (const std::shared_ptr<ParticleUpdater>& up : _updaters) {
            up->update(g_updateInterval, data);
        }

        Wait(*_bbUpdate, _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));

        _bbUpdate = CreateTask([this, aliveCount, averageEmitRate](const Task&)
        {
            BoundingBox aabb;
            for (U32 i = 0; i < aliveCount; i += to_U32(averageEmitRate) / 4) {
                aabb.add(_particles->_position[i]);
            }
            setBounds(aabb);
        });
        Start(*_bbUpdate, _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));
    }

    SceneNode::sceneUpdate(deltaTimeUS, sgn, sceneState);
}

U32 ParticleEmitter::getAliveParticleCount() const noexcept {
    if (!_particles) {
        return 0u;
    }
    return _particles->aliveCount();
}

}
