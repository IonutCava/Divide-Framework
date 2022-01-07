#include "stdafx.h"

#include "Headers/ParticleEmitter.h"


#include "Core/Headers/Configuration.h"
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
    for (U8 i = 0; i < s_MaxPlayerBuffers; ++i) {
        for (U8 j = 0; j < to_base(RenderStage::COUNT); ++j) {
            _particleGPUBuffers[i][j] = _context.newGVD(g_particleBufferSizeFactor);
            _particleGPUBuffers[i][j]->renderIndirect(true);
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

    for (U8 i = 0; i < s_MaxPlayerBuffers; ++i) {
        for (U8 j = 0; j < to_base(RenderStage::COUNT); ++j) {
            GenericVertexData& buffer = getDataBuffer(static_cast<RenderStage>(j), i);

            buffer.create(3);

            GenericVertexData::SetBufferParams params = {};
            params._buffer = g_particleGeometryBuffer;
            params._bufferParams._elementCount = to_U32(geometry.size());
            params._bufferParams._elementSize = sizeof(vec3<F32>);
            params._bufferParams._updateFrequency = BufferUpdateFrequency::RARELY;
            params._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
            params._bufferParams._sync = false;
            params._bufferParams._initialData = { (Byte*)geometry.data(), geometry.size() * params._bufferParams._elementSize};
            params._useRingBuffer = false;

            buffer.setBuffer(params);

            if (!indices.empty()) {
                GenericVertexData::IndexBuffer idxBuff;
                idxBuff.smallIndices = false;
                idxBuff.count = to_U32(indices.size());
                idxBuff.data = (Byte*)indices.data();

                buffer.setIndexBuffer(idxBuff, BufferUpdateFrequency::RARELY);
            }

            AttributeDescriptor& desc = buffer.attribDescriptor(to_base(AttribLocation::POSITION));
            desc.set(g_particleGeometryBuffer, 3, GFXDataFormat::FLOAT_32);
        }
    }

    if (!updateData()) {
        return false;
    }

    const bool useTexture = _particleTexture != nullptr;
    Material_ptr mat = CreateResource<Material>(_parentCache, ResourceDescriptor(useTexture ? "Material_particles_Texture" : "Material_particles"));

    // Generate a render state
    RenderStateBlock particleRenderState;
    particleRenderState.setCullMode(CullMode::NONE);
    particleRenderState.setZFunc(ComparisonFunction::EQUAL);
    _particleStateBlockHash = particleRenderState.getHash();

    particleRenderState.setZFunc(ComparisonFunction::LEQUAL);
    _particleStateBlockHashDepth = particleRenderState.getHash();

    mat->setRenderStateBlock(_particleStateBlockHashDepth, RenderStage::COUNT, RenderPassType::PRE_PASS);
    mat->setRenderStateBlock(_particleStateBlockHash, RenderStage::COUNT, RenderPassType::MAIN_PASS);
    mat->setRenderStateBlock(_particleStateBlockHash, RenderStage::COUNT, RenderPassType::OIT_PASS);

    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "particles.glsl";
    vertModule._variant = useTexture ? "WithTexture" : "NoTexture";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "particles.glsl";
    fragModule._variant = useTexture ? "WithTexture" : "NoTexture";

    if (useTexture){
        fragModule._defines.emplace_back("HAS_TEXTURE", true);
    }

    ShaderProgramDescriptor particleShaderDescriptor = {};
    particleShaderDescriptor._name = useTexture ? "particles_WithTexture" : "particles_NoTexture";
    particleShaderDescriptor._modules.push_back(vertModule);
    particleShaderDescriptor._modules.push_back(fragModule);

    ShaderProgramDescriptor particleShaderPrePassDescriptor = particleShaderDescriptor;
    particleShaderPrePassDescriptor._name = useTexture ? "particles_prePass_WithTexture" : "particles_prePass_NoTexture";
    particleShaderPrePassDescriptor._modules.back()._variant = "PrePass";
    particleShaderPrePassDescriptor._modules.back()._defines.emplace_back("PRE_PASS", true);

    ShaderProgramDescriptor particleShaderDepthDescriptor = particleShaderDescriptor;
    particleShaderDepthDescriptor._name = "particles_DepthPass";
    particleShaderDepthDescriptor._modules.pop_back();


    ShaderProgramDescriptor particleShaderShadowDescriptor = particleShaderDescriptor;
    particleShaderShadowDescriptor._name = "particles_VSM";
    particleShaderShadowDescriptor._modules.back()._variant = "Shadow.VSM";

    ShaderProgramDescriptor particleShaderShadowVSMDescriptor = particleShaderShadowDescriptor;
    particleShaderShadowVSMDescriptor._name = "particles_VSM_ORTHO";
    particleShaderShadowVSMDescriptor._modules.back()._variant += ".ORTHO";
    particleShaderShadowVSMDescriptor._modules.back()._defines.emplace_back("ORTHO_PROJECTION", true);

    mat->setShaderProgram(particleShaderDepthDescriptor,     RenderStage::COUNT,   RenderPassType::PRE_PASS);
    mat->setShaderProgram(particleShaderPrePassDescriptor,   RenderStage::DISPLAY, RenderPassType::PRE_PASS);
    mat->setShaderProgram(particleShaderDescriptor,          RenderStage::COUNT,   RenderPassType::MAIN_PASS);
    mat->setShaderProgram(particleShaderShadowDescriptor,    RenderStage::SHADOW,  RenderPassType::COUNT);
    mat->setShaderProgram(particleShaderShadowVSMDescriptor, RenderStage::SHADOW,  RenderPassType::COUNT, static_cast<RenderStagePass::VariantType>(LightType::DIRECTIONAL));

    if (_particleTexture) {
        SamplerDescriptor textureSampler = {};
        mat->setTexture(TextureUsage::UNIT0, _particleTexture, textureSampler.getHash(),  TextureOperation::NONE, TexturePrePassUsage::ALWAYS);
    }

    setMaterialTpl(mat);

    return true;
}

bool ParticleEmitter::updateData() {
    constexpr U32 positionAttribLocation = 13;
    constexpr U32 colourAttribLocation = to_base(AttribLocation::COLOR);

    const U32 particleCount = _particles->totalCount();

    for (U8 i = 0; i < s_MaxPlayerBuffers; ++i) {
        for (U8 j = 0; j < to_base(RenderStage::COUNT); ++j) {
            GenericVertexData& buffer = getDataBuffer(static_cast<RenderStage>(j), i);

            GenericVertexData::SetBufferParams params = {};
            params._buffer = g_particlePositionBuffer;
            params._useRingBuffer = true;
            params._instanceDivisor = 1;

            params._bufferParams._elementCount = particleCount;
            params._bufferParams._elementSize = sizeof(vec4<F32>);
            params._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
            params._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
            params._bufferParams._sync = true;
            params._bufferParams._initialData = { nullptr, 0 };

            buffer.setBuffer(params);

            params._buffer = g_particleColourBuffer;
            params._bufferParams._elementCount = particleCount;
            params._bufferParams._elementSize = sizeof(UColour4);

            buffer.setBuffer(params);

            buffer.attribDescriptor(positionAttribLocation).set(g_particlePositionBuffer,
                                                                4,
                                                                GFXDataFormat::FLOAT_32,
                                                                false,
                                                                0);

            buffer.attribDescriptor(colourAttribLocation).set(g_particleColourBuffer,
                                                              4,
                                                              GFXDataFormat::UNSIGNED_BYTE,
                                                              true,
                                                              0);
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
    cmd._primitiveType = _particles->particleGeometryType();
    cmd._cmd.indexCount = to_U32(_particles->particleGeometryIndices().size());
    if (cmd._cmd.indexCount == 0) {
        cmd._cmd.indexCount = to_U32(_particles->particleGeometryVertices().size());
    }
    cmdsOut.emplace_back(GFX::DrawCommand{ cmd });

    SceneNode::buildDrawCommands(sgn, cmdsOut);
}

void ParticleEmitter::prepareRender(SceneGraphNode* sgn,
                                    RenderingComponent& rComp,
                                    const RenderStagePass renderStagePass,
                                    const CameraSnapshot& cameraSnapshot,
                                    GFX::CommandBuffer& bufferInOut,
                                    const bool refreshData) {

    if ( _enabled &&  getAliveParticleCount() > 0) {
        Wait(*_bufferUpdate, _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));

        if (refreshData && _buffersDirty[to_U32(renderStagePass._stage)]) {
            GenericVertexData& buffer = getDataBuffer(renderStagePass._stage, 0);
            buffer.updateBuffer(g_particlePositionBuffer, 0u, to_U32(_particles->_renderingPositions.size()), _particles->_renderingPositions.data());
            buffer.updateBuffer(g_particleColourBuffer, 0u, to_U32(_particles->_renderingColours.size()), _particles->_renderingColours.data());
            buffer.incQueue();
            _buffersDirty[to_U32(renderStagePass._stage)] = false;
        }

        GenericDrawCommand& cmd = rComp.drawCommands().front()._drawCommands.front();
        cmd._cmd.primCount = to_U32(_particles->_renderingPositions.size());
        cmd._sourceBuffer = getDataBuffer(renderStagePass._stage, 0).handle();
        cmd._bufferIndex = 0u;

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

    SceneNode::prepareRender(sgn, rComp, renderStagePass, cameraSnapshot, bufferInOut, refreshData);
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
