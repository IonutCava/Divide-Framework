#include "stdafx.h"

#include "Headers/RenderPassExecutor.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/EngineTaskPool.h"

#include "Editor/Headers/Editor.h"

#include "Graphs/Headers/SceneNode.h"

#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RTAttachment.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include "Rendering/Headers/Renderer.h"
#include "Rendering/RenderPass/Headers/RenderQueue.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "Scenes/Headers/SceneState.h"

#include "ECS/Components/Headers/AnimationComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"


namespace Divide {
namespace {
    template<typename T>
    struct BufferCandidate {
        T _index = std::numeric_limits<T>::max();
        T _framesSinceLastUsed = std::numeric_limits<T>::max();
    };

    // Remove materials that haven't been indexed in this amount of frames to make space for new ones
    constexpr U16 g_maxMaterialFrameLifetime = 6u;
    constexpr U16 g_maxIndirectionFrameLifetime = 6u;
    // Use to partition parallel jobs
    constexpr U32 g_nodesPerPrepareDrawPartition = 16u;

    template<typename DataContainer>
    using ExecutorBuffer = RenderPassExecutor::ExecutorBuffer<DataContainer>;
    using BufferUpdateRange = RenderPassExecutor::BufferUpdateRange;

    FORCE_INLINE bool operator==(const BufferUpdateRange& lhs, const BufferUpdateRange& rhs) noexcept {
        return lhs._firstIDX == rhs._firstIDX && lhs._lastIDX == rhs._lastIDX;
    }

    FORCE_INLINE bool operator!=(const BufferUpdateRange& lhs, const BufferUpdateRange& rhs) noexcept {
        return lhs._firstIDX != rhs._firstIDX || lhs._lastIDX != rhs._lastIDX;
    }

    FORCE_INLINE bool Contains(const BufferUpdateRange& lhs, const BufferUpdateRange& rhs) noexcept {
        return lhs._firstIDX <= rhs._firstIDX && lhs._lastIDX >= rhs._lastIDX;
    }

