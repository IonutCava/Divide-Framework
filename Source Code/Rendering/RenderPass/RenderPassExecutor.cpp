#include "stdafx.h"

#include "Headers/RenderPassExecutor.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RTAttachment.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Rendering/RenderPass/Headers/RenderQueue.h"
#include "Scenes/Headers/SceneState.h"

#include "ECS/Components/Headers/AnimationComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "Rendering/Headers/Renderer.h"

namespace Divide {
namespace {
    // Remove materials that haven't been indexed in this amount of frames to make space for new ones
    constexpr U16 g_maxMaterialFrameLifetime = 6u;
    // Use to partition parallel jobs
    constexpr U32 g_nodesPerPrepareDrawPartition = 16u;
}

bool RenderPassExecutor::s_globalDataInit = false;
Pipeline* RenderPassExecutor::s_OITCompositionPipeline = nullptr;
Pipeline* RenderPassExecutor::s_OITCompositionMSPipeline = nullptr;
Pipeline* RenderPassExecutor::s_ResolveScreenTargetsPipeline = nullptr;
Mutex RenderPassExecutor::s_globalMaterialBufferLock;
ShaderBuffer* RenderPassExecutor::s_globalMaterialBuffer = nullptr;
Mutex RenderPassExecutor::s_globalTransformBufferLock;
ShaderBuffer* RenderPassExecutor::s_globalTransformBuffer = nullptr;
U32 RenderPassExecutor::s_materialDataBufferIndex = 0u;
std::array<RenderPassExecutor::PerRingEntryMaterialData, RenderPass::DataBufferRingSize> RenderPassExecutor::s_materialData;
U32 RenderPassExecutor::s_transformDataBufferIndex = 0u;

RenderPassExecutor::RenderPassExecutor(RenderPassManager& parent, GFXDevice& context, const RenderStage stage)
    : _parent(parent)
    , _context(context)
    , _stage(stage)
    , _renderQueue(parent.parent(), stage)
{
}

void RenderPassExecutor::postInit(const ShaderProgram_ptr& OITCompositionShader,
                                  const ShaderProgram_ptr& OITCompositionShaderMS,
                                  const ShaderProgram_ptr& ResolveScreenTargetsShaderMS) const {

    if (!s_globalDataInit) {
        s_globalDataInit = true;

        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateHash = _context.get2DStateBlock();

        pipelineDescriptor._shaderProgramHandle = OITCompositionShader->getGUID();
        s_OITCompositionPipeline = _context.newPipeline(pipelineDescriptor);

        pipelineDescriptor._shaderProgramHandle = OITCompositionShaderMS->getGUID();
        s_OITCompositionMSPipeline = _context.newPipeline(pipelineDescriptor);

        pipelineDescriptor._shaderProgramHandle = ResolveScreenTargetsShaderMS->getGUID();
        s_ResolveScreenTargetsPipeline = _context.newPipeline(pipelineDescriptor);

        U32 totalPassCount = 0u;
        for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i) {
            totalPassCount += RenderStagePass::totalPassCountForStage(static_cast<RenderStage>(i));
        }
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
        bufferDescriptor._bufferParams._syncAtEndOfCmdBuffer = true;
        bufferDescriptor._ringBufferLength = RenderPass::DataBufferRingSize;
        bufferDescriptor._separateReadWrite = false;
        bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
        bufferDescriptor._flags = to_U32(ShaderBuffer::Flags::NONE);

        {// Node Transform buffer
            bufferDescriptor._bufferParams._elementCount = totalPassCount * Config::MAX_VISIBLE_NODES;
            bufferDescriptor._bufferParams._elementSize = sizeof(NodeTransformData);
            bufferDescriptor._name = "NODE_TRANSFORM_DATA";
            s_globalTransformBuffer = _context.newSB(bufferDescriptor);
        }
        {// Node Material buffer
            bufferDescriptor._bufferParams._elementCount = Config::MAX_CONCURRENT_MATERIALS;
            bufferDescriptor._bufferParams._elementSize = sizeof(NodeMaterialData);
            bufferDescriptor._name = "NODE_MATERIAL_DATA";
            s_globalMaterialBuffer = _context.newSB(bufferDescriptor);

            ScopedLock<Mutex> w_lock(s_globalMaterialBufferLock);
            s_materialDataBufferIndex = s_globalMaterialBuffer->queueWriteIndex();
            for (PerRingEntryMaterialData& data : s_materialData) {
                data._nodeMaterialLookupInfo.fill({ Material::INVALID_MAT_HASH, U16_MAX });
            }
        }
    }
}

void RenderPassExecutor::setMaterialInfoAt(const size_t idx, PerRingEntryMaterialData::MaterialDataContainer& dataInOut, const NodeMaterialData& tempData, const NodeMaterialTextures& tempTextures) {
    OPTICK_EVENT();

    assert(idx < dataInOut.size());

    NodeMaterialData& target = dataInOut[idx];
    target = tempData;

    // GL_ARB_bindless_texture:
    // In the following four constructors, the low 32 bits of the sampler
    // type correspond to the .x component of the uvec2 and the high 32 bits
    // correspond to the .y component.
    // uvec2(any sampler type)     // Converts a sampler type to a pair of 32-bit unsigned integers
    // any sampler type(uvec2)     // Converts a pair of 32-bit unsigned integers to a sampler type
    // uvec2(any image type)       // Converts an image type to a pair of 32-bit unsigned integers
    // any image type(uvec2)       // Converts a pair of 32-bit unsigned integers to an image type
    for (U8 i = 0u; i < MATERIAL_TEXTURE_COUNT; ++i) {
        const SamplerAddress combined = tempTextures[i];
        target._textures[i / 2][(i % 2) * 2 + 0] = to_U32(combined & 0xFFFFFFFF); //low
        target._textures[i / 2][(i % 2) * 2 + 1] = to_U32(combined >> 32); //high
    }

    // second loop for cache reasons. 0u is fine as an address since we filter it at graphics API level.
    for (U8 i = 0u; i < MATERIAL_TEXTURE_COUNT; ++i) {
        _uniqueTextureAddresses.insert(tempTextures[i]);
    }
}

void RenderPassExecutor::processVisibleNodeTransform(PerRingEntryTransformData& transformData,
                                                     RenderingComponent* rComp,
                                                     const RenderStage stage,
                                                     const D64 interpolationFactor,
                                                     const U16 nodeIndex) {
    OPTICK_EVENT();

    // Rewrite all transforms
    // ToDo: Cache transforms for static nodes -Ionut
    NodeTransformData& transformOut = transformData[nodeIndex];
    //transformOut = {}; //We will rewrite everything anyway

    const SceneGraphNode* node = rComp->getSGN();
    { // Transform
        const TransformComponent* const transform = node->get<TransformComponent>();

        // Get the node's world matrix properly interpolated
        transform->getPreviousWorldMatrix(transformOut._prevWorldMatrix);
        transform->getWorldMatrix(interpolationFactor, transformOut._worldMatrix);
        transformOut._normalMatrixW.set(transformOut._worldMatrix);
        transformOut._normalMatrixW.setRow(3, 0.f, 0.f, 0.f, 1.f);

        if (!transform->isUniformScaled()) {
            // Non-uniform scaling requires an inverseTranspose to negatescaling contribution but preserve rotation
            transformOut._normalMatrixW.inverseTranspose();
        }
    }

    U8 boneCount = 0u;
    U8 frameTicked = 0u;
    { //Animation
        AnimationComponent* animComp = node->get<AnimationComponent>();
        if (animComp && animComp->playAnimations()) {
            boneCount = animComp->boneCount();
            if (animComp->frameTicked()) {
                frameTicked = 1u;
            }
        }
    }
    { //Misc
        const F32 nodeFlagValue = rComp->dataFlag();
        const U8 lod = rComp->getLodLevel(stage);
        const U8 occlusionCull = rComp->occlusionCull() ? 1u : 0u;

        // Since the normal matrix is 3x3, we can use the extra row and column to store additional data
        const BoundsComponent* const bounds = node->get<BoundsComponent>();
        const BoundingSphere& bSphere = bounds->getBoundingSphere();
        const vec3<F32>& bSphereCenter = bSphere.getCenter();
        const vec3<F32> bBoxHalfExtents = bounds->getBoundingBox().getHalfExtent();

        transformOut._normalMatrixW.setRow(3, bSphereCenter.x, bSphereCenter.y, bSphereCenter.z, nodeFlagValue);
        transformOut._normalMatrixW.element(0, 3) = to_F32(Util::PACK_UNORM4x8(boneCount, lod, frameTicked, occlusionCull));
        transformOut._normalMatrixW.element(1, 3) = to_F32(Util::PACK_HALF2x16(bBoxHalfExtents.xy));
        transformOut._normalMatrixW.element(2, 3) = to_F32(Util::PACK_HALF2x16(bBoxHalfExtents.z, bSphere.getRadius()));
    }
}

U16 RenderPassExecutor::processVisibleNodeMaterial(RenderingComponent* rComp) {
    OPTICK_EVENT();

    NodeMaterialData tempData{};
    NodeMaterialTextures tempTextures{};
    // Get the colour matrix (base colour, metallic, etc)
    rComp->getMaterialData(tempData, tempTextures);

    // Match materials
    size_t materialHash = HashMaterialData(tempData);
    Util::Hash_combine(materialHash, HashTexturesData(tempTextures));

    const auto findMaterialMatch = [](const size_t targetHash, PerRingEntryMaterialData::LookupInfoContainer& data) -> U16 {
        assert(data.size() < U16_MAX);

        const U16 count = to_U16(data.size());
        for (U16 idx = 0u; idx < count; ++idx) {
            auto& [hash, _] = data[idx];
            if (hash == targetHash) {
                return idx;
            }
        }

        return U16_MAX;
    };

    ScopedLock<Mutex> w_lock(s_globalMaterialBufferLock);
    PerRingEntryMaterialData& materialData = s_materialData[s_materialDataBufferIndex];
    PerRingEntryMaterialData::LookupInfoContainer& infoContainer = materialData._nodeMaterialLookupInfo;
    {// Try and match an existing material
        OPTICK_EVENT("processVisibleNode - try match material");
        const U16 idx = findMaterialMatch(materialHash, infoContainer);
        if (idx != U16_MAX) {
            infoContainer[idx].second = 0u;
            return idx;
        }
    }

    // If we fail, try and find an empty slot and update it
    OPTICK_EVENT("processVisibleNode - process unmatched material");
    // No match found (cache miss) so add a new entry.
    std::pair<U16, U16> bestCandidate = { U16_MAX, 0u };

    const U16 count = to_U16(infoContainer.size());
    for (U16 idx = 0u; idx < count; ++idx) {
        auto& [hash, framesSinceLastUsed] = infoContainer[idx];
        // Two cases here. We either have empty slots (e.g. startup, cache clear, etc) ...
        if (hash == Material::INVALID_MAT_HASH) {
            // ... in which case our current idx is what we are looking for ...
            bestCandidate.first = idx;
            bestCandidate.second = g_maxMaterialFrameLifetime;
            break;
        }
        // ... else we need to find a slot with a stale entry (but not one that is still in flight!)
        if (framesSinceLastUsed >= std::max(g_maxMaterialFrameLifetime, bestCandidate.second)) {
            bestCandidate.first = idx;
            bestCandidate.second = framesSinceLastUsed;
            // Keep going and see if we can find an even older entry
        }
    }
    DIVIDE_ASSERT(bestCandidate.first != U16_MAX, "RenderPassExecutor::processVisibleNode error: too many concurrent materials! Increase Config::MAX_CONCURRENT_MATERIALS");

    if (_matUpdateRange._firstIDX > bestCandidate.first) {
        _matUpdateRange._firstIDX = bestCandidate.first;
    }
    if (_matUpdateRange._lastIDX < bestCandidate.first + 1u) {
        _matUpdateRange._lastIDX = bestCandidate.first + 1u;
    }

    infoContainer[bestCandidate.first] = { materialHash, 0u };
    setMaterialInfoAt(bestCandidate.first, materialData._nodeMaterialData, tempData, tempTextures);

    return bestCandidate.first;
}