    FORCE_INLINE BufferUpdateRange GetPrevRangeDiff(const BufferUpdateRange& crtRange, const BufferUpdateRange& prevRange) noexcept {
        if (crtRange.range() == 0u) {
            return prevRange;
        }

        BufferUpdateRange ret;
        // We only care about the case where the previous range is not fully contained by the current one
        if (prevRange.range() > 0u && prevRange != crtRange && !Contains(crtRange, prevRange)) {
            if (prevRange._firstIDX < crtRange._firstIDX && prevRange._lastIDX > crtRange._lastIDX) {
                ret = prevRange;
            } else if (prevRange._firstIDX < crtRange._firstIDX) {
                ret._firstIDX = prevRange._firstIDX;
                ret._lastIDX = crtRange._firstIDX;
            } else if (prevRange._lastIDX > crtRange._lastIDX) {
                ret._firstIDX = crtRange._lastIDX;
                ret._lastIDX = prevRange._lastIDX;
            } else {
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        return ret;
    }

    template<typename DataContainer>
    FORCE_INLINE U32 GetPreviousIndex(ExecutorBuffer<DataContainer>& executorBuffer, U32 idx) {
        if (idx == 0) {
            idx = executorBuffer._gpuBuffer->queueLength();
        }

        return (idx - 1) % executorBuffer._gpuBuffer->queueLength();
    };

    template<typename DataContainer>
    FORCE_INLINE U32 GetNextIndex(ExecutorBuffer<DataContainer>& executorBuffer, const U32 idx) {
        return (idx + 1) % executorBuffer._gpuBuffer->queueLength();
    };

    FORCE_INLINE bool MergeBufferUpdateRanges(BufferUpdateRange& target, const BufferUpdateRange& source) noexcept {
        bool ret = false;
        if (target._firstIDX > source._firstIDX) {
            target._firstIDX = source._firstIDX;
            ret = true;
        }
        if (target._lastIDX < source._lastIDX) {
            target._lastIDX = source._lastIDX;
            ret = true;
        }

        return ret;
    };

    template<typename DataContainer>
    FORCE_INLINE void UpdateBufferRangeLocked(ExecutorBuffer<DataContainer>&executorBuffer, const U32 idx) noexcept {
        if (executorBuffer._bufferUpdateRange._firstIDX > idx) {
            executorBuffer._bufferUpdateRange._firstIDX = idx;
        }
        if (executorBuffer._bufferUpdateRange._lastIDX < idx) {
            executorBuffer._bufferUpdateRange._lastIDX = idx;
        }

        executorBuffer._highWaterMark = std::max(executorBuffer._highWaterMark, idx + 1u);
    }

    template<typename DataContainer>
    void UpdateBufferRange(ExecutorBuffer<DataContainer>& executorBuffer, const U32 idx) {
        ScopedLock<Mutex> w_lock(executorBuffer._lock);
        UpdateBufferRangeLocked(executorBuffer, idx);
    }

    template<typename DataContainer>
    void WriteToGPUBufferInternal(ExecutorBuffer<DataContainer>& executorBuffer, BufferUpdateRange target, GFX::MemoryBarrierCommand& memCmdInOut) {
        if (target.range() == 0u) {
            return;
        }

        OPTICK_EVENT();

        const size_t bufferAlignmentRequirement = ShaderBuffer::AlignmentRequirement(executorBuffer._gpuBuffer->getUsage());
        const size_t bufferPrimitiveSize = executorBuffer._gpuBuffer->getPrimitiveSize();
        if (bufferPrimitiveSize < bufferAlignmentRequirement) {
            // We need this due to alignment concerns
            if (target._firstIDX % 2u != 0) {
                target._firstIDX -= 1u;
            }
            if (target._lastIDX % 2u == 0) {
                target._lastIDX += 1u;
            }
        }

        memCmdInOut._bufferLocks.push_back(executorBuffer._gpuBuffer->writeData({ target._firstIDX, target.range() }, &executorBuffer._data._gpuData[target._firstIDX]));
    }

    template<typename DataContainer>
    void WriteToGPUBuffer(ExecutorBuffer<DataContainer>& executorBuffer, GFX::MemoryBarrierCommand& memCmdInOut) {
        BufferUpdateRange writeRange, prevWriteRange;
        {
            ScopedLock<Mutex> r_lock(executorBuffer._lock);

            if (!MergeBufferUpdateRanges(executorBuffer._bufferUpdateRangeHistory.back(), executorBuffer._bufferUpdateRange)) {
                NOP();
            }
            writeRange = executorBuffer._bufferUpdateRange;
            executorBuffer._bufferUpdateRange.reset();

            if_constexpr (RenderPass::DataBufferRingSize > 1u) {
                // We don't need to write everything again as big chunks have been written as part of the normal frame update process
                // Try and find only the items unoutched this frame
                prevWriteRange = GetPrevRangeDiff(executorBuffer._bufferUpdateRangeHistory.back(), executorBuffer._bufferUpdateRangePrev);
                executorBuffer._bufferUpdateRangePrev.reset();
            }
        }

        WriteToGPUBufferInternal(executorBuffer, writeRange, memCmdInOut);
        if_constexpr(RenderPass::DataBufferRingSize > 1u) {
            WriteToGPUBufferInternal(executorBuffer, prevWriteRange, memCmdInOut);
        }
    }

    template<typename DataContainer>
    bool NodeNeedsUpdate(ExecutorBuffer<DataContainer>& executorBuffer, const U32 indirectionIDX) {
        {
            SharedLock<SharedMutex> w_lock(executorBuffer._proccessedLock);
            if (executorBuffer._nodeProcessedThisFrame.find(indirectionIDX) != executorBuffer._nodeProcessedThisFrame.cend()) {
                return false;
            }
        }

        ScopedLock<SharedMutex> w_lock(executorBuffer._proccessedLock);
        // This does a check anyway, so should be safe
        return executorBuffer._nodeProcessedThisFrame.insert(indirectionIDX).second;
    }

    template<typename DataContainer>
    void ExecutorBufferPostRender(ExecutorBuffer<DataContainer>& executorBuffer) {
        OPTICK_EVENT();

        ScopedLock<Mutex> w_lock(executorBuffer._lock);
        const BufferUpdateRange rangeWrittenThisFrame = executorBuffer._bufferUpdateRangeHistory.back();

        // At the end of the frame, bump our history queue by one position and prepare the tail for a new write
        if_constexpr(RenderPass::DataBufferRingSize > 1u) {
            OPTICK_EVENT("History Update");
            for (U8 i = 0u; i < RenderPass::DataBufferRingSize - 1; ++i) {
                executorBuffer._bufferUpdateRangeHistory[i] = executorBuffer._bufferUpdateRangeHistory[i + 1];
            }
            executorBuffer._bufferUpdateRangeHistory[RenderPass::DataBufferRingSize - 1].reset();

            // We can gather all of our history (once we evicted the oldest entry) into our "previous frame written range" entry
            executorBuffer._bufferUpdateRangePrev.reset();
            for (U32 i = 0u; i < executorBuffer._gpuBuffer->queueLength() - 1u; ++i) {
                MergeBufferUpdateRanges(executorBuffer._bufferUpdateRangePrev, executorBuffer._bufferUpdateRangeHistory[i]);
            }
        }
        // We need to increment our buffer queue to get the new write range into focus
        executorBuffer._gpuBuffer->incQueue();
        executorBuffer._nodeProcessedThisFrame.clear();
    }
}

bool RenderPassExecutor::s_globalDataInit = false;
Mutex RenderPassExecutor::s_indirectionGlobalLock;
std::array<bool, RenderPassExecutor::MAX_INDIRECTION_ENTRIES> RenderPassExecutor::s_indirectionFreeList{};
SamplerAddress RenderPassExecutor::s_defaultTextureSamplerAddress = 0u;

Pipeline* RenderPassExecutor::s_OITCompositionPipeline = nullptr;
Pipeline* RenderPassExecutor::s_OITCompositionMSPipeline = nullptr;
Pipeline* RenderPassExecutor::s_ResolveGBufferPipeline = nullptr;

RenderPassExecutor::RenderPassExecutor(RenderPassManager& parent, GFXDevice& context, const RenderStage stage)
    : _parent(parent)
    , _context(context)
    , _stage(stage)
    , _renderQueue(parent.parent(), stage)
{
    const U8 passCount = TotalPassCountForStage(stage);

    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
    bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
    bufferDescriptor._ringBufferLength = RenderPass::DataBufferRingSize;
    bufferDescriptor._bufferParams._elementCount = Config::MAX_VISIBLE_NODES;
    bufferDescriptor._usage = ShaderBuffer::Usage::COMMAND_BUFFER;
    bufferDescriptor._bufferParams._elementSize = sizeof(IndirectDrawCommand);
    const char* stageName = TypeUtil::RenderStageToString(stage);
    for (U8 i = 0u; i < passCount; ++i) {
        bufferDescriptor._name = Util::StringFormat("CMD_DATA_%s_%d", stageName, i);
        _cmdBuffers.emplace_back(MOV(_context.newSB(bufferDescriptor)));
    }
}

ShaderBuffer* RenderPassExecutor::getCommandBufferForStagePass(const RenderStagePass stagePass) {
    const U16 idx = IndexForStage(stagePass);
    return _cmdBuffers[idx].get();
}

void RenderPassExecutor::OnStartup(const GFXDevice& gfx) {
    s_defaultTextureSamplerAddress = Texture::DefaultTexture()->getGPUAddress(0u);
    assert(Uvec2ToTexture(TextureToUVec2(s_defaultTextureSamplerAddress)) == s_defaultTextureSamplerAddress);

    s_indirectionFreeList.fill(true);

    Material::OnStartup(s_defaultTextureSamplerAddress);
}

void RenderPassExecutor::OnShutdown([[maybe_unused]] const GFXDevice& gfx) {
    s_globalDataInit = false;
    s_defaultTextureSamplerAddress = 0u;
    s_OITCompositionPipeline = nullptr;
    s_OITCompositionMSPipeline = nullptr;
    s_ResolveGBufferPipeline = nullptr;

    Material::OnShutdown();
}

void RenderPassExecutor::postInit(const ShaderProgram_ptr& OITCompositionShader,
                                  const ShaderProgram_ptr& OITCompositionShaderMS,
                                  const ShaderProgram_ptr& ResolveGBufferShaderMS) {

    if (!s_globalDataInit) {
        s_globalDataInit = true;

        PipelineDescriptor pipelineDescriptor;
        pipelineDescriptor._stateHash = _context.get2DStateBlock();
        pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;

        pipelineDescriptor._shaderProgramHandle = ResolveGBufferShaderMS->handle();
        s_ResolveGBufferPipeline = _context.newPipeline(pipelineDescriptor);

        BlendingSettings& state0 = pipelineDescriptor._blendStates._settings[to_U8(GFXDevice::ScreenTargets::ALBEDO)];
        state0.enabled(true);
        state0.blendOp(BlendOperation::ADD);
        if_constexpr(Config::USE_COLOURED_WOIT) {
            state0.blendSrc(BlendProperty::INV_SRC_ALPHA);
            state0.blendDest(BlendProperty::ONE);
        } else {
            state0.blendSrc(BlendProperty::SRC_ALPHA);
            state0.blendDest(BlendProperty::INV_SRC_ALPHA);
        }

        pipelineDescriptor._shaderProgramHandle = OITCompositionShader->handle();
        s_OITCompositionPipeline = _context.newPipeline(pipelineDescriptor);

        pipelineDescriptor._shaderProgramHandle = OITCompositionShaderMS->handle();
        s_OITCompositionMSPipeline = _context.newPipeline(pipelineDescriptor);

    }

    _transformBuffer._data._freeList.fill(true);
    _materialBuffer._data._lookupInfo.fill({ INVALID_MAT_HASH, g_invalidMaterialIndex });
    const vec2<U32> defaultSampler = TextureToUVec2(s_defaultTextureSamplerAddress);
    for (NodeMaterialTextures& textures : _texturesBuffer._data._gpuData) {
        for (NodeMaterialTextureAddress& texture : textures) {
            texture = defaultSampler;
        }
    }
    _texturesBuffer._data._lookupInfo.fill({ INVALID_TEX_HASH, g_invalidTexturesIndex });

    ShaderBufferDescriptor bufferDescriptor = {};
    bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
    bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
    bufferDescriptor._ringBufferLength = RenderPass::DataBufferRingSize;
    bufferDescriptor._usage = ShaderBuffer::Usage::UNBOUND_BUFFER;
    {// Node Transform buffer
        bufferDescriptor._bufferParams._elementCount = to_U32(_transformBuffer._data._gpuData.size());
        bufferDescriptor._bufferParams._elementSize = sizeof(NodeTransformData);
        bufferDescriptor._name = Util::StringFormat("NODE_TRANSFORM_DATA_%s", TypeUtil::RenderStageToString(_stage));
        _transformBuffer._gpuBuffer = _context.newSB(bufferDescriptor);
        _transformBuffer._bufferUpdateRangeHistory.resize(bufferDescriptor._ringBufferLength);
    }
    {// Node Material buffer
        bufferDescriptor._bufferParams._elementCount = to_U32(_materialBuffer._data._gpuData.size());
        bufferDescriptor._bufferParams._elementSize = sizeof(NodeMaterialData);
        bufferDescriptor._name = Util::StringFormat("NODE_MATERIAL_DATA_%s", TypeUtil::RenderStageToString(_stage));
        _materialBuffer._gpuBuffer = _context.newSB(bufferDescriptor);
        _materialBuffer._bufferUpdateRangeHistory.resize(bufferDescriptor._ringBufferLength);
    }
    {// Node Textures buffer
        static_assert((sizeof(NodeMaterialTextures) * Config::MAX_VISIBLE_NODES) % 256 == 0u);
        static_assert(sizeof(NodeMaterialTextureAddress) == sizeof(SamplerAddress));

        bufferDescriptor._bufferParams._elementCount = to_U32(_texturesBuffer._data._gpuData.size());
        bufferDescriptor._bufferParams._elementSize = sizeof(NodeMaterialTextures);
        bufferDescriptor._name = Util::StringFormat("NODE_TEXTURE_DATA_%s", TypeUtil::RenderStageToString(_stage));
        _texturesBuffer._gpuBuffer = _context.newSB(bufferDescriptor);
        _texturesBuffer._bufferUpdateRangeHistory.resize(bufferDescriptor._ringBufferLength);
    }
    {// Indirection Buffer
        bufferDescriptor._bufferParams._elementCount = to_U32(_indirectionBuffer._data._gpuData.size());
        bufferDescriptor._bufferParams._elementSize = sizeof(NodeIndirectionData);
        bufferDescriptor._name = Util::StringFormat("NODE_INDIRECTION_DATA_%s", TypeUtil::RenderStageToString(_stage));
        _indirectionBuffer._gpuBuffer = _context.newSB(bufferDescriptor);
        _indirectionBuffer._bufferUpdateRangeHistory.resize(bufferDescriptor._ringBufferLength);
    }
}

void RenderPassExecutor::processVisibleNodeTransform(RenderingComponent* rComp, const D64 interpolationFactor) {
    OPTICK_EVENT();

    const U32 indirectionIDX = Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry(rComp);

    if (!NodeNeedsUpdate(_transformBuffer, indirectionIDX)) {
        return;
    }

    NodeTransformData transformOut;

    const SceneGraphNode* node = rComp->parentSGN();

    { // Transform
        OPTICK_EVENT("Transform query");
        const TransformComponent* const transform = node->get<TransformComponent>();

        // Get the node's world matrix properly interpolated
        transform->getPreviousWorldMatrix(transformOut._prevWorldMatrix);
        transform->getWorldMatrix(interpolationFactor, transformOut._worldMatrix);
        transformOut._normalMatrixW.set(mat3<F32>(transformOut._worldMatrix));

        if (!transform->isUniformScaled()) {
            // Non-uniform scaling requires an inverseTranspose to negatescaling contribution but preserve rotation
            transformOut._normalMatrixW.inverseTranspose();
        }
    }

    U8 boneCount = 0u;
    U8 frameTicked = 0u;
    { //Animation
        if (node->HasComponents(ComponentType::ANIMATION)) {
            const AnimationComponent* animComp = node->get<AnimationComponent>();
            boneCount = animComp->boneCount();
            if (animComp->playAnimations() && animComp->frameTicked()) {
                frameTicked = 1u;
            }
        }
    }
    { //Misc
        transformOut._normalMatrixW.setRow(3, node->get<BoundsComponent>()->getBoundingSphere().asVec4());

        transformOut._normalMatrixW.element(0,3) = to_F32(Util::PACK_UNORM4x8(
            0u,
            frameTicked,
            rComp->getLoDLevel(_stage),
            rComp->occlusionCull() ? 1u : 0u
        ));

        U8 selectionFlag = 0u;
        // We don't propagate selection flags to children outside of the editor, so check for that
        if (node->hasFlag(SceneGraphNode::Flags::SELECTED) ||
            node->parent() && node->parent()->hasFlag(SceneGraphNode::Flags::SELECTED)) {
            selectionFlag = 2u;
        } else if (node->hasFlag(SceneGraphNode::Flags::HOVERED)) {
            selectionFlag = 1u;
        }
        transformOut._normalMatrixW.element(1, 3) = to_F32(selectionFlag);
        transformOut._normalMatrixW.element(2, 3) = to_F32(boneCount);
    }
    {
        OPTICK_EVENT("Buffer idx update");
        U32 transformIdx = U32_MAX;
        {
            ScopedLock<Mutex> w_lock(_transformBuffer._lock);
            for (U32 idx = 0u; idx < Config::MAX_VISIBLE_NODES; ++idx) {
                if (_transformBuffer._data._freeList[idx]) {
                    _transformBuffer._data._freeList[idx] = false;
                    transformIdx = idx;
                    break;
                }
            }
            DIVIDE_ASSERT(transformIdx != U32_MAX);
        }
        _transformBuffer._data._gpuData[transformIdx] = transformOut;
        UpdateBufferRangeLocked(_transformBuffer, transformIdx);

        if (_indirectionBuffer._data._gpuData[indirectionIDX][TRANSFORM_IDX] != transformIdx || transformIdx == 0u) {
            _indirectionBuffer._data._gpuData[indirectionIDX][TRANSFORM_IDX] = transformIdx;
            UpdateBufferRange(_indirectionBuffer, indirectionIDX);
        }
    }
}

U32 RenderPassExecutor::processVisibleNodeTextures(RenderingComponent* rComp, bool& cacheHit) {
    OPTICK_EVENT();

    cacheHit = false;

    NodeMaterialTextures tempData{};
    rComp->getMaterialTextures(tempData, s_defaultTextureSamplerAddress);
    {
        ScopedLock<Mutex> w_lock(_texturesBuffer._lock);
        for (const NodeMaterialTextureAddress address : tempData) {
            _uniqueTextureAddresses.insert(Uvec2ToTexture(address));
        }
    }
    const size_t texturesHash = HashTexturesData(tempData);

    const auto findTexturesMatch = [](const size_t targetHash, BufferTexturesData::LookupInfoContainer& data) -> U32 {
        const U32 count = to_U32(data.size());
        for (U32 idx = 0u; idx < count; ++idx) {
            const auto [hash, _] = data[idx];
            if (hash == targetHash) {
                return idx;
            }
        }

        return g_invalidTexturesIndex;
    };


    ScopedLock<Mutex> w_lock(_texturesBuffer._lock);
    BufferTexturesData::LookupInfoContainer& infoContainer = _texturesBuffer._data._lookupInfo;
    {// Try and match an existing texture blob
        OPTICK_EVENT("processVisibleNode - try match textures");
        const U32 idx = findTexturesMatch(texturesHash, infoContainer);
        if (idx != g_invalidTexturesIndex) {
            infoContainer[idx]._framesSinceLastUsed = 0u;
            cacheHit = true;
            UpdateBufferRangeLocked(_texturesBuffer, idx);
            return idx;
        }
    }

    // If we fail, try and find an empty slot and update it
    OPTICK_EVENT("processVisibleNode - process unmatched textures");
    // No match found (cache miss) so add a new entry.
    BufferCandidate<U32> bestCandidate = { g_invalidTexturesIndex, 0u };

    const U32 count = to_U32(infoContainer.size());
    for (U32 idx = 0u; idx < count; ++idx) {
        const auto [hash, framesSinceLastUsed] = infoContainer[idx];
        // Two cases here. We either have empty slots (e.g. startup, cache clear, etc) ...
        if (hash == INVALID_TEX_HASH) {
            // ... in which case our current idx is what we are looking for ...
            bestCandidate._index = idx;
            bestCandidate._framesSinceLastUsed = g_maxMaterialFrameLifetime;
            break;
        }
        // ... else we need to find a slot with a stale entry (but not one that is still in flight!)
        if (framesSinceLastUsed >= std::max(to_U32(g_maxMaterialFrameLifetime), bestCandidate._framesSinceLastUsed)) {
            bestCandidate._index = idx;
            bestCandidate._framesSinceLastUsed = framesSinceLastUsed;
            // Keep going and see if we can find an even older entry
        }
    }

    assert(bestCandidate._index != g_invalidTexturesIndex);

    infoContainer[bestCandidate._index] = { texturesHash, 0u };
    assert(bestCandidate._index < _texturesBuffer._data._gpuData.size());
    _texturesBuffer._data._gpuData[bestCandidate._index] = tempData;
    UpdateBufferRangeLocked(_texturesBuffer, bestCandidate._index);

    return bestCandidate._index;
}

void RenderPassExecutor::parseTextureRange(RenderBin::SortedQueue& queue, const U32 start, const U32 end) {
    for (U32 i = start; i < end; ++i) {

        const U32 indirectionIDX = Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry(queue[i]);
        if (!NodeNeedsUpdate(_texturesBuffer, indirectionIDX)) {
            continue;
        }

        [[maybe_unused]] bool cacheHit = false;
        const U32 idx = processVisibleNodeTextures(queue[i], cacheHit);
        DIVIDE_ASSERT(idx != g_invalidTexturesIndex && idx != U32_MAX);

        // We are already protected by the atomic boolean for this entry
        if (_indirectionBuffer._data._gpuData[indirectionIDX][TEXTURES_IDX] != idx || idx == 0u) {
            _indirectionBuffer._data._gpuData[indirectionIDX][TEXTURES_IDX] = idx;
            UpdateBufferRange(_indirectionBuffer, indirectionIDX);
        }
    }
}

U16 RenderPassExecutor::processVisibleNodeMaterial(RenderingComponent* rComp, bool& cacheHit) {
    OPTICK_EVENT();

    cacheHit = false;

    NodeMaterialData tempData{};
    // Get the colour matrix (base colour, metallic, etc)
    rComp->getMaterialData(tempData);

    // Match materials
    const size_t materialHash = HashMaterialData(tempData);

    const auto findMaterialMatch = [](const size_t targetHash, BufferMaterialData::LookupInfoContainer& data) -> U16 {
        const U16 count = to_U16(data.size());
        for (U16 idx = 0u; idx < count; ++idx) {
            const auto [hash, _] = data[idx];
            if (hash == targetHash) {
                return idx;
            }
        }

        return g_invalidMaterialIndex;
    };

    ScopedLock<Mutex> w_lock(_materialBuffer._lock);
    BufferMaterialData::LookupInfoContainer& infoContainer = _materialBuffer._data._lookupInfo;
    {// Try and match an existing material
        OPTICK_EVENT("processVisibleNode - try match material");
        const U16 idx = findMaterialMatch(materialHash, infoContainer);
        if (idx != g_invalidMaterialIndex) {
            infoContainer[idx]._framesSinceLastUsed = 0u;
            cacheHit = true;
            UpdateBufferRangeLocked(_materialBuffer, idx);
            return idx;
        }
    }

    // If we fail, try and find an empty slot and update it
    OPTICK_EVENT("processVisibleNode - process unmatched material");
    // No match found (cache miss) so add a new entry.
    BufferCandidate<U16> bestCandidate{ g_invalidMaterialIndex, 0u };

    const U16 count = to_U16(infoContainer.size());
    for (U16 idx = 0u; idx < count; ++idx) {
        const auto [hash, framesSinceLastUsed] = infoContainer[idx];
        // Two cases here. We either have empty slots (e.g. startup, cache clear, etc) ...
        if (hash == INVALID_MAT_HASH) {
            // ... in which case our current idx is what we are looking for ...
            bestCandidate._index = idx;
            bestCandidate._framesSinceLastUsed = g_maxMaterialFrameLifetime;
            break;
        }
        // ... else we need to find a slot with a stale entry (but not one that is still in flight!)
        if (framesSinceLastUsed >= std::max(g_maxMaterialFrameLifetime, bestCandidate._framesSinceLastUsed)) {
            bestCandidate._index = idx;
            bestCandidate._framesSinceLastUsed = framesSinceLastUsed;
            // Keep going and see if we can find an even older entry
        }
    }

    DIVIDE_ASSERT(bestCandidate._index != g_invalidMaterialIndex, "RenderPassExecutor::processVisibleNode error: too many concurrent materials! Increase Config::MAX_CONCURRENT_MATERIALS");

    infoContainer[bestCandidate._index] = { materialHash, 0u };
    assert(bestCandidate._index < _materialBuffer._data._gpuData.size());

    _materialBuffer._data._gpuData[bestCandidate._index] = tempData;
    UpdateBufferRangeLocked(_materialBuffer, bestCandidate._index);

    return bestCandidate._index;
}


void RenderPassExecutor::parseMaterialRange(RenderBin::SortedQueue& queue, const U32 start, const U32 end) {
    for (U32 i = start; i < end; ++i) {

        const U32 indirectionIDX = Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry(queue[i]);
        if (!NodeNeedsUpdate(_materialBuffer, indirectionIDX)) {
            continue;
        }

        [[maybe_unused]] bool cacheHit = false;
        const U16 idx = processVisibleNodeMaterial(queue[i], cacheHit);
        DIVIDE_ASSERT(idx != g_invalidMaterialIndex && idx != U32_MAX);

        if (_indirectionBuffer._data._gpuData[indirectionIDX][MATERIAL_IDX] != idx || idx == 0u) {
            _indirectionBuffer._data._gpuData[indirectionIDX][MATERIAL_IDX] = idx;
            UpdateBufferRange(_indirectionBuffer, indirectionIDX);
        }
    }
}

U16 RenderPassExecutor::buildDrawCommands(const RenderPassParams& params, const bool doPrePass, const bool doOITPass, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    OPTICK_EVENT();

    constexpr bool doMainPass = true;

    RenderStagePass stagePass = params._stagePass;
    RenderPass::BufferData bufferData = _parent.getPassForStage(_stage).getBufferData(stagePass);

    _drawCommands.clear();
    _uniqueTextureAddresses.clear();
    _uniqueTextureAddresses.insert(s_defaultTextureSamplerAddress);

    for (RenderBin::SortedQueue& sQueue : _sortedQueues) {
        sQueue.resize(0);
        sQueue.reserve(Config::MAX_VISIBLE_NODES);
    }

    const U16 queueTotalSize = _renderQueue.getSortedQueues({}, _sortedQueues);

    //Erase nodes with no draw commands
    for (RenderBin::SortedQueue& queue : _sortedQueues) {
        erase_if(queue, [](RenderingComponent* item) noexcept {
            return !Attorney::RenderingCompRenderPass::hasDrawCommands(*item);
        });
    }

    TaskPool& pool = _context.context().taskPool(TaskPoolType::HIGH_PRIORITY);
    Task* updateTask = CreateTask(TASK_NOP);
    {
        OPTICK_EVENT("buildDrawCommands - process nodes: Transforms")
        U32& nodeCount = *bufferData._lastNodeCount;
        nodeCount = 0u;
        for (RenderBin::SortedQueue& queue : _sortedQueues) {
            const U32 queueSize = to_U32(queue.size());
            if (queueSize > g_nodesPerPrepareDrawPartition) {
                Start(*CreateTask(updateTask, [this, &queue, queueSize](const Task&) {
                    const D64 interpFactor = GFXDevice::FrameInterpolationFactor();
                    for (U32 i = 0u; i < queueSize / 2; ++i) {
                        processVisibleNodeTransform(queue[i], interpFactor);
                    }
                }), pool); 
                Start(*CreateTask(updateTask, [this, &queue, queueSize](const Task&) {
                    const D64 interpFactor = GFXDevice::FrameInterpolationFactor();
                    for (U32 i = queueSize / 2; i < queueSize; ++i) {
                        processVisibleNodeTransform(queue[i], interpFactor);
                    }
                }), pool);
            } else {
                const D64 interpFactor = GFXDevice::FrameInterpolationFactor();
                for (U32 i = 0u; i < queueSize; ++i) {
                    processVisibleNodeTransform(queue[i], interpFactor);
                }
            }
            nodeCount += queueSize;
        }
        assert(nodeCount < Config::MAX_VISIBLE_NODES);
    }
    {
        OPTICK_EVENT("buildDrawCommands - process nodes: Textures")
        for (RenderBin::SortedQueue& queue : _sortedQueues) {
            const U32 queueSize = to_U32(queue.size());
            if (queueSize > g_nodesPerPrepareDrawPartition) {
                const U32 midPoint = queueSize / 2;
                Start(*CreateTask(updateTask, [this, &queue, midPoint](const Task&) {
                    parseTextureRange(queue, 0u, midPoint);
                }), pool); 
                Start(*CreateTask(updateTask, [this, &queue, midPoint, queueSize](const Task&) {
                    parseTextureRange(queue, midPoint, queueSize);
                }), pool);
            } else {
                parseTextureRange(queue, 0u, queueSize);
            }
        }
    }
    {
        OPTICK_EVENT("buildDrawCommands - process nodes: Materials")
        for (RenderBin::SortedQueue& queue : _sortedQueues) {
            const U32 queueSize = to_U32(queue.size());
            if (queueSize > g_nodesPerPrepareDrawPartition) {
                const U32 midPoint = queueSize / 2;
                Start(*CreateTask(updateTask, [this, &queue, midPoint](const Task&) {
                    parseMaterialRange(queue, 0u, midPoint);
                }), pool);
                Start(*CreateTask(updateTask, [this, &queue, midPoint, queueSize](const Task&) {
                    parseMaterialRange(queue, midPoint, queueSize);
                }), pool);
            } else {
                parseMaterialRange(queue, 0u, queueSize);
            }
        }
    }
    {
        OPTICK_EVENT("buildDrawCommands - process nodes: Waiting for tasks to finish")
        StartAndWait(*updateTask, pool);
    }

    ShaderBuffer* cmdBuffer = getCommandBufferForStagePass(stagePass);
    cmdBuffer->incQueue();
    const U32 elementOffset = cmdBuffer->queueWriteIndex() * cmdBuffer->getPrimitiveCount();

    const auto retrieveCommands = [&]() {
        for (RenderBin::SortedQueue& queue : _sortedQueues) {
            for (RenderingComponent* rComp : queue) {
                Attorney::RenderingCompRenderPass::retrieveDrawCommands(*rComp, stagePass, elementOffset, _drawCommands);
            }
        }
    };

    {
        const RenderPassType prevType = stagePass._passType;
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
        } else {
            OPTICK_EVENT("buildDrawCommands - retrieve draw commands: TRANSPARENCY_PASS")
            stagePass._passType = RenderPassType::TRANSPARENCY_PASS;
            retrieveCommands();
        }
        stagePass._passType = prevType;
    }

    const U32 cmdCount = to_U32(_drawCommands.size());
    *bufferData._lastCommandCount = cmdCount;

    if (cmdCount > 0u) {
        OPTICK_EVENT("buildDrawCommands - update command buffer");
        memCmdInOut._bufferLocks.push_back(cmdBuffer->writeData({ 0u, cmdCount }, _drawCommands.data()));
    }
    {
        OPTICK_EVENT("buildDrawCommands - update material buffer");
        WriteToGPUBuffer(_materialBuffer, memCmdInOut);
    }
    {
        OPTICK_EVENT("buildDrawCommands - update transform buffer");
        WriteToGPUBuffer(_transformBuffer, memCmdInOut);
    }
    {
        OPTICK_EVENT("buildDrawCommands - update texture buffer");
        WriteToGPUBuffer(_texturesBuffer, memCmdInOut);
    }
    {
        OPTICK_EVENT("buildDrawCommands - update indirection buffer");
        WriteToGPUBuffer(_indirectionBuffer, memCmdInOut);
    }

    DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
    set._usage = DescriptorSetUsage::PER_BATCH_SET;
    {
        auto& binding = set._bindings.emplace_back();
        binding._resourceSlot = to_base(ShaderBufferLocation::CMD_BUFFER);
        binding._type = DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::NONE;
        binding._data.As<ShaderBufferEntry>() = { cmdBuffer,  { 0u, cmdBuffer->getPrimitiveCount() } };
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._resourceSlot = to_base(ShaderBufferLocation::GPU_COMMANDS);
        binding._type = DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::COMPUTE;
        binding._data.As<ShaderBufferEntry>() = { cmdBuffer, { 0u, cmdBuffer->getPrimitiveCount() } };
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._resourceSlot = to_base(ShaderBufferLocation::NODE_MATERIAL_DATA);
        binding._type = DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
        binding._data.As<ShaderBufferEntry>() = { _materialBuffer._gpuBuffer.get(), { 0u, _materialBuffer._highWaterMark } };
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._resourceSlot = to_base(ShaderBufferLocation::NODE_TRANSFORM_DATA);
        binding._type = DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::ALL_GEOMETRY;
        binding._data.As<ShaderBufferEntry>() = { _transformBuffer._gpuBuffer.get(), { 0u, _transformBuffer._highWaterMark } };
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._resourceSlot = to_base(ShaderBufferLocation::NODE_INDIRECTION_DATA);
        binding._type = DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::VERTEX;
        binding._data.As<ShaderBufferEntry>() = { _indirectionBuffer._gpuBuffer.get(), { 0u, _indirectionBuffer._highWaterMark } };
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._resourceSlot = to_base(ShaderBufferLocation::NODE_TEXTURE_DATA);
        binding._type = DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::ALL_DRAW;
        binding._data.As<ShaderBufferEntry>() = { _texturesBuffer._gpuBuffer.get(), { 0u, _texturesBuffer._highWaterMark } };

    }

    if (!_uniqueTextureAddresses.empty()) {
        GFX::SetTexturesResidencyCommand residencyCmd{};
        residencyCmd._makeResident = true;

        size_t texIdx = 0u;
        const size_t capacity = residencyCmd._addresses.size();
        for (const SamplerAddress& address : _uniqueTextureAddresses) {
            residencyCmd._addresses[texIdx++] = address;
            if (texIdx == capacity) {
                GFX::EnqueueCommand(bufferInOut, residencyCmd);
                texIdx = 0u;
            }
        }
        if (texIdx > 0u) {
            GFX::EnqueueCommand(bufferInOut, residencyCmd);
        }
    }

    return queueTotalSize;
}

U16 RenderPassExecutor::prepareNodeData(VisibleNodeList<>& nodes,
                                        const RenderPassParams& params,
                                        const CameraSnapshot& cameraSnapshot,
                                        const bool hasInvalidNodes,
                                        const bool doPrePass,
                                        const bool doOITPass,
                                        GFX::CommandBuffer& bufferInOut,
                                        GFX::MemoryBarrierCommand& memCmdInOut)
{
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

    RenderStagePass stagePass = params._stagePass;
    const SceneRenderState& sceneRenderState = _parent.parent().sceneManager()->getActiveScene().state()->renderState();
    {
        _renderQueue.refresh();
        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = to_U32(nodes.size());
        descriptor._cbk = [&](const Task* /*parentTask*/, const U32 start, const U32 end) {
            for (U32 i = start; i < end; ++i) {
                const VisibleNode& node = nodes.node(i);
                assert(node._materialReady);
                RenderingComponent * rComp = node._node->get<RenderingComponent>();
                if (Attorney::RenderingCompRenderPass::prepareDrawPackage(*rComp, cameraSnapshot, sceneRenderState, stagePass, true)) {
                    _renderQueue.addNodeToQueue(node._node, stagePass, node._distanceToCameraSq);
                }
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
    }

    _renderQueuePackages.resize(0);

    RenderQueue::PopulateQueueParams queueParams{};
    queueParams._stagePass = stagePass;
    queueParams._binType = RenderBinType::COUNT;
    queueParams._filterByBinType = false;
    _renderQueue.populateRenderQueues(queueParams, _renderQueuePackages);

    return buildDrawCommands(params, doPrePass, doOITPass, bufferInOut, memCmdInOut);
}

void RenderPassExecutor::prepareRenderQueues(const RenderPassParams& params, 
                                             const VisibleNodeList<>& nodes,
                                             const CameraSnapshot& cameraSnapshot,
                                             bool transparencyPass,
                                             const RenderingOrder renderOrder,
                                             GFX::CommandBuffer& bufferInOut)
{
    OPTICK_EVENT();

    RenderStagePass stagePass = params._stagePass;
    const RenderBinType targetBin = transparencyPass ? RenderBinType::TRANSLUCENT : RenderBinType::COUNT;
    const SceneRenderState& sceneRenderState = _parent.parent().sceneManager()->getActiveScene().state()->renderState();
 
    _renderQueue.refresh(targetBin);

    const U32 nodeCount = to_U32(nodes.size());
    ParallelForDescriptor descriptor = {};
    descriptor._cbk = [&](const Task* /*parentTask*/, const U32 start, const U32 end) {
        for (U32 i = start; i < end; ++i) {
            const VisibleNode& node = nodes.node(i);
            SceneGraphNode* sgn = node._node;
            if (sgn->getNode().renderState().drawState(stagePass)) {
                if (Attorney::RenderingCompRenderPass::prepareDrawPackage(*sgn->get<RenderingComponent>(), cameraSnapshot, sceneRenderState, stagePass, false)) {
                    _renderQueue.addNodeToQueue(sgn, stagePass, node._distanceToCameraSq, targetBin);
                }
            }
        }
    };

    if (nodeCount > g_nodesPerPrepareDrawPartition * 2) {
        OPTICK_EVENT("prepareRenderQueues - parallel gather");
        descriptor._iterCount = nodeCount;
        descriptor._partitionSize = g_nodesPerPrepareDrawPartition;
        descriptor._priority = TaskPriority::DONT_CARE;
        descriptor._useCurrentThread = true;
        parallel_for(_parent.parent().platformContext(), descriptor);
    } else {
        OPTICK_EVENT("prepareRenderQueues - serial gather");
        descriptor._cbk(nullptr, 0u, nodeCount);
    }

    // Sort all bins
    _renderQueue.sort(stagePass, targetBin, renderOrder);

    _renderQueuePackages.resize(0);
    _renderQueuePackages.reserve(Config::MAX_VISIBLE_NODES);

    // Draw everything in the depth pass but only draw stuff from the translucent bin in the OIT Pass and everything else in the colour pass

    RenderQueue::PopulateQueueParams queueParams{};
    queueParams._stagePass = stagePass;
    if (IsDepthPass(stagePass)) {
        queueParams._binType = RenderBinType::COUNT;
        queueParams._filterByBinType = false;
    } else {
        queueParams._binType = RenderBinType::TRANSLUCENT;
        queueParams._filterByBinType = !transparencyPass;
    }

    _renderQueue.populateRenderQueues(queueParams, _renderQueuePackages);


    for (const auto&[rComp, pkg] : _renderQueuePackages) {
        Attorney::RenderingCompRenderPassExecutor::getCommandBuffer(rComp, pkg, bufferInOut);
    }

    if (params._stagePass._passType != RenderPassType::PRE_PASS) {
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = Util::StringFormat("Post Render pass for stage [ %s ]", TypeUtil::RenderStageToString(stagePass._stage), to_U32(stagePass._stage));

        _renderQueue.postRender(Attorney::SceneManagerRenderPass::renderState(_parent.parent().sceneManager()),
                                params._stagePass,
                                bufferInOut);

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
}

void RenderPassExecutor::prePass(const VisibleNodeList<>& nodes, const RenderPassParams& params, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    assert(params._stagePass._passType == RenderPassType::PRE_PASS);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - PrePass" });

    const bool layeredRendering = params._layerParams._layer > 0;

    GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    renderPassCmd->_name = "DO_PRE_PASS";
    renderPassCmd->_target = params._target;
    renderPassCmd->_descriptor = params._targetDescriptorPrePass;

    if (layeredRendering) {
        GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
    }

    prepareRenderQueues(params, nodes, cameraSnapshot, false, RenderingOrder::COUNT, bufferInOut);

    if (layeredRendering) {
        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderSubPassCommand{});
    }

    GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::occlusionPass(const PlayerIndex idx, 
                                       const VisibleNodeList<>& nodes,
                                       const CameraSnapshot& cameraSnapshot,
                                       [[maybe_unused]] const U32 visibleNodeCount,
                                       RenderStagePass stagePass,
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

    _context.occlusionCull(bufferData,
                           hizTexture,
                           hizSampler,
                           cameraSnapshot,
                           _stage == RenderStage::DISPLAY,
                           bufferInOut);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::mainPass(const VisibleNodeList<>& nodes, const RenderPassParams& params, const CameraSnapshot& cameraSnapshot, RenderTarget& target, const bool prePassExecuted, const bool hasHiZ, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    assert(params._stagePass._passType == RenderPassType::MAIN_PASS);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - MainPass" });

    if (params._target != INVALID_RENDER_TARGET_ID) {
        const bool layeredRendering = params._layerParams._layer > 0;

        GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        renderPassCmd->_name = "DO_MAIN_PASS";
        renderPassCmd->_target = params._target;
        renderPassCmd->_descriptor = params._targetDescriptorMainPass;

        if (layeredRendering) {
            GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
        }

        const RenderTarget* screenTarget = _context.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN);
        RTAttachment* normalsAtt = screenTarget->getAttachment(RTAttachmentType::Colour, to_base(GFXDevice::ScreenTargets::NORMALS));

        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
        set._usage = DescriptorSetUsage::PER_PASS_SET;

        Texture_ptr hizTex = nullptr;
        if (hasHiZ) {
            const RenderTarget* hizTarget = _context.renderTargetPool().getRenderTarget(params._targetHIZ);
            RTAttachment* hizAtt = hizTarget->getAttachment(RTAttachmentType::Depth_Stencil, 0);

            auto& binding = set._bindings.emplace_back();
            binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
            binding._resourceSlot = to_base(TextureUsage::DEPTH);
            binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
            binding._data.As<DescriptorCombinedImageSampler>() = { hizAtt->texture()->data(), hizAtt->descriptor()._samplerHash };
        } else if (prePassExecuted) {
            RTAttachment* depthAtt = target.getAttachment(RTAttachmentType::Depth_Stencil, 0);
            auto& binding = set._bindings.emplace_back();
            binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
            binding._resourceSlot = to_base(TextureUsage::DEPTH);
            binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
            binding._data.As<DescriptorCombinedImageSampler>() = { depthAtt->texture()->data(), depthAtt->descriptor()._samplerHash };
        }

        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_base(TextureUsage::SCENE_NORMALS);
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
        binding._data.As<DescriptorCombinedImageSampler>() = { normalsAtt->texture()->data(), normalsAtt->descriptor()._samplerHash };

        prepareRenderQueues(params, nodes, cameraSnapshot, false, RenderingOrder::COUNT, bufferInOut);

        if (layeredRendering) {
            GFX::EnqueueCommand(bufferInOut, GFX::EndRenderSubPassCommand{});
        }

        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::woitPass(const VisibleNodeList<>& nodes, const RenderPassParams& params, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    assert(params._stagePass._passType == RenderPassType::OIT_PASS);

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - W-OIT Pass" });

    GFX::ClearRenderTargetCommand clearRTCmd{};
    clearRTCmd._target = params._targetOIT;
    if_constexpr(Config::USE_COLOURED_WOIT) {
        // Don't clear our screen target. That would be BAD.
        clearRTCmd._descriptor._clearColourAttachment[to_U8(GFXDevice::ScreenTargets::MODULATE)] = false;
    }
    // Don't clear and don't write to depth buffer
    clearRTCmd._descriptor._clearDepth = false;
    GFX::EnqueueCommand(bufferInOut, clearRTCmd);

    // Step1: Draw translucent items into the accumulation and revealage buffers
    GFX::BeginRenderPassCommand* beginRenderPassOitCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    beginRenderPassOitCmd->_name = "DO_OIT_PASS_1";
    beginRenderPassOitCmd->_target = params._targetOIT;
    SetEnabled(beginRenderPassOitCmd->_descriptor._drawMask, RTAttachmentType::Depth_Stencil, 0, false);
    //beginRenderPassOitCmd->_descriptor._alphaToCoverage = true;
    {
        const RenderTarget* nonMSTarget = _context.renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN);
        const auto& colourAtt = nonMSTarget->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO));

        DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
        set._usage = DescriptorSetUsage::PER_PASS_SET;
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_base(TextureUsage::TRANSMITANCE);
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
        binding._data.As<DescriptorCombinedImageSampler>() = { colourAtt->texture()->data(), colourAtt->descriptor()._samplerHash };
    }

    prepareRenderQueues(params, nodes, cameraSnapshot, true, RenderingOrder::COUNT, bufferInOut);

    // We're gonna do a new bind soon enough
    GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut)->_setDefaultRTState = params._targetOIT == INVALID_RENDER_TARGET_ID;

    const bool useMSAA = params._target == RenderTargetNames::SCREEN_MS;

    // Step2: Composition pass
    // Don't clear depth & colours and do not write to the depth buffer
    GFX::EnqueueCommand(bufferInOut, GFX::SetCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });

    const bool layeredRendering = params._layerParams._layer > 0;
    GFX::BeginRenderPassCommand* beginRenderPassCompCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    beginRenderPassCompCmd->_name = "DO_OIT_PASS_2";
    beginRenderPassCompCmd->_target = params._target;
    beginRenderPassCompCmd->_descriptor = params._targetDescriptorComposition;

    if (layeredRendering) {
        GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
    }

    GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ useMSAA ? s_OITCompositionMSPipeline : s_OITCompositionPipeline });