U16 RenderPassExecutor::buildDrawCommands(const RenderPassParams& params, const bool doPrePass, const bool doOITPass, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    constexpr bool doMainPass = true;

    RenderStagePass stagePass = params._stagePass;
    RenderPass::BufferData bufferData = _parent.getPassForStage(_stage).getBufferData(stagePass);
    ShaderBuffer* cmdBuffer = bufferData._commandBuffer;

    const D64 interpFactor = GFXDevice::FrameInterpolationFactor();

    _drawCommands.clear();
 
    _uniqueTextureAddresses.clear();

    for (RenderBin::SortedQueue& sQueue : _sortedQueues) {
        sQueue.resize(0);
        sQueue.reserve(Config::MAX_VISIBLE_NODES);
    }

    const U16 queueTotalSize = _renderQueue.getSortedQueues({}, _sortedQueues);

    { //Erase nodes with no draw commands
        const auto erasePredicate = [&stagePass](std::pair<RenderingComponent*, NodeDataIdx>& item) {
            return !Attorney::RenderingCompRenderPass::hasDrawCommands(*item.first, stagePass);
        };

        for (RenderBin::SortedQueue& queue : _sortedQueues) {
            erase_if(queue, erasePredicate);
        }
    }

    TaskPool& pool = _context.context().taskPool(TaskPoolType::HIGH_PRIORITY);
    Task* updateTask = CreateTask(TASK_NOP);

    {
        OPTICK_EVENT("buildDrawCommands - process nodes: Transforms")
        U32& nodeCount = *bufferData._lastNodeCount;
        nodeCount = 0u;
        const auto parseRange = [this, renderStage = stagePass._stage, interpFactor](const U32 nodeCount, RenderBin::SortedQueue& queue, const U32 start, const U32 end) {
            auto& nodeTransforms = _nodeTransformData[s_transformDataBufferIndex];
            for (U32 i = start; i < end; ++i) {
                auto& [rComp, dataIdx] = queue[i];
                dataIdx._transformIDX = to_U16(i + nodeCount);
                processVisibleNodeTransform(nodeTransforms, rComp, renderStage, interpFactor, dataIdx._transformIDX);
            }
        };

        for (RenderBin::SortedQueue& queue : _sortedQueues) {
            const U32 queueSize = to_U32(queue.size());
            if (queueSize > g_nodesPerPrepareDrawPartition) {
                const U32 midPoint = queueSize / 2;
                Start(*CreateTask(updateTask, [&queue, &parseRange, nodeCount, midPoint](const Task&) {
                    parseRange(nodeCount, queue, 0u, midPoint);
                }), pool); 
                Start(*CreateTask(updateTask, [&queue, &parseRange, nodeCount, midPoint, queueSize](const Task&) {
                    parseRange(nodeCount, queue, midPoint, queueSize);
                }), pool);
            } else {
                parseRange(nodeCount, queue, 0u, queueSize);
            }
            nodeCount += queueSize;
        }
        assert(nodeCount < Config::MAX_VISIBLE_NODES);
    }
    {
        OPTICK_EVENT("buildDrawCommands - process nodes: Materials")

        const auto parseRange = [this](RenderBin::SortedQueue& queue, const U32 start, const U32 end) {
            for (U32 i = start; i < end; ++i) {
                auto& [rComp, dataIdx] = queue[i];
                dataIdx._materialIDX = processVisibleNodeMaterial(rComp);
            }
        };

        for (RenderBin::SortedQueue& queue : _sortedQueues) {
            const U32 queueSize = to_U32(queue.size());
            if (queueSize > g_nodesPerPrepareDrawPartition) {
                const U32 midPoint = queueSize / 2;
                Start(*CreateTask(updateTask, [&queue, &parseRange, midPoint](const Task&) {
                    parseRange(queue, 0u, midPoint);
                }), pool);
                Start(*CreateTask(updateTask, [&queue, &parseRange, midPoint, queueSize](const Task&) {
                    parseRange(queue, midPoint, queueSize);
                }), pool);
            } else {
                parseRange(queue, 0u, queueSize);
            }
        }
    }
    {
        OPTICK_EVENT("buildDrawCommands - process nodes: Waiting for tasks to finish")
        StartAndWait(*updateTask, pool);
    }

    const U32 cmdOffset = (cmdBuffer->queueWriteIndex() * cmdBuffer->getPrimitiveCount()) + bufferData._commandElementOffset;
    const RenderPassType prevType = stagePass._passType;
    const auto retrieveCommands = [&]() {
        for (RenderBin::SortedQueue& queue : _sortedQueues) {
            for (auto& [rComp, dataIdx] : queue) {
                Attorney::RenderingCompRenderPass::retrieveDrawCommands(*rComp, stagePass, cmdOffset, dataIdx, _drawCommands);
            }
        }
    };

    if (doPrePass) {
        OPTICK_EVENT("buildDrawCommands - retrieve draw commands: PRE_PASS")
        stagePass._passType = RenderPassType::PRE_PASS;
        retrieveCommands();
    }
    if (doMainPass) {
        OPTICK_EVENT("buildDrawCommands - retrieve draw commands: MAIN_PASS")
        stagePass._passType = RenderPassType::MAIN_PASS;
        retrieveCommands();
    }
    if (doOITPass) {
        OPTICK_EVENT("buildDrawCommands - retrieve draw commands: OIT_PASS")
        stagePass._passType = RenderPassType::OIT_PASS;
        retrieveCommands();
    }
    
    stagePass._passType = prevType;

    *bufferData._lastCommandCount = to_U32(_drawCommands.size());

    {
        OPTICK_EVENT("buildDrawCommands - update buffers");

        cmdBuffer->writeData(bufferData._commandElementOffset, *bufferData._lastCommandCount, _drawCommands.data());

        {
            ScopedLock<Mutex> w_lock(s_globalTransformBufferLock);
            PerRingEntryTransformData& transformData = _nodeTransformData[s_materialDataBufferIndex];
            s_globalTransformBuffer->writeData(bufferData._transformElementOffset, *bufferData._lastNodeCount, transformData.data());
        }

        if (_matUpdateRange.range() > 0u) {
            ScopedLock<Mutex> w_lock(s_globalMaterialBufferLock);
            PerRingEntryMaterialData& materialData = s_materialData[s_materialDataBufferIndex];
            s_globalMaterialBuffer->writeData(_matUpdateRange._firstIDX, _matUpdateRange.range(), &materialData._nodeMaterialData[_matUpdateRange._firstIDX]);
        }
        _matUpdateRange.reset();
    }

    ShaderBufferBinding cmdBufferBinding{};
    cmdBufferBinding._elementRange = { 0u, cmdBuffer->getPrimitiveCount() };
    cmdBufferBinding._buffer = cmdBuffer;
    cmdBufferBinding._binding = ShaderBufferLocation::CMD_BUFFER;

    ShaderBufferBinding transformBufferBinding{};
    transformBufferBinding._elementRange = { bufferData._transformElementOffset, Config::MAX_VISIBLE_NODES };
    transformBufferBinding._buffer = s_globalTransformBuffer;
    transformBufferBinding._binding = ShaderBufferLocation::NODE_TRANSFORM_DATA;

    ShaderBufferBinding materialBufferBinding{};
    materialBufferBinding._elementRange = { 0u, Config::MAX_CONCURRENT_MATERIALS };
    materialBufferBinding._buffer = s_globalMaterialBuffer;
    materialBufferBinding._binding = ShaderBufferLocation::NODE_MATERIAL_DATA;

    DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
    set._buffers.add(cmdBufferBinding);
    set._buffers.add(transformBufferBinding);
    set._buffers.add(materialBufferBinding);

    if (_uniqueTextureAddresses.size() > 0u) {
        GFX::SetTexturesResidencyCommand residencyCmd = {};
        residencyCmd._addresses = _uniqueTextureAddresses;
        residencyCmd._state = true;
        GFX::EnqueueCommand(bufferInOut, residencyCmd);
    }

    return queueTotalSize;
}

U16 RenderPassExecutor::prepareNodeData(VisibleNodeList<>& nodes, const RenderPassParams& params, const bool hasInvalidNodes, const bool doPrePass, const bool doOITPass, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    if (hasInvalidNodes) {
        VisibleNodeList<> tempNodes{};
        for (size_t i = 0; i < nodes.size(); ++i) {
            const VisibleNode& node = nodes.node(i);
            if (node._materialReady) {
                tempNodes.append(node);
            }
        }
        nodes.reset();
        nodes.append(tempNodes);
    }

    const RenderStagePass& stagePass = params._stagePass;
    const SceneRenderState& sceneRenderState = _parent.parent().sceneManager()->getActiveScene().state()->renderState();
    const Camera& cam = *params._camera;

    _renderQueue.refresh();

    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = to_U32(nodes.size());
    descriptor._cbk = [&](const Task* /*parentTask*/, const U32 start, const U32 end) {
        for (U32 i = start; i < end; ++i) {
            VisibleNode& node = nodes.node(i);
            assert(node._materialReady);
            RenderingComponent * rComp = node._node->get<RenderingComponent>();
            Attorney::RenderingCompRenderPass::prepareDrawPackage(*rComp, cam, sceneRenderState, stagePass, true);
            _renderQueue.addNodeToQueue(node._node, stagePass, node._distanceToCameraSq);
        }
    };

    if (descriptor._iterCount < g_nodesPerPrepareDrawPartition) {
        descriptor._cbk(nullptr, 0, descriptor._iterCount);
    } else {
        descriptor._partitionSize = g_nodesPerPrepareDrawPartition;
        descriptor._priority = TaskPriority::DONT_CARE;
        descriptor._useCurrentThread = true;
        parallel_for(_parent.parent().platformContext(), descriptor);
    }

    _renderQueue.sort(stagePass);

    _renderQueuePackages.resize(0);
    _renderQueuePackages.reserve(Config::MAX_VISIBLE_NODES);

    // Draw everything in the depth pass but only draw stuff from the translucent bin in the OIT Pass and everything else in the colour pass
    _renderQueue.populateRenderQueues(stagePass, std::make_pair(RenderBinType::COUNT, true), _renderQueuePackages);

    return buildDrawCommands(params, doPrePass, doOITPass, bufferInOut);
}

void RenderPassExecutor::prepareVisibleNode(const VisibleNode& node,
                                            const RenderBinType targetBin,
                                            const RenderStagePass& stagePass,
                                            const SceneRenderState& sceneRenderState,
                                            const Camera& cam) {
    assert(node._materialReady);

    SceneGraphNode* sgn = node._node;
    if (sgn->getNode().renderState().drawState(stagePass)) {
        Attorney::RenderingCompRenderPass::prepareDrawPackage(*sgn->get<RenderingComponent>(), cam, sceneRenderState, stagePass, false);
        _renderQueue.addNodeToQueue(sgn, stagePass, node._distanceToCameraSq, targetBin);
    }
}

void RenderPassExecutor::prepareRenderQueues(const RenderPassParams& params, const VisibleNodeList<>& nodes, bool transparencyPass, const RenderingOrder renderOrder) {
    OPTICK_EVENT();

    const RenderStagePass& stagePass = params._stagePass;
    const RenderBinType targetBin = transparencyPass ? RenderBinType::TRANSLUCENT : RenderBinType::COUNT;
    const SceneRenderState& sceneRenderState = _parent.parent().sceneManager()->getActiveScene().state()->renderState();
    const Camera& cam = *params._camera;

    _renderQueue.refresh(targetBin);

    const U32 nodeCount = to_U32(nodes.size());
    if (nodeCount > g_nodesPerPrepareDrawPartition * 2) {
        OPTICK_EVENT("prepareRenderQueues - parallel gather");
        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = nodeCount;
        descriptor._partitionSize = g_nodesPerPrepareDrawPartition;
        descriptor._priority = TaskPriority::DONT_CARE;
        descriptor._useCurrentThread = true;
        descriptor._cbk = [&](const Task* /*parentTask*/, const U32 start, const U32 end) {
            for (U32 i = start; i < end; ++i) {
                prepareVisibleNode(nodes.node(i), targetBin, stagePass, sceneRenderState, cam);
            }
        };

        parallel_for(_parent.parent().platformContext(), descriptor);
    } else {
        OPTICK_EVENT("prepareRenderQueues - serial gather");

        for (U32 i = 0u; i < nodeCount; ++i) {
            prepareVisibleNode(nodes.node(i), targetBin, stagePass, sceneRenderState, cam);
        }
    }

    // Sort all bins
    _renderQueue.sort(stagePass, targetBin, renderOrder);

    _renderQueuePackages.resize(0);
    _renderQueuePackages.reserve(Config::MAX_VISIBLE_NODES);

    // Draw everything in the depth pass but only draw stuff from the translucent bin in the OIT Pass and everything else in the colour pass
    _renderQueue.populateRenderQueues(stagePass, stagePass.isDepthPass()
                                                   ? std::make_pair(RenderBinType::COUNT, true)
                                                   : std::make_pair(RenderBinType::TRANSLUCENT, transparencyPass),
                                      _renderQueuePackages);

    
    for (RenderBin::SortedQueue& sQueue : _sortedQueues) {
        sQueue.resize(0);
        sQueue.reserve(Config::MAX_VISIBLE_NODES);
    }

    static const vector<RenderBinType> allBins{};
    static const vector<RenderBinType> prePassBins{
         RenderBinType::OPAQUE,
         RenderBinType::IMPOSTOR,
         RenderBinType::TERRAIN,
         RenderBinType::TERRAIN_AUX,
         RenderBinType::SKY,
         RenderBinType::TRANSLUCENT
    };

    _renderQueue.getSortedQueues(stagePass._passType == RenderPassType::PRE_PASS ? prePassBins : allBins, _sortedQueues);
}