    RenderTarget* oitRT = _context.renderTargetPool().getRenderTarget(params._targetOIT);
    const auto& accumAtt = oitRT->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ACCUMULATION));
    const auto& revAtt = oitRT->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::REVEALAGE));

    DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
    set._usage = DescriptorSetUsage::PER_DRAW_SET;
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TextureUsage::UNIT0);
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
        binding._data.As<DescriptorCombinedImageSampler>() = { accumAtt->texture()->data(), accumAtt->descriptor()._samplerHash };
    }
    {
        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._resourceSlot = to_U8(TextureUsage::UNIT1);
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
        binding._data.As<DescriptorCombinedImageSampler>() = { revAtt->texture()->data(), revAtt->descriptor()._samplerHash };
    }

    GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);

    if (layeredRendering) {
        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderSubPassCommand{});
    }

    GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::transparencyPass(const VisibleNodeList<>& nodes, const RenderPassParams& params, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    assert(params._stagePass._passType == RenderPassType::TRANSPARENCY_PASS);

    //Grab all transparent geometry
    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - Transparency Pass" });

    const bool layeredRendering = params._layerParams._layer > 0;

    GFX::BeginRenderPassCommand* beginRenderPassTransparentCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
    beginRenderPassTransparentCmd->_name = "DO_TRANSPARENCY_PASS";
    beginRenderPassTransparentCmd->_target = params._target;
    SetEnabled(beginRenderPassTransparentCmd->_descriptor._drawMask, RTAttachmentType::Depth_Stencil, 0, false);

    if (layeredRendering) {
        GFX::EnqueueCommand<GFX::BeginRenderSubPassCommand>(bufferInOut)->_writeLayers.push_back(params._layerParams);
    }

    prepareRenderQueues(params, nodes, cameraSnapshot, true, RenderingOrder::BACK_TO_FRONT, bufferInOut);

    if (layeredRendering) {
        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderSubPassCommand{});
    }

    GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::resolveMainScreenTarget(const RenderPassParams& params,
                                                 const bool resolveDepth,
                                                 const bool resolveGBuffer,
                                                 const bool resolveColourBuffer,
                                                 GFX::CommandBuffer& bufferInOut) const
{
    if (!resolveDepth && !resolveGBuffer && !resolveColourBuffer) {
        return;
    }

    OPTICK_EVENT();

    // If we rendered to the multisampled screen target, we can now copy the colour to our regular buffer as we are done with it at this point
    if (params._target == RenderTargetNames::SCREEN_MS) {
        GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ " - Resolve Screen Targets" });

        if (resolveDepth || resolveColourBuffer) {
            // If we rendered to the multisampled screen target, we can now blit the depth buffer to our resolved target
            GFX::BlitRenderTargetCommand* blitCmd = GFX::EnqueueCommand<GFX::BlitRenderTargetCommand>(bufferInOut);
            blitCmd->_source = RenderTargetNames::SCREEN_MS;
            blitCmd->_destination = RenderTargetNames::SCREEN;
            if (resolveDepth) {
                blitCmd->_blitDepth._inputLayer = 0u;
                blitCmd->_blitDepth._outputLayer = 0u;
            }
            if (resolveColourBuffer) {
                blitCmd->_blitColours[0].set(to_U8(GFXDevice::ScreenTargets::ALBEDO), to_U8(GFXDevice::ScreenTargets::ALBEDO), 0u, 0u);
            }
        }
        if (resolveGBuffer) {
            GFX::BeginRenderPassCommand* beginRenderPassCommand = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
            beginRenderPassCommand->_target = RenderTargetNames::SCREEN;
            SetEnabled(beginRenderPassCommand->_descriptor._drawMask, RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::ALBEDO), false);
            SetEnabled(beginRenderPassCommand->_descriptor._drawMask, RTAttachmentType::Depth_Stencil, 0, false);
            beginRenderPassCommand->_name = "RESOLVE_MAIN_GBUFFER";

            GFX::EnqueueCommand(bufferInOut, GFX::BindPipelineCommand{ s_ResolveGBufferPipeline });

            const RenderTarget* MSSource = _context.renderTargetPool().getRenderTarget(params._target);
            RTAttachment* velocityAtt = MSSource->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::VELOCITY));
            RTAttachment* normalsAtt = MSSource->getAttachment(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::NORMALS));

            DescriptorSet& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(bufferInOut)->_set;
            set._usage = DescriptorSetUsage::PER_DRAW_SET;
            {
                auto& binding = set._bindings.emplace_back();
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                binding._resourceSlot = to_U8(TextureUsage::UNIT0);
                binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
                binding._data.As<DescriptorCombinedImageSampler>() = { velocityAtt->texture()->data(), velocityAtt->descriptor()._samplerHash };
            }
            {
                auto& binding = set._bindings.emplace_back();
                binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                binding._resourceSlot = to_U8(TextureUsage::UNIT1);
                binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
                binding._data.As<DescriptorCombinedImageSampler>() = { normalsAtt->texture()->data(), normalsAtt->descriptor()._samplerHash };
            }

            GFX::EnqueueCommand<GFX::DrawCommand>(bufferInOut);
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>(bufferInOut);

        }
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
    }
}