void RenderPassExecutor::prePass(const VisibleNodeList<>& nodes, const RenderPassParams& params, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    assert(params._stagePass._passType == RenderPassType::PRE_PASS);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - PrePass" });

    prepareRenderQueues(params, nodes, false);

    const bool layeredRendering = params._layerParams._layer > 0;

    GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    renderPassCmd->_name = "DO_PRE_PASS";
    renderPassCmd->_target = params._target;
    renderPassCmd->_descriptor = params._targetDescriptorPrePass;

    if (layeredRendering) {
        GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
    }

    renderQueueToSubPasses(bufferInOut);

    postRender(params._stagePass, *params._camera, _renderQueue, bufferInOut);

    if (layeredRendering) {
        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderSubPassCommand{});
    }

    GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::occlusionPass(const VisibleNodeList<>& nodes,
                                       [[maybe_unused]] const U32 visibleNodeCount,
                                       const RenderStagePass& stagePass,
                                       const Camera& camera,
                                       const RenderTargetID& sourceDepthBuffer,
                                       const RenderTargetID& targetDepthBuffer,
                                       GFX::CommandBuffer& bufferInOut) const {
    OPTICK_EVENT();

    //ToDo: Find a way to skip occlusion culling for low number of nodes in view but also keep light culling up and running -Ionut
    assert(stagePass._passType == RenderPassType::PRE_PASS);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "HiZ Construct & Cull" });

    // Update HiZ Target
    const auto [hizTexture, hizSampler] = _context.constructHIZ(sourceDepthBuffer, targetDepthBuffer, bufferInOut);

    // ToDo: This should not be needed as we unbind the render target before we dispatch the compute task anyway. See if we can remove this -Ionut
    GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{
        to_base(MemoryBarrierType::RENDER_TARGET) |
        to_base(MemoryBarrierType::TEXTURE_FETCH) |
        to_base(MemoryBarrierType::TEXTURE_BARRIER)
    });

    // Run occlusion culling CS
    RenderPass::BufferData bufferData = _parent.getPassForStage(_stage).getBufferData(stagePass);

    GFX::SendPushConstantsCommand HIZPushConstantsCMDInOut = {};
    _context.occlusionCull(stagePass, bufferData, hizTexture, hizSampler, HIZPushConstantsCMDInOut, bufferInOut);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Per-node HiZ Cull" });
    const size_t nodeCount = nodes.size();
    for (size_t i = 0; i < nodeCount; ++i) {
        const VisibleNode& node = nodes.node(i);
        Attorney::SceneGraphNodeRenderPassManager::occlusionCullNode(node._node, stagePass, hizTexture, camera, HIZPushConstantsCMDInOut, bufferInOut);
    }
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    // Occlusion culling barrier
    GFX::EnqueueCommand(bufferInOut, GFX::MemoryBarrierCommand{
        to_base(MemoryBarrierType::COMMAND_BUFFER) | //For rendering
        to_base(MemoryBarrierType::SHADER_STORAGE) | //For updating later on
        (bufferData._cullCounterBuffer != nullptr ? to_base(MemoryBarrierType::ATOMIC_COUNTER) : 0u)
    });

    if (bufferData._cullCounterBuffer != nullptr) {
        _context.updateCullCount(bufferData, bufferInOut);

        bufferData._cullCounterBuffer->incQueue();

        GFX::ClearBufferDataCommand clearAtomicCounter{};
        clearAtomicCounter._buffer = bufferData._cullCounterBuffer;
        clearAtomicCounter._offsetElementCount = 0;
        clearAtomicCounter._elementCount = 1;
        GFX::EnqueueCommand(bufferInOut, clearAtomicCounter);
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::mainPass(const VisibleNodeList<>& nodes, const RenderPassParams& params, RenderTarget& target, const bool prePassExecuted, const bool hasHiZ, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    const RenderStagePass& stagePass = params._stagePass;
    assert(stagePass._passType == RenderPassType::MAIN_PASS);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - MainPass" });

    prepareRenderQueues(params, nodes, false);

    if (params._target._usage != RenderTargetUsage::COUNT) {
        LightPool& activeLightPool = Attorney::SceneManagerRenderPass::lightPool(_parent.parent().sceneManager());

        Texture_ptr hizTex = nullptr;
        size_t hizSampler = 0;
        if (hasHiZ) {
            const RenderTarget& hizTarget = _context.renderTargetPool().renderTarget(params._targetHIZ);
            const auto& hizAtt = hizTarget.getAttachment(RTAttachmentType::Depth, 0);
            hizTex = hizAtt.texture();
            hizSampler = hizAtt.samplerHash();
        }

        const RenderTarget& nonMSTarget = _context.renderTargetPool().renderTarget(RenderTargetUsage::SCREEN);

        _context.getRenderer().preRender(stagePass, hizTex, hizSampler, activeLightPool, params._camera, bufferInOut);

        if (hasHiZ) {
            GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ hizTex->data(), hizSampler, TextureUsage::DEPTH });
        } else if (prePassExecuted) {
            if (params._target._usage == RenderTargetUsage::SCREEN_MS) {
                const auto& depthAtt = nonMSTarget.getAttachment(RTAttachmentType::Depth, 0);
                GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ depthAtt.texture()->data(), depthAtt.samplerHash(), TextureUsage::DEPTH });
            } else {
                const auto& depthAtt = target.getAttachment(RTAttachmentType::Depth, 0);
                GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ depthAtt.texture()->data(), depthAtt.samplerHash(), TextureUsage::DEPTH });
            }
        }

        const bool layeredRendering = params._layerParams._layer > 0;

        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_name = "DO_MAIN_PASS";
        renderPassCmd->_target = params._target;
        renderPassCmd->_descriptor = params._targetDescriptorMainPass;

        if (layeredRendering) {
            GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
        }

        // We try and render translucent items in the shadow pass and due some alpha-discard tricks
        renderQueueToSubPasses(bufferInOut);

        postRender(params._stagePass, *params._camera, _renderQueue, bufferInOut);

        if (layeredRendering) {
            GFX::EnqueueCommand(bufferInOut, GFX::EndRenderSubPassCommand{});
        }

        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::woitPass(const VisibleNodeList<>& nodes, const RenderPassParams& params, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    const bool isMSAATarget = params._targetOIT._usage == RenderTargetUsage::OIT_MS;

    assert(params._stagePass._passType == RenderPassType::OIT_PASS);

    prepareRenderQueues(params, nodes, true);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - W-OIT Pass" });

    if (renderQueueSize() > 0) {
        GFX::ClearRenderTargetCommand clearRTCmd{};
        clearRTCmd._target = params._targetOIT;
        if_constexpr(Config::USE_COLOURED_WOIT) {
            // Don't clear our screen target. That would be BAD.
            clearRTCmd._descriptor.clearColour(to_U8(GFXDevice::ScreenTargets::MODULATE), false);
        }
        // Don't clear and don't write to depth buffer
        clearRTCmd._descriptor.clearDepth(false);
        GFX::EnqueueCommand(bufferInOut, clearRTCmd);

        // Step1: Draw translucent items into the accumulation and revealage buffers
        GFX::BeginRenderPassCommand* beginRenderPassOitCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        beginRenderPassOitCmd->_name = "DO_OIT_PASS_1";
        beginRenderPassOitCmd->_target = params._targetOIT;
        beginRenderPassOitCmd->_descriptor.drawMask().setEnabled(RTAttachmentType::Depth, 0, false);
        //beginRenderPassOitCmd->_descriptor.alphaToCoverage(true);

        {
            GFX::SetBlendStateCommand* setBlendStateCmd = GFX::EnqueueCommand<GFX::SetBlendStateCommand>(bufferInOut);
            RTBlendState& state0 = setBlendStateCmd->_blendStates[to_U8(GFXDevice::ScreenTargets::ACCUMULATION)];
            state0._blendProperties._enabled = true;
            state0._blendProperties._blendSrc = BlendProperty::ONE;
            state0._blendProperties._blendDest = BlendProperty::ONE;
            state0._blendProperties._blendOp = BlendOperation::ADD;

            RTBlendState& state1 = setBlendStateCmd->_blendStates[to_U8(GFXDevice::ScreenTargets::REVEALAGE)];
            state1._blendProperties._enabled = true;
            state1._blendProperties._blendSrc = BlendProperty::ZERO;
            state1._blendProperties._blendDest = BlendProperty::INV_SRC_COLOR;
            state1._blendProperties._blendOp = BlendOperation::ADD;

            RTBlendState& state2 = setBlendStateCmd->_blendStates[to_U8(GFXDevice::ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES)];
            state2._blendProperties._enabled = true; 
            state2._blendProperties._blendOp = BlendOperation::MAX;
            state2._blendProperties._blendOpAlpha = BlendOperation::MAX;

            RTBlendState& state3 = setBlendStateCmd->_blendStates[to_U8(GFXDevice::ScreenTargets::SPECULAR)];
            state3._blendProperties._enabled = true;
            state3._blendProperties._blendOp = BlendOperation::MAX;
            state3._blendProperties._blendOpAlpha = BlendOperation::MAX;

            if_constexpr(Config::USE_COLOURED_WOIT) {
                RTBlendState& state4 = setBlendStateCmd->_blendStates[to_U8(GFXDevice::ScreenTargets::MODULATE)];
                state4._blendProperties._enabled = true;
                state4._blendProperties._blendSrc = BlendProperty::ONE;
                state4._blendProperties._blendDest = BlendProperty::ONE;
                state4._blendProperties._blendOp = BlendOperation::ADD;
            }
        }

        {
            const RenderTarget& nonMSTarget = _context.renderTargetPool().renderTarget(RenderTargetUsage::SCREEN);
            const auto& colourAtt = nonMSTarget.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
            GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set._textureData.add(TextureEntry{ colourAtt.texture()->data(), colourAtt.samplerHash(), TextureUsage::POST_FX_DATA });
        }

        renderQueueToSubPasses(bufferInOut/*, quality*/);

        postRender(params._stagePass, *params._camera, _renderQueue, bufferInOut);

        // Reset blend states
        GFX::EnqueueCommand(bufferInOut, GFX::SetBlendStateCommand{});

        // We're gonna do a new bind soon enough
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut)->_setDefaultRTState = isMSAATarget;

        const bool useMSAA = params._target == RenderTargetUsage::SCREEN_MS;

        // Step2: Composition pass
        // Don't clear depth & colours and do not write to the depth buffer
        GFX::EnqueueCommand(bufferInOut, GFX::SetCameraCommand{ Camera::utilityCamera(Camera::UtilityCamera::_2D)->snapshot() });

        const bool layeredRendering = params._layerParams._layer > 0;
        GFX::BeginRenderPassCommand* beginRenderPassCompCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        beginRenderPassCompCmd->_name = "DO_OIT_PASS_2";
        beginRenderPassCompCmd->_target = params._target;
        beginRenderPassCompCmd->_descriptor = params._targetDescriptorComposition;

        if (layeredRendering) {
            GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
        }

        {
            GFX::SetBlendStateCommand* setBlendStateCmd = GFX::EnqueueCommand<GFX::SetBlendStateCommand>(bufferInOut);
            RTBlendState& state0 = setBlendStateCmd->_blendStates[to_U8(GFXDevice::ScreenTargets::ALBEDO)];
            state0._blendProperties._enabled = true;
            state0._blendProperties._blendOp = BlendOperation::ADD;
            if_constexpr(Config::USE_COLOURED_WOIT) {
                state0._blendProperties._blendSrc = BlendProperty::INV_SRC_ALPHA;
                state0._blendProperties._blendDest = BlendProperty::ONE;
            } else {
                state0._blendProperties._blendSrc = BlendProperty::SRC_ALPHA;
                state0._blendProperties._blendDest = BlendProperty::INV_SRC_ALPHA;
            }
        }

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ useMSAA ? s_OITCompositionMSPipeline : s_OITCompositionPipeline });

        RenderTarget& oitRT = _context.renderTargetPool().renderTarget(params._targetOIT);
        const auto& accumAtt = oitRT.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ACCUMULATION));
        const auto& revAtt = oitRT.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::REVEALAGE));

        const TextureData accum = accumAtt.texture()->data();
        const TextureData revealage = revAtt.texture()->data();

        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
        set._textureData.add(TextureEntry{ accum,     accumAtt.samplerHash(), to_base(TextureUsage::UNIT0) });
        set._textureData.add(TextureEntry{ revealage, revAtt.samplerHash(),   to_base(TextureUsage::UNIT1) });
        
        GFX::EnqueueCommand(bufferInOut, GFX::DrawCommand{ GenericDrawCommand{} })->_drawCommands.front()._primitiveType = PrimitiveType::TRIANGLES;

        // Reset blend states
        GFX::EnqueueCommand(bufferInOut, GFX::SetBlendStateCommand{});

        if (layeredRendering) {
            GFX::EnqueueCommand(bufferInOut, GFX::EndRenderSubPassCommand{});
        }

        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::transparencyPass(const VisibleNodeList<>& nodes, const RenderPassParams& params, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    if (_stage == RenderStage::SHADOW) {
        return;
    }

    if (params._stagePass._passType == RenderPassType::OIT_PASS) {
        woitPass(nodes, params, bufferInOut);
    } else {
        assert(params._stagePass._passType == RenderPassType::MAIN_PASS);

        //Grab all transparent geometry
        prepareRenderQueues(params, nodes, true, RenderingOrder::BACK_TO_FRONT);

        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - Transparency Pass" });

        if (renderQueueSize() > 0) {
            const bool layeredRendering = params._layerParams._layer > 0;

            GFX::BeginRenderPassCommand* beginRenderPassTransparentCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
            beginRenderPassTransparentCmd->_name = "DO_TRANSPARENCY_PASS";
            beginRenderPassTransparentCmd->_target = params._target;
            beginRenderPassTransparentCmd->_descriptor.drawMask().setEnabled(RTAttachmentType::Depth, 0, false);
            

            if (layeredRendering) {
                GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
            }

            RTBlendState& state0 = GFX::EnqueueCommand<GFX::SetBlendStateCommand>(bufferInOut)->_blendStates[to_U8(GFXDevice::ScreenTargets::ALBEDO)];
            state0._blendProperties._enabled = true;
            state0._blendProperties._blendSrc = BlendProperty::SRC_ALPHA;
            state0._blendProperties._blendDest = BlendProperty::INV_SRC_ALPHA;
            state0._blendProperties._blendOp = BlendOperation::ADD;

            renderQueueToSubPasses(bufferInOut/*, quality*/);

            postRender(params._stagePass, *params._camera, _renderQueue, bufferInOut);

            // Reset blend states
            GFX::EnqueueCommand(bufferInOut, GFX::SetBlendStateCommand{});

            if (layeredRendering) {
                GFX::EnqueueCommand(bufferInOut, GFX::EndRenderSubPassCommand{});
            }

            GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});

            resolveMainScreenTarget(params, bufferInOut);
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
}

void RenderPassExecutor::postRender(const RenderStagePass& stagePass,
                                    const Camera& camera,
                                    RenderQueue& renderQueue,
                                    GFX::CommandBuffer& bufferInOut) const {
    OPTICK_EVENT();
    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = Util::StringFormat("Post Render pass for stage [ %s ]", TypeUtil::RenderStageToString(stagePass._stage));

    SceneManager* sceneManager = _parent.parent().sceneManager();
    const SceneRenderState& activeSceneRenderState = Attorney::SceneManagerRenderPass::renderState(sceneManager);
    renderQueue.postRender(activeSceneRenderState, stagePass, bufferInOut);

    if (stagePass._stage == RenderStage::DISPLAY && stagePass._passType == RenderPassType::MAIN_PASS) {
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Debug Draw";
        /// These should be OIT rendered as well since things like debug nav meshes have translucency
        Attorney::SceneManagerRenderPass::debugDraw(sceneManager, stagePass, &camera, bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::resolveMainScreenTarget(const RenderPassParams& params, GFX::CommandBuffer& bufferInOut) const {
    OPTICK_EVENT();

    // If we rendered to the multisampled screen target, we can now copy the colour to our regular buffer as we are done with it at this point
    if (params._target._usage == RenderTargetUsage::SCREEN_MS) {
        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - Resolve Screen Targets" });

        const RenderTarget& MSSource = _context.renderTargetPool().renderTarget(params._target);
        const auto& albedoAtt = MSSource.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));
        const auto& velocityAtt = MSSource.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::VELOCITY));
        const auto& normalsAtt = MSSource.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES));
        const auto& specularAtt = MSSource.getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::SPECULAR));

        const TextureData albedoTex = albedoAtt.texture()->data();
        const TextureData velocityTex = velocityAtt.texture()->data();
        const TextureData normalsTex = normalsAtt.texture()->data();
        const TextureData specularTex = specularAtt.texture()->data();

        GFX::BeginRenderPassCommand* beginRenderPassCommand = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        beginRenderPassCommand->_target = { RenderTargetUsage::SCREEN, 0u };
        beginRenderPassCommand->_descriptor.drawMask().setEnabled(RTAttachmentType::Depth, 0, false);
        beginRenderPassCommand->_name = "RESOLVE_MAIN_PASS";

        GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ s_ResolveScreenTargetsPipeline });

        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
        set._textureData.add(TextureEntry{ albedoTex, albedoAtt.samplerHash(), to_base(TextureUsage::UNIT0) });
        set._textureData.add(TextureEntry{ velocityTex, velocityAtt.samplerHash(), to_base(TextureUsage::NORMALMAP) });
        set._textureData.add(TextureEntry{ normalsTex, normalsAtt.samplerHash(), to_base(TextureUsage::HEIGHTMAP) });
        set._textureData.add(TextureEntry{ specularTex, specularAtt.samplerHash(), to_base(TextureUsage::OPACITY) });

        GFX::EnqueueCommand(bufferInOut, GFX::DrawCommand{ GenericDrawCommand{} })->_drawCommands.front()._primitiveType = PrimitiveType::TRIANGLES;

        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
}