bool RenderPassExecutor::validateNodesForStagePass(VisibleNodeList<>& nodes, const RenderStagePass stagePass) {
    bool ret = false;
    const I32 nodeCount = to_I32(nodes.size());
    for (I32 i = nodeCount - 1; i >= 0; i--) {
        VisibleNode& node = nodes.node(i);
        if (node._node == nullptr || (node._materialReady && !Attorney::SceneGraphNodeRenderPassManager::canDraw(node._node, stagePass))) {
            node._materialReady = false;
            ret = true;
        }
    }
    return ret;
}

void RenderPassExecutor::doCustomPass(const PlayerIndex playerIdx, Camera* camera, RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    OPTICK_EVENT();
    assert(params._stagePass._stage == _stage);

    if (!camera->updateLookAt()) {
        NOP();
    }

    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName =
        Util::StringFormat("Custom pass ( %s - %s )", TypeUtil::RenderStageToString(_stage), params._passName.empty() ? "N/A" : params._passName.c_str());

    // Tell the Rendering API to draw from our desired PoV
    GFX::SetCameraCommand* camCmd = GFX::EnqueueCommand<GFX::SetCameraCommand>(bufferInOut);
    camCmd->_cameraSnapshot = camera->snapshot();
    const CameraSnapshot& camSnapshot = camCmd->_cameraSnapshot;

    const bool layeredRendering = params._layerParams._layer > 0;
    if (!layeredRendering) {
        Attorney::SceneManagerRenderPass::prepareLightData(_parent.parent().sceneManager(), _stage, camSnapshot, memCmdInOut);
    }

    GFX::EnqueueCommand(bufferInOut, GFX::SetClipPlanesCommand{ params._clippingPlanes });

    RenderTarget* target = _context.renderTargetPool().getRenderTarget(params._target);

    // Cull the scene and grab the visible nodes
    I64 ignoreGUID = params._sourceNode == nullptr ? -1 : params._sourceNode->getGUID();

    NodeCullParams cullParams = {};
    Attorney::SceneManagerRenderPass::initDefaultCullValues(_parent.parent().sceneManager(), _stage, cullParams);

    cullParams._clippingPlanes = params._clippingPlanes;
    cullParams._stage = _stage;
    cullParams._minExtents = params._minExtents;
    cullParams._ignoredGUIDS = { &ignoreGUID, 1 };
    cullParams._cameraEyePos = camSnapshot._eye;
    cullParams._frustum = &camera->getFrustum();
    cullParams._cullMaxDistance = std::min(cullParams._cullMaxDistance, camSnapshot._zPlanes.y);
    cullParams._maxLoD = params._maxLoD;

    U16 cullFlags = to_base(CullOptions::DEFAULT_CULL_OPTIONS);
    if (!BitCompare(params._drawMask, to_U8(1 << to_base(RenderPassParams::Flags::DRAW_DYNAMIC_NODES)))) {
        cullFlags |= to_base(CullOptions::CULL_DYNAMIC_NODES);
    }
    if (!BitCompare(params._drawMask, to_U8(1 << to_base(RenderPassParams::Flags::DRAW_STATIC_NODES)))) {
        cullFlags |= to_base(CullOptions::CULL_STATIC_NODES);
    }
    if (BitCompare(params._drawMask, to_U8(1 << to_base(RenderPassParams::Flags::DRAW_SKY_NODES)))) {
        cullFlags |= to_base(CullOptions::KEEP_SKY_NODES);
    }

    VisibleNodeList<>& visibleNodes = Attorney::SceneManagerRenderPass::cullScene(_parent.parent().sceneManager(), cullParams, cullFlags);

    constexpr bool doMainPass = true;
    // PrePass requires a depth buffer
    const bool doPrePass = _stage != RenderStage::SHADOW &&
                           params._target != INVALID_RENDER_TARGET_ID &&
                           target->usesAttachment(RTAttachmentType::Depth_Stencil, 0);
    const bool doOITPass = params._targetOIT != INVALID_RENDER_TARGET_ID;
    const bool doOcclusionPass = doPrePass && params._targetHIZ != INVALID_RENDER_TARGET_ID;

    bool hasInvalidNodes = false;
    {
        OPTICK_EVENT("doCustomPass: Validate draw")
        if (doPrePass) {
            params._stagePass._passType = RenderPassType::PRE_PASS;
            hasInvalidNodes = validateNodesForStagePass(visibleNodes, params._stagePass) || hasInvalidNodes;
        }
        if (doMainPass) {
            params._stagePass._passType = RenderPassType::MAIN_PASS;
            hasInvalidNodes = validateNodesForStagePass(visibleNodes, params._stagePass) || hasInvalidNodes;
        }
        if (doOITPass) {
            params._stagePass._passType = RenderPassType::OIT_PASS;
            hasInvalidNodes = validateNodesForStagePass(visibleNodes, params._stagePass) || hasInvalidNodes;
        } else {
            params._stagePass._passType = RenderPassType::TRANSPARENCY_PASS;
            hasInvalidNodes = validateNodesForStagePass(visibleNodes, params._stagePass) || hasInvalidNodes;
        }
    }

    if (params._feedBackContainer != nullptr) {
        params._feedBackContainer->resize(visibleNodes.size());
        std::memcpy(params._feedBackContainer->data(), visibleNodes.data(), visibleNodes.size() * sizeof(VisibleNode));
        if (hasInvalidNodes) {
            // This may hurt ... a lot ... -Ionut
            dvd_erase_if(*params._feedBackContainer, [](VisibleNode& node) {
                return node._node == nullptr || !node._materialReady;
            });
        };
    }
    // We prepare all nodes for the MAIN_PASS rendering. PRE_PASS and OIT_PASS are support passes only. Their order and sorting are less important.
    params._stagePass._passType = RenderPassType::MAIN_PASS;
    const U32 visibleNodeCount = prepareNodeData(visibleNodes, params, camSnapshot, hasInvalidNodes, doPrePass, doOITPass, bufferInOut, memCmdInOut);

#   pragma region PRE_PASS
    // We need the pass to be PRE_PASS even if we skip the prePass draw stage as it is the default state subsequent operations expect
    params._stagePass._passType = RenderPassType::PRE_PASS;
    if (doPrePass) {
        prePass(visibleNodes, params, camSnapshot, bufferInOut);
    }
#   pragma endregion

    resolveMainScreenTarget(params, true, true, false, bufferInOut);

#   pragma region HI_Z
    if (doOcclusionPass) {
        // This also renders into our HiZ texture that we may want to use later in PostFX
        occlusionPass(playerIdx, visibleNodes, camSnapshot, visibleNodeCount, params._stagePass, RenderTargetNames::SCREEN, params._targetHIZ, bufferInOut);
    }
#   pragma endregion

#   pragma region LIGHT_PASS
    _context.getRenderer().prepareLighting(_stage, camSnapshot, bufferInOut);
#   pragma endregion

#   pragma region MAIN_PASS
    // Same as for PRE_PASS. Subsequent operations expect a certain state
    params._stagePass._passType = RenderPassType::MAIN_PASS;
    if (_stage == RenderStage::DISPLAY) {
        _context.getRenderer().postFX().prePass(playerIdx, camSnapshot, bufferInOut);
    }
    if (doMainPass) {
        mainPass(visibleNodes, params, camSnapshot, *target, doPrePass, doOcclusionPass, bufferInOut);
    } else {
        DIVIDE_UNEXPECTED_CALL();
    }
#   pragma endregion

#   pragma region TRANSPARENCY_PASS
    if (_stage != RenderStage::SHADOW) {
        // If doIOTPass is false, use forward pass shaders (i.e. MAIN_PASS again for transparents)
        if (doOITPass) {
            params._stagePass._passType = RenderPassType::OIT_PASS;
            woitPass(visibleNodes, params, camSnapshot, bufferInOut);
        } else {
            params._stagePass._passType = RenderPassType::TRANSPARENCY_PASS;
            transparencyPass(visibleNodes, params, camSnapshot, bufferInOut);
        }
    }
#   pragma endregion

    if (_stage == RenderStage::DISPLAY) {
        GFX::EnqueueCommand(bufferInOut, GFX::PushCameraCommand{ camSnapshot });

        GFX::BeginRenderPassCommand* beginRenderPassTransparentCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>(bufferInOut);
        beginRenderPassTransparentCmd->_name = "DO_POST_RENDER_PASS";
        beginRenderPassTransparentCmd->_target = params._target;
        SetEnabled(beginRenderPassTransparentCmd->_descriptor._drawMask, RTAttachmentType::Colour, 1, false);
        SetEnabled(beginRenderPassTransparentCmd->_descriptor._drawMask, RTAttachmentType::Colour, 2, false);
        SetEnabled(beginRenderPassTransparentCmd->_descriptor._drawMask, RTAttachmentType::Depth_Stencil, 0, false);

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(bufferInOut)->_scopeName = "Debug Draw Pass";
        Attorney::SceneManagerRenderPass::debugDraw(_parent.parent().sceneManager(), bufferInOut);
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

        if_constexpr(Config::Build::ENABLE_EDITOR) {
            Attorney::EditorRenderPassExecutor::postRender(_context.context().editor(), camSnapshot, params._target, bufferInOut);
        }
        GFX::EnqueueCommand(bufferInOut, GFX::EndRenderPassCommand{});
        GFX::EnqueueCommand(bufferInOut, GFX::PopCameraCommand{});
    }

    resolveMainScreenTarget(params, false, false, true, bufferInOut);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void RenderPassExecutor::postRender() {
    OPTICK_EVENT();

    ExecutorBufferPostRender(_indirectionBuffer);
    ExecutorBufferPostRender(_materialBuffer);
    ExecutorBufferPostRender(_transformBuffer);
    ExecutorBufferPostRender(_texturesBuffer);
    {
        OPTICK_EVENT("Increment Lifetime");
        for (BufferMaterialData::LookupInfo& it : _materialBuffer._data._lookupInfo) {
            if (it._hash != INVALID_MAT_HASH) {
                ++it._framesSinceLastUsed;
            }
        }
        for (BufferTexturesData::LookupInfo& it : _texturesBuffer._data._lookupInfo) {
            if (it._hash != INVALID_TEX_HASH) {
                ++it._framesSinceLastUsed;
            }
        }
    }
    {
        OPTICK_EVENT("Clear Freelists");
        ScopedLock<Mutex> w_lock(_transformBuffer._lock);
        _transformBuffer._data._freeList.fill(true);
    }

    _materialBuffer._highWaterMark = _transformBuffer._highWaterMark = _texturesBuffer._highWaterMark = 0u;
}

U32 RenderPassExecutor::renderQueueSize() const {
    return to_U32(_renderQueuePackages.size());
}

void RenderPassExecutor::OnRenderingComponentCreation(RenderingComponent* rComp) {
    ScopedLock<Mutex> w_lock(s_indirectionGlobalLock);
    assert(Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry(rComp) == U32_MAX);
    U32 entry = U32_MAX;
    for (U32 i = 0u; i < MAX_INDIRECTION_ENTRIES; ++i) {
        if (s_indirectionFreeList[i]) {
            s_indirectionFreeList[i] = false;
            entry = i;
            break;
        }
    }
    DIVIDE_ASSERT(entry != U32_MAX, "Insufficient space left in indirection buffer. Consider increasing available storage!");
    Attorney::RenderingCompRenderPassExecutor::setIndirectionBufferEntry(rComp, entry);
}

void RenderPassExecutor::OnRenderingComponentDestruction(RenderingComponent* rComp) {
    const U32 entry = Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry(rComp);
    if (entry == U32_MAX) {
        DIVIDE_UNEXPECTED_CALL();
        return;
    }
    ScopedLock<Mutex> w_lock(s_indirectionGlobalLock);
    assert(!s_indirectionFreeList[entry]);
    s_indirectionFreeList[entry] = true;
}

} //namespace Divide