void RenderPassExecutor::doCustomPass(RenderPassParams params, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    assert(params._stagePass._stage == _stage);

    params._camera->updateLookAt();
    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName =
        Util::StringFormat("Custom pass ( %s - %s )", TypeUtil::RenderStageToString(_stage), params._passName.empty() ? "N/A" : params._passName.c_str());

    // Tell the Rendering API to draw from our desired PoV
    GFX::SetCameraCommand* camCmd = GFX::EnqueueCommand<GFX::SetCameraCommand>(bufferInOut);
    camCmd->_cameraSnapshot = params._camera->snapshot();

    const bool layeredRendering = params._layerParams._layer > 0;
    if (!layeredRendering) {
        Attorney::SceneManagerRenderPass::prepareLightData(_parent.parent().sceneManager(), _stage, camCmd->_cameraSnapshot._eye, camCmd->_cameraSnapshot._viewMatrix);
    }

    GFX::EnqueueCommand(bufferInOut, GFX::SetClipPlanesCommand{ params._clippingPlanes });

    RenderTarget& target = _context.renderTargetPool().renderTarget(params._target);

    // Cull the scene and grab the visible nodes
    I64 ignoreGUID = params._sourceNode == nullptr ? -1 : params._sourceNode->getGUID();

    NodeCullParams cullParams = {};
    Attorney::SceneManagerRenderPass::initDefaultCullValues(_parent.parent().sceneManager(), _stage, cullParams);

    cullParams._clippingPlanes = params._clippingPlanes;
    cullParams._stage = _stage;
    cullParams._minExtents = params._minExtents;
    cullParams._ignoredGUIDS = { &ignoreGUID, 1 };
    cullParams._currentCamera = params._camera;
    cullParams._cullMaxDistanceSq = std::min(cullParams._cullMaxDistanceSq, SQUARED(params._camera->getZPlanes().y));
    cullParams._maxLoD = params._maxLoD;

    U16 cullFlags = to_base(CullOptions::DEFAULT_CULL_OPTIONS);
    if (!BitCompare(params._drawMask, to_U8(1 << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES)))) {
        cullFlags |= to_base(CullOptions::CULL_DYNAMIC_NODES);
    }
    if (!BitCompare(params._drawMask, to_U8(1 << to_base(RenderPassParams::Flags::DRAW_STATIC_NODES)))) {
        cullFlags |= to_base(CullOptions::CULL_STATIC_NODES);
    }

    VisibleNodeList<>& visibleNodes = Attorney::SceneManagerRenderPass::cullScene(_parent.parent().sceneManager(), cullParams, cullFlags);

    if (params._feedBackContainer != nullptr) {
        auto& container = params._feedBackContainer->_visibleNodes;
        container.resize(visibleNodes.size());
        std::memcpy(container.data(), visibleNodes.data(), visibleNodes.size() * sizeof(VisibleNode));
    }

    constexpr bool doMainPass = true;
    // PrePass requires a depth buffer
    const bool doPrePass = _stage != RenderStage::SHADOW &&
                           params._target._usage != RenderTargetUsage::COUNT &&
                           target.getAttachment(RTAttachmentType::Depth, 0).used();
    const bool doOITPass = params._targetOIT._usage != RenderTargetUsage::COUNT;
    const bool doOcclusionPass = doPrePass && params._targetHIZ._usage != RenderTargetUsage::COUNT;
    bool hasInvalidNodes = false;
    {
        OPTICK_EVENT("doCustomPass: Validate draw")
        const auto ValidateNodesForStagePass = [&visibleNodes, &hasInvalidNodes](const RenderStagePass& stagePass) {
            const I32 nodeCount = to_I32(visibleNodes.size());
            for (I32 i = nodeCount - 1; i >= 0; i--) {
                VisibleNode& node = visibleNodes.node(i);
                if (node._materialReady && !Attorney::SceneGraphNodeRenderPassManager::canDraw(node._node, stagePass)) {
                    node._materialReady = false;
                    hasInvalidNodes = true;
                }
            }
        };

        if (doPrePass) {
            params._stagePass._passType = RenderPassType::PRE_PASS;
            ValidateNodesForStagePass(params._stagePass);
        }
        if (doMainPass) {
            params._stagePass._passType = RenderPassType::MAIN_PASS;
            ValidateNodesForStagePass(params._stagePass);
        }
        if (doOITPass) {
            params._stagePass._passType = RenderPassType::OIT_PASS;
            ValidateNodesForStagePass(params._stagePass);
        }
    }

    // We prepare all nodes for the MAIN_PASS rendering. PRE_PASS and OIT_PASS are support passes only. Their order and sorting are less important.
    params._stagePass._passType = RenderPassType::MAIN_PASS;
    const U32 visibleNodeCount = prepareNodeData(visibleNodes, params, hasInvalidNodes, doPrePass, doOITPass, bufferInOut);

#   pragma region PRE_PASS
    // We need the pass to be PRE_PASS even if we skip the prePass draw stage as it is the default state subsequent operations expect
    params._stagePass._passType = RenderPassType::PRE_PASS;
    if (doPrePass) {
        prePass(visibleNodes, params, bufferInOut);
    }
#   pragma endregion

    RenderTargetID sourceID = params._target;
    if (params._target._usage == RenderTargetUsage::SCREEN_MS) {
        // If we rendered to the multisampled screen target, we can now blit the depth buffer to our resolved target
        GFX::BlitRenderTargetCommand* blitCmd = GFX::EnqueueCommand<GFX::BlitRenderTargetCommand>(bufferInOut);
        blitCmd->_source = sourceID;
        blitCmd->_destination = { RenderTargetUsage::SCREEN, params._target._index };
        blitCmd->_blitDepth._inputLayer = 0u;
        blitCmd->_blitDepth._outputLayer = 0u;

        sourceID = blitCmd->_destination;
    }

#   pragma region HI_Z
    if (doOcclusionPass) {
        // This also renders into our HiZ texture that we may want to use later in PostFX
        occlusionPass(visibleNodes, visibleNodeCount, params._stagePass, *params._camera, sourceID, params._targetHIZ, bufferInOut);
    }
#   pragma endregion

#   pragma region MAIN_PASS
    // Same as for PRE_PASS. Subsequent operations expect a certain state
    params._stagePass._passType = RenderPassType::MAIN_PASS;
    if (doMainPass) {
        mainPass(visibleNodes, params, target, doPrePass, doOcclusionPass, bufferInOut);
    } else {
        DIVIDE_UNEXPECTED_CALL();
    }
#   pragma endregion

#   pragma region TRANSPARENCY_PASS
    // If doIOTPass is false, use forward pass shaders (i.e. MAIN_PASS again for transparents)
    if (doOITPass) {
        params._stagePass._passType = RenderPassType::OIT_PASS;
    }
    transparencyPass(visibleNodes, params, bufferInOut);
#   pragma endregion

    resolveMainScreenTarget(params, bufferInOut);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

    _matUpdateRange.reset();
    ScopedLock<Mutex> w_lock(s_globalMaterialBufferLock);
    PerRingEntryMaterialData& materialData = s_materialData[s_materialDataBufferIndex];
    // Increment material lifetime by 1 (a frame has passed)
    for (auto& [_, lifetime] : materialData._nodeMaterialLookupInfo) {
        ++lifetime;
    }
}

U32 RenderPassExecutor::renderQueueSize(const RenderPackage::MinQuality qualityRequirement) const {
    if (qualityRequirement == RenderPackage::MinQuality::COUNT) {
        return to_U32(_renderQueuePackages.size());
    }

    U32 size = 0;
    for (const RenderPackage* item : _renderQueuePackages) {
        if (item->qualityRequirement() == qualityRequirement) {
            ++size;
        }
    }

    return size;
}

void RenderPassExecutor::renderQueueToSubPasses(GFX::CommandBuffer& commandsInOut, const RenderPackage::MinQuality qualityRequirement) const {
    OPTICK_EVENT();

    eastl::fixed_vector<GFX::CommandBuffer*, Config::MAX_VISIBLE_NODES, true, eastl::dvd_allocator> buffers = {};

    if (qualityRequirement == RenderPackage::MinQuality::COUNT) {
        for (RenderPackage* item : _renderQueuePackages) {
            buffers.push_back(Attorney::RenderPackageRenderPassExecutor::getCommandBuffer(item));
        }
    } else {
        for (RenderPackage* item : _renderQueuePackages) {
            if (item->qualityRequirement() == qualityRequirement) {
                buffers.push_back(Attorney::RenderPackageRenderPassExecutor::getCommandBuffer(item));
            }
        }
    }

    commandsInOut.add(buffers.data(), buffers.size());
}


void RenderPassExecutor::PostRender() {
    { //Material data
        ScopedLock<Mutex> w_lock(s_globalMaterialBufferLock);
        s_globalMaterialBuffer->incQueue();
        s_materialDataBufferIndex = s_globalMaterialBuffer->queueWriteIndex();
    }
    { //Transform Data
        ScopedLock<Mutex> w_lock(s_globalTransformBufferLock);
        s_globalTransformBuffer->incQueue();
        s_transformDataBufferIndex = s_globalTransformBuffer->queueWriteIndex();
    }
}

} //namespace Divide