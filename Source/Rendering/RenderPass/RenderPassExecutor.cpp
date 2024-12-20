

#include "Headers/RenderPassExecutor.h"
#include "Headers/RenderQueue.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"

#include "Editor/Headers/Editor.h"

#include "Graphs/Headers/SceneNode.h"

#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/ProjectManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RTAttachment.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "Scenes/Headers/SceneState.h"

#include "ECS/Components/Headers/AnimationComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/SelectionComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"


namespace Divide
{
    namespace
    {
        // Use to partition parallel jobs
        constexpr U32 g_nodesPerPrepareDrawPartition = 16u;

        template<typename DataContainer>
        using ExecutorBuffer = RenderPassExecutor::ExecutorBuffer<DataContainer>;

        void UpdateBufferRange( RenderPassExecutor::BufferUpdateRange& range, SharedMutex& mutex, const U32 minIdx, const U32 maxIdx )
        {
            LockGuard<SharedMutex> w_lock(mutex);
            if (range._firstIDX > minIdx)
            {
                range._firstIDX = minIdx;
            }
            if (range._lastIDX < maxIdx)
            {
                range._lastIDX = maxIdx;
            }
        }

        void UpdateBufferRange(RenderPassExecutor::BufferUpdateRange& range, SharedMutex& mutex, const U32 idx )
        {
            UpdateBufferRange(range, mutex, idx, idx);
        }

        template<typename DataContainer>
        void WriteToGPUBuffer( ExecutorBuffer<DataContainer>& executorBuffer, GFX::MemoryBarrierCommand& memCmdInOut )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

            RenderPassExecutor::BufferUpdateRange crtRange, prevRange;
            {
                SharedLock<SharedMutex> w_lock( executorBuffer._rangeLock );
                crtRange = executorBuffer._updateRange;
                prevRange = executorBuffer._prevUpdateRange;
            }

            if (crtRange.range() > 0u)
            {
                const size_t bufferAlignmentRequirement = ShaderBuffer::AlignmentRequirement(executorBuffer._gpuBuffer->getUsage());
                const size_t bufferPrimitiveSize = executorBuffer._gpuBuffer->getPrimitiveSize();
                if (bufferPrimitiveSize < bufferAlignmentRequirement)
                {
                    // We need this due to alignment concerns
                    if (crtRange._firstIDX % 2u != 0)
                    {
                        crtRange._firstIDX -= 1u;
                    }
                    if (crtRange._lastIDX % 2u == 0)
                    {
                        crtRange._lastIDX += 1u;
                    }
                }

                executorBuffer._gpuBuffer->incQueue();
                if (prevRange.range() > 0)
                {
                    crtRange._firstIDX = std::min(crtRange._firstIDX, prevRange._firstIDX);
                    crtRange._lastIDX = std::max(crtRange._lastIDX, prevRange._lastIDX);
                }

                bufferPtr data = &executorBuffer._data._gpuData[crtRange._firstIDX];
                memCmdInOut._bufferLocks.push_back(executorBuffer._gpuBuffer->writeData({ crtRange._firstIDX, crtRange.range() }, data));
            }

            {
                LockGuard<SharedMutex> w_lock(executorBuffer._rangeLock);
                executorBuffer._updateRange.reset();
                executorBuffer._prevUpdateRange = crtRange;
            }

            LockGuard<Mutex> w_lock(executorBuffer._nodeProcessedLock);
            executorBuffer._nodeProcessedThisFrame.clear();
        }

        template<typename DataContainer>
        bool NodeNeedsUpdate( ExecutorBuffer<DataContainer>& executorBuffer, const U32 indirectionIDX )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Scene );
            LockGuard<Mutex> w_lock( executorBuffer._nodeProcessedLock );
            // Returns true if and only if the insertion took place! Since this is a set, we can only have unique instances of the ID
            return executorBuffer._nodeProcessedThisFrame.insert(indirectionIDX).second;
        }

        template<typename DataContainer>
        void Reset(ExecutorBuffer<DataContainer>& executorBuffer)
        {
            executorBuffer._gpuBuffer.reset();
            {
                LockGuard<SharedMutex> w_lock(executorBuffer._data._freeListLock);
                executorBuffer._data._freeList.clear();
                executorBuffer._data._gpuData.clear();
            }
            {
                LockGuard<SharedMutex> w_lock(executorBuffer._rangeLock);
                executorBuffer._updateRange.reset();
            }
            {
                LockGuard<Mutex> w_lock(executorBuffer._nodeProcessedLock);
                executorBuffer._nodeProcessedThisFrame.clear();
            }
        }
    }

    bool RenderPassExecutor::s_globalDataInit = false;
    bool RenderPassExecutor::s_resizeBufferQueued = false;

    ExecutorBuffer<RenderPassExecutor::BufferMaterialData>    RenderPassExecutor::s_materialBuffer;
    ExecutorBuffer<RenderPassExecutor::BufferTransformData>   RenderPassExecutor::s_transformBuffer;
    ExecutorBuffer<RenderPassExecutor::BufferIndirectionData> RenderPassExecutor::s_indirectionBuffer;
    
    Pipeline* RenderPassExecutor::s_OITCompositionPipeline = nullptr;
    Pipeline* RenderPassExecutor::s_OITCompositionMSPipeline = nullptr;
    Pipeline* RenderPassExecutor::s_ResolveGBufferPipeline = nullptr;


    [[nodiscard]] U32 RenderPassExecutor::BufferUpdateRange::range() const noexcept
    {
        return _lastIDX >= _firstIDX ? _lastIDX - _firstIDX + 1u : 0u;
    }

    void RenderPassExecutor::BufferUpdateRange::reset() noexcept
    {
        _firstIDX = U32_MAX;
        _lastIDX = 0u;
    }

    RenderPassExecutor::RenderPassExecutor( RenderPassManager& parent, GFXDevice& context, const RenderStage stage )
        : _parent( parent )
        , _context( context )
        , _stage( stage )
    {
        _renderQueue = std::make_unique<RenderQueue>( parent.parent(), stage );

        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        bufferDescriptor._bufferParams._usageType = BufferUsageType::COMMAND_BUFFER;
        bufferDescriptor._bufferParams._elementCount = Config::MAX_VISIBLE_NODES * TotalPassCountForStage( stage );
        bufferDescriptor._bufferParams._elementSize = sizeof( IndirectIndexedDrawCommand );
        bufferDescriptor._ringBufferLength = Config::MAX_FRAMES_IN_FLIGHT + 1u;
        Util::StringFormat( bufferDescriptor._name, "CMD_DATA_{}", TypeUtil::RenderStageToString( stage ) );
        _cmdBuffer = _context.newSB( bufferDescriptor );
    }

    void RenderPassExecutor::OnStartup( GFXDevice& gfx )
    {
        Material::OnStartup();

        s_indirectionBuffer._data._gpuData.resize(Config::MAX_VISIBLE_NODES);
        s_indirectionBuffer._data._freeList.resize(Config::MAX_VISIBLE_NODES, true);
        s_transformBuffer._data._gpuData.resize(Config::MAX_VISIBLE_NODES);
        s_transformBuffer._data._freeList.resize(Config::MAX_VISIBLE_NODES, true);
        s_materialBuffer._data._gpuData.resize(Config::MAX_CONCURRENT_MATERIALS);
        s_materialBuffer._data._freeList.resize(Config::MAX_CONCURRENT_MATERIALS);
        ResizeGPUBuffers(gfx, Config::MAX_VISIBLE_NODES, Config::MAX_VISIBLE_NODES, Config::MAX_CONCURRENT_MATERIALS);
    }

    void RenderPassExecutor::OnShutdown( [[maybe_unused]] GFXDevice& gfx )
    {
        s_globalDataInit = false;
        s_OITCompositionPipeline = nullptr;
        s_OITCompositionMSPipeline = nullptr;
        s_ResolveGBufferPipeline = nullptr;
        Reset(s_materialBuffer);
        Reset(s_transformBuffer);
        Reset(s_indirectionBuffer);

        Material::OnShutdown();
    }

    void RenderPassExecutor::PrepareGPUBuffers(GFXDevice& context)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

        if (s_resizeBufferQueued)
        {
            const size_t indirectionCount = s_indirectionBuffer._data._gpuData.size();
            const size_t transformCount = s_transformBuffer._data._gpuData.size();
            const size_t materialCount = s_materialBuffer._data._gpuData.size();

            ResizeGPUBuffers(context, indirectionCount, transformCount, materialCount);
            s_resizeBufferQueued = false;
        }
    }

    void RenderPassExecutor::FlushBuffersToGPU(GFX::MemoryBarrierCommand& memCmdInOut)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

        {
            PROFILE_SCOPE("buildDrawCommands - update material buffer", Profiler::Category::Graphics);
            WriteToGPUBuffer(s_materialBuffer, memCmdInOut);
        }
        {
            PROFILE_SCOPE("buildDrawCommands - update transform buffer", Profiler::Category::Graphics);
            WriteToGPUBuffer(s_transformBuffer, memCmdInOut);
        }
        {
            PROFILE_SCOPE("buildDrawCommands - update indirection buffer", Profiler::Category::Graphics);
            WriteToGPUBuffer(s_indirectionBuffer, memCmdInOut);
        }
        {
            PROFILE_SCOPE("Increment Lifetime", Profiler::Category::Scene);
            for (auto& it : s_materialBuffer._data._freeList)
            {
                it._framesSinceLastUsed += (it._hash != INVALID_MAT_HASH) ? 1u : 0u;
            }
        }
    }

    void RenderPassExecutor::ResizeGPUBuffers(GFXDevice& context, const size_t indirectionCount, const size_t transformCount, const size_t materialCount)
    {
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        bufferDescriptor._bufferParams._usageType = BufferUsageType::UNBOUND_BUFFER;
        bufferDescriptor._ringBufferLength = Config::MAX_FRAMES_IN_FLIGHT + 1u;

        if (s_materialBuffer._gpuBuffer == nullptr || s_materialBuffer._gpuBuffer->getPrimitiveCount() != to_U32(materialCount))
        {
            bufferDescriptor._bufferParams._elementCount = to_U32(materialCount);
            bufferDescriptor._bufferParams._elementSize = sizeof(NodeMaterialData);
            bufferDescriptor._name = "NODE_MATERIAL_DATA";
            if (!s_materialBuffer._data._gpuData.empty())
            {
                bufferDescriptor._initialData = { s_materialBuffer._data._gpuData.data(), s_materialBuffer._data._gpuData.size() };
            }

            s_materialBuffer._gpuBuffer = context.newSB(bufferDescriptor);
        }
        if (s_transformBuffer._gpuBuffer == nullptr || s_transformBuffer._gpuBuffer->getPrimitiveCount() != to_U32(transformCount))
        {
            bufferDescriptor._bufferParams._elementCount = to_U32(transformCount);
            bufferDescriptor._bufferParams._elementSize = sizeof(NodeTransformData);
            bufferDescriptor._name = "NODE_TRANSFORM_DATA";
            if (!s_transformBuffer._data._gpuData.empty())
            {
                bufferDescriptor._initialData = { s_transformBuffer._data._gpuData.data(), s_transformBuffer._data._gpuData.size() };
            }

            s_transformBuffer._gpuBuffer = context.newSB(bufferDescriptor);
        }
        if (s_indirectionBuffer._gpuBuffer == nullptr || s_indirectionBuffer._gpuBuffer->getPrimitiveCount() != to_U32(indirectionCount))
        {
            bufferDescriptor._bufferParams._elementCount = to_U32(indirectionCount);
            bufferDescriptor._bufferParams._elementSize = sizeof(NodeIndirectionData);
            bufferDescriptor._name = "NODE_INDIRECTION_DATA";
            if (!s_indirectionBuffer._data._gpuData.empty())
            {
                bufferDescriptor._initialData = { s_indirectionBuffer._data._gpuData.data(), s_indirectionBuffer._data._gpuData.size() };
            }
            s_indirectionBuffer._gpuBuffer = context.newSB(bufferDescriptor);
        }
    }

    void RenderPassExecutor::PostInit( GFXDevice& context,
                                       const Handle<ShaderProgram> OITCompositionShader,
                                       const Handle<ShaderProgram> OITCompositionShaderMS,
                                       const Handle<ShaderProgram> ResolveGBufferShaderMS )
    {
        if ( !s_globalDataInit )
        {
            s_globalDataInit = true;

            PipelineDescriptor pipelineDescriptor;
            pipelineDescriptor._stateBlock._cullMode = CullMode::NONE;
            pipelineDescriptor._stateBlock._depthTestEnabled = false;
            pipelineDescriptor._stateBlock._depthWriteEnabled = false;
            pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
            pipelineDescriptor._shaderProgramHandle = ResolveGBufferShaderMS;
            s_ResolveGBufferPipeline = context.newPipeline( pipelineDescriptor );

            BlendingSettings& state0 = pipelineDescriptor._blendStates._settings[to_U8( GFXDevice::ScreenTargets::ALBEDO )];
            state0.enabled( true );
            state0.blendOp( BlendOperation::ADD );
            state0.blendSrc( BlendProperty::INV_SRC_ALPHA );
            state0.blendDest( BlendProperty::ONE );

            pipelineDescriptor._shaderProgramHandle = OITCompositionShader;
            s_OITCompositionPipeline = context.newPipeline( pipelineDescriptor );

            pipelineDescriptor._shaderProgramHandle = OITCompositionShaderMS;
            s_OITCompositionMSPipeline = context.newPipeline( pipelineDescriptor );
        }
    }

    RenderPassExecutor::ParseResult RenderPassExecutor::processVisibleNodeTransform(RenderingComponent* rComp, const D64 interpolationFactor, U32& transformIDXOut)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        ParseResult ret{};

        const U32 indirectionIDX = Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry( rComp );
        if ( !NodeNeedsUpdate( s_transformBuffer, indirectionIDX ) )
        {
            return ret;
        }

        transformIDXOut = Attorney::RenderingCompRenderPassExecutor::getIndirectionNodeData(rComp)._transformIDX;

        PROFILE_SCOPE( "Buffer idx update", Profiler::Category::Scene );
        DIVIDE_ASSERT(transformIDXOut != NodeIndirectionData::INVALID_IDX);

        // Should be thread safe at this point
        NodeTransformData& transformOut = s_transformBuffer._data._gpuData[transformIDXOut];

        const SceneGraphNode* node = rComp->parentSGN();

        transformOut._prevTransform = transformOut._transform;

        if ( COMPARE(interpolationFactor, 1.))
        {
            const TransformValues& transformIn    = node->get<TransformComponent>()->world()._values;
            transformOut._transform._position.xyz = transformIn._translation;
            transformOut._transform._scale.xyz    = transformIn._scale;
            transformOut._transform._rotation     = transformIn._orientation;
        }
        else if ( COMPARE(interpolationFactor, 0.))
        {
            const TransformValues& transformInPrev = node->get<TransformComponent>()->world()._previousValues;
            transformOut._transform._position.xyz  = transformInPrev._translation;
            transformOut._transform._scale.xyz     = transformInPrev._scale;
            transformOut._transform._rotation      = transformInPrev._orientation;
        }
        else
        {
            auto tComp = node->get<TransformComponent>();
            const TransformValues& transformInCrt  = tComp->world()._values;
            const TransformValues& transformInPrev = tComp->world()._previousValues;

            const TransformValues interpolatedValues = Lerp(transformInPrev, transformInCrt, to_F32(interpolationFactor));
            transformOut._transform._position.xyz    = interpolatedValues._translation;
            transformOut._transform._scale.xyz       = interpolatedValues._scale;
            transformOut._transform._rotation        = interpolatedValues._orientation;
        }

        transformOut._boundingSphere = node->get<BoundsComponent>()->getBoundingSphere()._sphere;

        transformOut._data = VECTOR4_ZERO;

        if ( node->HasComponents( ComponentType::ANIMATION ) )
        {
            AnimEvaluator::FrameIndex frameIndex{};
            const AnimationComponent* animComp = node->get<AnimationComponent>();
            const U8 boneCount = animComp->boneCount();
            if ( animComp->playAnimations() )
            {
                frameIndex = animComp->frameIndex();
            }
            transformOut._data.x = to_F32(std::max(frameIndex._curr, 0));
            transformOut._data.y = to_F32(boneCount);
        }

        transformOut._data.z = rComp->getLoDLevel(RenderStage::DISPLAY);

        U8 selectionFlag = 0u;
        if ( node->HasComponents( ComponentType::SELECTION ) )
        {
            auto selComp = node->get<SelectionComponent>();
            selectionFlag = to_U8(selComp->selectionType() );

            if (selectionFlag != 0u)
            {
                if (!selComp->hoverHighlightEnabled() &&
                    (selectionFlag == to_U8(SelectionComponent::SelectionType::HOVERED) ||
                     selectionFlag == to_U8(SelectionComponent::SelectionType::PARENT_HOVERED)))
                {
                    selectionFlag = 0u;
                }
                if (!selComp->selectionHighlightEnabled() &&
                    (selectionFlag == to_U8(SelectionComponent::SelectionType::SELECTED) ||
                     selectionFlag == to_U8(SelectionComponent::SelectionType::PARENT_SELECTED)))
                {
                    selectionFlag = 0u;
                }
            }
        }

        transformOut._data.w = to_F32
        (
            Util::PACK_UNORM4x8
            (
                rComp->occlusionCull() ? 1u : 0u,
                selectionFlag,
                0u,
                0u
            )
        );

        ret._updateBuffer = true;
        return ret;
    }

    RenderPassExecutor::ParseResult RenderPassExecutor::processVisibleNodeMaterial( RenderingComponent* rComp, U32& materialIDXOut, U32& indirectionIDXOut)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        ParseResult ret{};
        indirectionIDXOut = Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry(rComp);
        U32 materialIDX = Attorney::RenderingCompRenderPassExecutor::getIndirectionNodeData(rComp)._materialIDX;

        if (!NodeNeedsUpdate(s_materialBuffer, indirectionIDXOut))
        {
            materialIDXOut = materialIDX;
            return ret;
        }

        NodeMaterialData tempData{};
        // Get the colour matrix (base colour, metallic, etc)
        rComp->getMaterialData( tempData );

        // Match materials
        const size_t materialHash = HashMaterialData( tempData );

        {// Try and match an existing material
            SharedLock<SharedMutex> r_lock( s_materialBuffer._data._freeListLock);
            PROFILE_SCOPE( "processVisibleNode - try match material", Profiler::Category::Scene );

            // Usually, the material doesn't change, so check that first
            if (materialIDX != NodeIndirectionData::INVALID_IDX &&
                s_materialBuffer._data._freeList[materialIDX]._hash == materialHash)
            {
                materialIDXOut = materialIDX;
                return ret;
            }

            auto& infoContainer = s_materialBuffer._data._freeList;
            // Otherwise, we have an updated material, so try to match against a different one first
            for ( size_t idx = 0u; idx < infoContainer.size(); ++idx )
            {
                auto& [hash, framesSinceLastUsed ] = infoContainer[idx];
                if ( hash == materialHash )
                {
                    framesSinceLastUsed = 0u;
                    Attorney::RenderingCompRenderPassExecutor::setMaterialIDX(rComp, to_U32(idx));
                    materialIDXOut = materialIDX;

                    ret._updateIndirection = true;
                    s_indirectionBuffer._data._gpuData[indirectionIDXOut]._materialIDX = materialIDX;
                    return ret;
                }
            }
        }

        // If we fail, try and find an empty slot and update it
        PROFILE_SCOPE( "processVisibleNode - process unmatched material", Profiler::Category::Scene );

        LockGuard<SharedMutex> w_lock(s_materialBuffer._data._freeListLock);
        materialIDX = NodeIndirectionData::INVALID_IDX;

        auto& infoContainer = s_materialBuffer._data._freeList;
        // No match found (cache miss) so try again and add a new entry if we still fail
        for ( size_t idx = 0u; idx < infoContainer.size(); ++idx )
        {
            auto& entry = infoContainer[idx];
            if (entry._hash == materialHash)
            {
                entry._framesSinceLastUsed = 0u;
                materialIDX = to_U32(idx);
                break;
            }
        }

        // Cache miss
        if (materialIDX == NodeIndirectionData::INVALID_IDX)
        {
            for (size_t idx = 0u; idx < infoContainer.size(); ++idx )
            {
                auto& entry = infoContainer[idx];
                if (entry._hash == INVALID_MAT_HASH ||
                    entry._framesSinceLastUsed >= MaterialLookupInfo::MAX_FRAME_LIFETIME)
                {
                    // ... in which case our current idx is what we are looking for ...
                    entry._hash = materialHash;
                    entry._framesSinceLastUsed = 0u;
                    materialIDX = to_U32(idx);
                    break;
                }
            }

            // Cache miss + resize required
            if (materialIDX == NodeIndirectionData::INVALID_IDX)
            {
                materialIDX = to_U32(s_materialBuffer._data._freeList.size());
                s_materialBuffer._data._freeList.emplace_back(MaterialLookupInfo
                {
                    ._hash = materialHash,
                    ._framesSinceLastUsed = 0u
                });
                s_resizeBufferQueued = true;
            }

            DIVIDE_ASSERT( materialIDX != NodeIndirectionData::INVALID_IDX );

            ret._updateBuffer = true;
            s_materialBuffer._data._gpuData[materialIDX] = tempData;
        }

        Attorney::RenderingCompRenderPassExecutor::setMaterialIDX(rComp, materialIDX);

        ret._updateIndirection = true;
        s_indirectionBuffer._data._gpuData[indirectionIDXOut]._materialIDX = materialIDX;

        materialIDXOut = materialIDX;

        return ret;
    }

    void RenderPassExecutor::parseTransformRange(RenderBin::SortedQueue& queue, U32 start, U32 end, const PlayerIndex index, const D64 interpolationFactor)
    {
        U32 minTransform = U32_MAX, maxTransform = 0u;

        for (U32 i = start; i < end; ++i)
        {
            U32 transformIDXOut = 0u;
            ParseResult ret = processVisibleNodeTransform( queue[i], interpolationFactor, transformIDXOut );
            DIVIDE_ASSERT(!ret._updateIndirection);

            if (transformIDXOut < minTransform)
            {
                minTransform = transformIDXOut;
            }
            if (transformIDXOut > maxTransform)
            {
                maxTransform = transformIDXOut;
            }
        }

        if ( minTransform <= maxTransform)
        {
            UpdateBufferRange(s_transformBuffer._updateRange, s_transformBuffer._rangeLock, minTransform, maxTransform);
        }
    }

    void RenderPassExecutor::parseMaterialRange( RenderBin::SortedQueue& queue, const U32 start, const U32 end )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        U32 minMaterial = U32_MAX, maxMaterial = 0u;
        U32 minIndirection = U32_MAX, maxIndirection = 0u;

        for ( U32 i = start; i < end; ++i )
        {
            U32 materialIDXOut = 0u, indirectionIDXOut = 0u;
            const ParseResult ret = processVisibleNodeMaterial( queue[i], materialIDXOut, indirectionIDXOut);
            if (ret._updateBuffer)
            {
                if (materialIDXOut < minMaterial)
                {
                    minMaterial = materialIDXOut;
                }
                if (materialIDXOut > maxMaterial)
                {
                    maxMaterial = materialIDXOut;
                }
            }
            if (ret._updateIndirection)
            {
                if (indirectionIDXOut < minIndirection )
                {
                    minIndirection = indirectionIDXOut;
                }
                if (indirectionIDXOut > maxIndirection )
                {
                    maxIndirection = indirectionIDXOut;
                }
            }
        }

        if (minMaterial <= maxMaterial)
        {
            UpdateBufferRange( s_materialBuffer._updateRange, s_materialBuffer._rangeLock, minMaterial, maxMaterial );
        }

        if (minIndirection <= maxIndirection)
        {
            UpdateBufferRange( s_indirectionBuffer._updateRange, s_indirectionBuffer._rangeLock, minIndirection, maxIndirection );
        }
    }

    [[nodiscard]] constexpr size_t MIN_NODE_COUNT( const size_t N, const size_t L) noexcept
    {
        return N == 0u ? L : N;
    }

    size_t RenderPassExecutor::buildDrawCommands( const PlayerIndex index, const RenderPassParams& params, const bool doPrePass, const bool doOITPass, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        constexpr bool doMainPass = true;

        RenderPass::PassData passData = _parent.getPassForStage( _stage ).getPassData();

        efficient_clear( _drawCommands );

        for ( RenderBin::SortedQueue& sQueue : _sortedQueues )
        {
            sQueue.resize( 0 );
            sQueue.reserve( Config::MAX_VISIBLE_NODES );
        }

        const size_t queueTotalSize = _renderQueue->getSortedQueues( {}, _sortedQueues );

        //Erase nodes with no draw commands
        for ( RenderBin::SortedQueue& queue : _sortedQueues )
        {
            erase_if( queue,
                      []( RenderingComponent* item ) noexcept
                      {
                          return !Attorney::RenderingCompRenderPass::hasDrawCommands( *item );
                      } );
        }

        bool updateTaskDirty = false;

        const D64 interpFactor = GFXDevice::FrameInterpolationFactor();

        TaskPool& pool = _context.context().taskPool( TaskPoolType::RENDERER );
        Task* updateTask = CreateTask( TASK_NOP );
        {
            PROFILE_SCOPE( "buildDrawCommands - process nodes: Transforms", Profiler::Category::Scene );

            U32& nodeCount = *passData._lastNodeCount;
            nodeCount = 0u;
            for ( RenderBin::SortedQueue& queue : _sortedQueues )
            {
                const U32 queueSize = to_U32( queue.size() );
                if ( queueSize > g_nodesPerPrepareDrawPartition )
                {
                    const U32 midPoint = queueSize / 2;
                    Start( *CreateTask( updateTask, [this, index, interpFactor, &queue, midPoint]( const Task& )
                                        {
                                            parseTransformRange( queue, 0u, midPoint, index, interpFactor );
                                        } ), pool );
                    Start( *CreateTask( updateTask, [this, index, interpFactor, &queue, midPoint, queueSize]( const Task& )
                                        {
                                            parseTransformRange(queue, midPoint, queueSize, index, interpFactor );
                                        } ), pool );
                    updateTaskDirty = true;
                }
                else
                {
                    parseTransformRange(queue, 0u, queueSize, index, interpFactor );
                }
                nodeCount += queueSize;
            }
            assert( nodeCount < Config::MAX_VISIBLE_NODES );
        }
        {
            PROFILE_SCOPE( "buildDrawCommands - process nodes: Materials", Profiler::Category::Scene );
            for ( RenderBin::SortedQueue& queue : _sortedQueues )
            {
                const U32 queueSize = to_U32( queue.size() );
                if ( queueSize > g_nodesPerPrepareDrawPartition )
                {
                    const U32 midPoint = queueSize / 2;
                    Start( *CreateTask( updateTask, [this, &queue, midPoint]( const Task& )
                                        {
                                            parseMaterialRange( queue, 0u, midPoint );
                                        } ), pool );
                    Start( *CreateTask( updateTask, [this, &queue, midPoint, queueSize]( const Task& )
                                        {
                                            parseMaterialRange( queue, midPoint, queueSize );
                                        } ), pool );
                    updateTaskDirty = true;
                }
                else
                {
                    parseMaterialRange( queue, 0u, queueSize );
                }
            }
        }
        if (updateTaskDirty) 
        {
            PROFILE_SCOPE( "buildDrawCommands - process nodes: Waiting for tasks to finish", Profiler::Category::Scene );
            Start( *updateTask, pool );
            Wait( *updateTask, pool );
        }

        RenderStagePass stagePass = params._stagePass;
        const U32 startOffset = Config::MAX_VISIBLE_NODES * IndexForStage( stagePass );
        const auto retrieveCommands = [&]()
        {
            for ( RenderBin::SortedQueue& queue : _sortedQueues )
            {
                for ( RenderingComponent* rComp : queue )
                {
                    Attorney::RenderingCompRenderPass::retrieveDrawCommands( *rComp, stagePass, startOffset, _drawCommands );
                }
            }
        };

        {
            const RenderPassType prevType = stagePass._passType;
            if ( doPrePass )
            {
                PROFILE_SCOPE( "buildDrawCommands - retrieve draw commands: PRE_PASS", Profiler::Category::Scene );
                stagePass._passType = RenderPassType::PRE_PASS;
                retrieveCommands();
            }
            if ( doMainPass )
            {
                PROFILE_SCOPE( "buildDrawCommands - retrieve draw commands: MAIN_PASS", Profiler::Category::Scene );
                stagePass._passType = RenderPassType::MAIN_PASS;
                retrieveCommands();
            }
            if ( doOITPass )
            {
                PROFILE_SCOPE( "buildDrawCommands - retrieve draw commands: OIT_PASS", Profiler::Category::Scene );
                stagePass._passType = RenderPassType::OIT_PASS;
                retrieveCommands();
            }
            else
            {
                PROFILE_SCOPE( "buildDrawCommands - retrieve draw commands: TRANSPARENCY_PASS", Profiler::Category::Scene );
                stagePass._passType = RenderPassType::TRANSPARENCY_PASS;
                retrieveCommands();
            }
            stagePass._passType = prevType;
        }

        const U32 cmdCount = to_U32( _drawCommands.size() );
        *passData._lastCommandCount = cmdCount;


        if ( cmdCount > 0u )
        {
            PROFILE_SCOPE( "buildDrawCommands - update command buffer", Profiler::Category::Graphics );
            _cmdBuffer->incQueue();
            memCmdInOut._bufferLocks.push_back( _cmdBuffer->writeData( { startOffset, cmdCount }, _drawCommands.data() ) );
        }

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_BATCH;
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::NONE ); //Command buffer only
            Set( binding._data, _cmdBuffer.get(), { startOffset, Config::MAX_VISIBLE_NODES});
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 2u, ShaderStageVisibility::COMPUTE );
            Set( binding._data, _cmdBuffer.get(), { startOffset, Config::MAX_VISIBLE_NODES } );
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 3u, ShaderStageVisibility::ALL );
            Set(binding._data, s_transformBuffer._gpuBuffer.get(), { 0u, s_transformBuffer._data._gpuData.size() });
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 4u, ShaderStageVisibility::ALL );
            Set( binding._data, s_indirectionBuffer._gpuBuffer.get(), { 0u, s_indirectionBuffer._data._gpuData.size() });
        }
        {
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 5u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, s_materialBuffer._gpuBuffer.get(), { 0u, s_materialBuffer._data._gpuData.size() });
        }

        return queueTotalSize;
    }

    size_t RenderPassExecutor::prepareNodeData( const PlayerIndex index,
                                                const RenderPassParams& params,
                                                const CameraSnapshot& cameraSnapshot,
                                                const bool hasInvalidNodes,
                                                const bool doPrePass,
                                                const bool doOITPass,
                                                GFX::CommandBuffer& bufferInOut,
                                                GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( hasInvalidNodes )
        {
            bool nodeRemoved = true;
            while ( nodeRemoved )
            {
                nodeRemoved = false;
                for ( size_t i = 0; i < _visibleNodesCache.size(); ++i )
                {
                    if ( !_visibleNodesCache.node( i )._materialReady )
                    {
                        _visibleNodesCache.remove( i );
                        nodeRemoved = true;
                        break;
                    }
                }
            }
        }

        RenderStagePass stagePass = params._stagePass;
        const SceneRenderState& sceneRenderState = _parent.parent().projectManager()->activeProject()->getActiveScene()->state()->renderState();
        {
            Mutex memCmdLock;

            _renderQueue->clear();

            const auto cbk = [&]( const Task* /*parentTask*/, const U32 start, const U32 end )
            {
                GFX::MemoryBarrierCommand postDrawMemCmd{};
                for ( U32 i = start; i < end; ++i )
                {
                    const VisibleNode& node = _visibleNodesCache.node( i );
                    RenderingComponent* rComp = node._node->get<RenderingComponent>();
                    if ( Attorney::RenderingCompRenderPass::prepareDrawPackage( *rComp, cameraSnapshot, sceneRenderState, stagePass, postDrawMemCmd, true ) )
                    {
                        _renderQueue->addNodeToQueue( node._node, stagePass, node._distanceToCameraSq );
                    }
                }

                LockGuard<Mutex> w_lock(memCmdLock);
                memCmdInOut._bufferLocks.insert(memCmdInOut._bufferLocks.cend(), postDrawMemCmd._bufferLocks.cbegin(), postDrawMemCmd._bufferLocks.cend());
            };

            const ParallelForDescriptor descriptor
            {
                ._iterCount = to_U32( _visibleNodesCache.size() ),
                ._partitionSize = g_nodesPerPrepareDrawPartition,
                ._priority = TaskPriority::DONT_CARE,
                ._useCurrentThread = true
            };

            Parallel_For( _parent.parent().platformContext().taskPool( TaskPoolType::RENDERER ), descriptor, cbk );
            
            _renderQueue->sort( stagePass );
        }

        efficient_clear( _renderQueuePackages );

        RenderQueue::PopulateQueueParams queueParams{};
        queueParams._stagePass = stagePass;
        queueParams._binType = RenderBinType::COUNT;
        queueParams._filterByBinType = false;
        _renderQueue->populateRenderQueues( queueParams, _renderQueuePackages );

        return buildDrawCommands( index, params, doPrePass, doOITPass, bufferInOut, memCmdInOut );
    }

    void RenderPassExecutor::prepareRenderQueues( const RenderPassParams& params,
                                                  const CameraSnapshot& cameraSnapshot,
                                                  bool transparencyPass,
                                                  const RenderingOrder renderOrder,
                                                  GFX::CommandBuffer& bufferInOut,
                                                  GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        RenderStagePass stagePass = params._stagePass;
        const RenderBinType targetBin = transparencyPass ? RenderBinType::TRANSLUCENT : RenderBinType::COUNT;
        const SceneRenderState& sceneRenderState = _parent.parent().projectManager()->activeProject()->getActiveScene()->state()->renderState();

        _renderQueue->clear( targetBin );

        Mutex memCmdLock;
        GFX::MemoryBarrierCommand postDrawMemCmd{};

        const U32 nodeCount = to_U32( _visibleNodesCache.size() );

        const auto cbk = [&]( const Task* /*parentTask*/, const U32 start, const U32 end )
        {
            for ( U32 i = start; i < end; ++i )
            {
                const VisibleNode& node = _visibleNodesCache.node( i );
                SceneGraphNode* sgn = node._node;
                if ( sgn->getNode().renderState().drawState( stagePass ) )
                {
                    if ( Attorney::RenderingCompRenderPass::prepareDrawPackage( *sgn->get<RenderingComponent>(), cameraSnapshot, sceneRenderState, stagePass, postDrawMemCmd, false ) )
                    {
                        _renderQueue->addNodeToQueue( sgn, stagePass, node._distanceToCameraSq, targetBin );
                    }
                }
            }

            LockGuard<Mutex> w_lock( memCmdLock );
            memCmdInOut._bufferLocks.insert( memCmdInOut._bufferLocks.cend(), postDrawMemCmd._bufferLocks.cbegin(), postDrawMemCmd._bufferLocks.cend() );
        };

        PROFILE_SCOPE( "prepareRenderQueues - parallel gather", Profiler::Category::Scene );
        const ParallelForDescriptor descriptor
        {
            ._iterCount = nodeCount,
            ._partitionSize = g_nodesPerPrepareDrawPartition,
            ._priority = TaskPriority::DONT_CARE,
            ._useCurrentThread = true
        };
        Parallel_For( _parent.parent().platformContext().taskPool( TaskPoolType::RENDERER ), descriptor, cbk );

        // Sort all bins
        _renderQueue->sort( stagePass, targetBin, renderOrder );

        efficient_clear( _renderQueuePackages );

        // Draw everything in the depth pass but only draw stuff from the translucent bin in the OIT Pass and everything else in the colour pass
        RenderQueue::PopulateQueueParams queueParams{};

        queueParams._stagePass = stagePass;
        if ( IsDepthPass( stagePass ) )
        {
            queueParams._binType = RenderBinType::COUNT;
            queueParams._filterByBinType = false;
        }
        else
        {
            queueParams._binType = RenderBinType::TRANSLUCENT;
            queueParams._filterByBinType = !transparencyPass;
        }

        _renderQueue->populateRenderQueues( queueParams, _renderQueuePackages );


        for ( const auto& [rComp, pkg] : _renderQueuePackages )
        {
            Attorney::RenderingCompRenderPassExecutor::getCommandBuffer( rComp, pkg, bufferInOut );
        }

        if ( params._stagePass._passType != RenderPassType::PRE_PASS )
        {
            auto cmd = GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut );
            Util::StringFormat( cmd->_scopeName, "Post Render pass for stage [ {} ]", TypeUtil::RenderStageToString( stagePass._stage ), to_U32( stagePass._stage ) );

            _renderQueue->postRender( Attorney::ProjectManagerRenderPass::renderState( _parent.parent().projectManager().get() ),
                                      params._stagePass,
                                      bufferInOut );

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
        }
    }

    void RenderPassExecutor::prePass( const RenderPassParams& params, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        assert( params._stagePass._passType == RenderPassType::PRE_PASS );

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = " - PrePass";


        GFX::BeginRenderPassCommand renderPassCmd{};
        renderPassCmd._name = "DO_PRE_PASS";
        renderPassCmd._target = params._target;
        renderPassCmd._descriptor = params._targetDescriptorPrePass;
        renderPassCmd._clearDescriptor = params._clearDescriptorPrePass;

        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut, renderPassCmd );
        
        prepareRenderQueues( params, cameraSnapshot, false, RenderingOrder::COUNT, bufferInOut, memCmdInOut );
        GFX::EnqueueCommand( bufferInOut, GFX::EndRenderPassCommand{} );
        
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void RenderPassExecutor::occlusionPass( [[maybe_unused]] const PlayerIndex idx,
                                            const CameraSnapshot& cameraSnapshot,
                                            [[maybe_unused]] const size_t visibleNodeCount,
                                            const RenderTargetID& sourceDepthBuffer,
                                            const RenderTargetID& targetHiZBuffer,
                                            GFX::CommandBuffer& bufferInOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "HiZ Construct & Cull";

        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_bufferLocks.emplace_back( BufferLock
        {
            ._range = {0u, U32_MAX },
            ._type = BufferSyncUsage::GPU_READ_TO_GPU_WRITE,
            ._buffer = _cmdBuffer->getBufferImpl()
        });

        // Update HiZ Target
        const auto [hizTexture, hizSampler] = _context.constructHIZ( sourceDepthBuffer, targetHiZBuffer, bufferInOut );
        // Run occlusion culling CS
        _context.occlusionCull( _parent.getPassForStage( _stage ).getPassData(),
                                hizTexture,
                                hizSampler,
                                cameraSnapshot,
                                _stage == RenderStage::DISPLAY,
                                bufferInOut );

        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_bufferLocks.emplace_back( BufferLock
        {
            ._range = {0u, U32_MAX },
            ._type = BufferSyncUsage::GPU_WRITE_TO_GPU_READ,
            ._buffer = _cmdBuffer->getBufferImpl()
        });

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void RenderPassExecutor::mainPass( const RenderPassParams& params, const CameraSnapshot& cameraSnapshot, RenderTarget& target, const bool prePassExecuted, const bool hasHiZ, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        assert( params._stagePass._passType == RenderPassType::MAIN_PASS );

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = " - MainPass";

        if ( params._target != INVALID_RENDER_TARGET_ID ) [[likely]]
        {

            GFX::BeginRenderPassCommand renderPassCmd{};
            renderPassCmd._name = "DO_MAIN_PASS";
            renderPassCmd._target = params._target;
            renderPassCmd._descriptor = params._targetDescriptorMainPass;
            renderPassCmd._clearDescriptor = params._clearDescriptorMainPass;

            GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut, renderPassCmd );

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_PASS;
            if ( params._stagePass._stage == RenderStage::DISPLAY )
            {
                const RenderTarget* MSSource = _context.renderTargetPool().getRenderTarget( params._target );
                RTAttachment* normalsAtt = MSSource->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::NORMALS );

                DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, normalsAtt->texture(), normalsAtt->_descriptor._sampler );
            }
            if ( hasHiZ )
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
                const RenderTarget* hizTarget = _context.renderTargetPool().getRenderTarget( params._targetHIZ );
                RTAttachment* hizAtt = hizTarget->getAttachment( RTAttachmentType::COLOUR );
                Set( binding._data, hizAtt->texture(), hizAtt->_descriptor._sampler );
            }
            else if ( prePassExecuted )
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
                RTAttachment* depthAtt = target.getAttachment( RTAttachmentType::DEPTH );
                Set( binding._data, depthAtt->texture(), depthAtt->_descriptor._sampler );
            }

            prepareRenderQueues( params, cameraSnapshot, false, RenderingOrder::COUNT, bufferInOut, memCmdInOut );
            GFX::EnqueueCommand( bufferInOut, GFX::EndRenderPassCommand{} );
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void RenderPassExecutor::woitPass( const RenderPassParams& params, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        assert( params._stagePass._passType == RenderPassType::OIT_PASS );

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = " - W-OIT Pass";

        // Step1: Draw translucent items into the accumulation and revealage buffers
        GFX::BeginRenderPassCommand* beginRenderPassOitCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
        beginRenderPassOitCmd->_name = "OIT PASS 1";
        beginRenderPassOitCmd->_target = params._targetOIT;
        beginRenderPassOitCmd->_clearDescriptor[to_base( GFXDevice::ScreenTargets::ACCUMULATION )] = { VECTOR4_ZERO, true };
        beginRenderPassOitCmd->_clearDescriptor[to_base( GFXDevice::ScreenTargets::REVEALAGE )]    = { VECTOR4_UNIT, true };
        beginRenderPassOitCmd->_clearDescriptor[to_base( GFXDevice::ScreenTargets::NORMALS )]      = { VECTOR4_ZERO, true };
        beginRenderPassOitCmd->_clearDescriptor[to_base( GFXDevice::ScreenTargets::MODULATE )]     = { VECTOR4_ZERO, false };
        beginRenderPassOitCmd->_descriptor._drawMask[to_base( GFXDevice::ScreenTargets::ACCUMULATION )] = true;
        beginRenderPassOitCmd->_descriptor._drawMask[to_base( GFXDevice::ScreenTargets::REVEALAGE )] = true;
        beginRenderPassOitCmd->_descriptor._drawMask[to_base( GFXDevice::ScreenTargets::NORMALS )]  = true;
        beginRenderPassOitCmd->_descriptor._drawMask[to_base( GFXDevice::ScreenTargets::MODULATE )]  = true;
        beginRenderPassOitCmd->_descriptor._autoResolveMSAA = params._targetDescriptorMainPass._autoResolveMSAA;
        beginRenderPassOitCmd->_descriptor._keepMSAADataAfterResolve = params._targetDescriptorMainPass._keepMSAADataAfterResolve;

        prepareRenderQueues( params, cameraSnapshot, true, RenderingOrder::COUNT, bufferInOut, memCmdInOut );
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut )->_transitionMask[to_base( GFXDevice::ScreenTargets::MODULATE )] = false;

        RenderTarget* oitRT = _context.renderTargetPool().getRenderTarget( params._targetOIT );
        const auto& accumAtt = oitRT->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ACCUMULATION );
        const auto& revAtt = oitRT->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::REVEALAGE );
        const auto& normalsAtt = oitRT->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::NORMALS );

        // Step2: Composition pass
        // Don't clear depth & colours and do not write to the depth buffer
        GFX::BeginRenderPassCommand* beginRenderPassCompCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut);
        beginRenderPassCompCmd->_name = "OIT PASS 2";
        beginRenderPassCompCmd->_target = params._target;
        beginRenderPassCompCmd->_descriptor._drawMask[to_base( GFXDevice::ScreenTargets::ALBEDO )] = true;
        beginRenderPassCompCmd->_descriptor._drawMask[to_base( GFXDevice::ScreenTargets::VELOCITY )] = false;
        beginRenderPassCompCmd->_descriptor._drawMask[to_base( GFXDevice::ScreenTargets::NORMALS )] = true;
        beginRenderPassCompCmd->_descriptor._autoResolveMSAA = params._targetDescriptorMainPass._autoResolveMSAA;
        beginRenderPassCompCmd->_descriptor._keepMSAADataAfterResolve = params._targetDescriptorMainPass._keepMSAADataAfterResolve;

        GFX::EnqueueCommand<GFX::SetCameraCommand>( bufferInOut )->_cameraSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot();
        GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = params._useMSAA ? s_OITCompositionMSPipeline : s_OITCompositionPipeline;
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, accumAtt->texture(), accumAtt->_descriptor._sampler );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, revAtt->texture(), revAtt->_descriptor._sampler );
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 2u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, normalsAtt->texture(), normalsAtt->_descriptor._sampler );
            }
        }
        GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    void RenderPassExecutor::transparencyPass( const RenderPassParams& params, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        assert( params._stagePass._passType == RenderPassType::TRANSPARENCY_PASS );

        //Grab all transparent geometry
        GFX::BeginRenderPassCommand beginRenderPassTransparentCmd{};
        beginRenderPassTransparentCmd._name = "DO_TRANSPARENCY_PASS";
        beginRenderPassTransparentCmd._target = params._target;
        beginRenderPassTransparentCmd._descriptor = params._targetDescriptorMainPass;

        GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut, beginRenderPassTransparentCmd );
        prepareRenderQueues( params, cameraSnapshot, true, RenderingOrder::BACK_TO_FRONT, bufferInOut, memCmdInOut );
        GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );
    }

    void RenderPassExecutor::resolveMainScreenTarget( const RenderPassParams& params, GFX::CommandBuffer& bufferInOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // If we rendered to the multisampled screen target, we can now copy the colour to our regular buffer as we are done with it at this point
        if ( params._target == RenderTargetNames::SCREEN && params._useMSAA )
        {
            GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = " - Resolve Screen Targets";

            GFX::BeginRenderPassCommand* beginRenderPassCommand = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
            beginRenderPassCommand->_target = RenderTargetNames::NORMALS_RESOLVED;
            beginRenderPassCommand->_clearDescriptor[0u] = { VECTOR4_ZERO, true };
            beginRenderPassCommand->_descriptor._drawMask[0u] = true;
            beginRenderPassCommand->_name = "RESOLVE_MAIN_GBUFFER";

            GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = s_ResolveGBufferPipeline;

            const RenderTarget* MSSource = _context.renderTargetPool().getRenderTarget( params._target );
            RTAttachment* normalsAtt = MSSource->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::NORMALS );

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, normalsAtt->texture(), normalsAtt->_descriptor._sampler );

            GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();
            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( bufferInOut );

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
        }
    }

    bool RenderPassExecutor::validateNodesForStagePass( const RenderStagePass stagePass )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        bool ret = false;
        const I32 nodeCount = to_I32( _visibleNodesCache.size() );
        for ( I32 i = nodeCount - 1; i >= 0; i-- )
        {
            VisibleNode& node = _visibleNodesCache.node( i );
            if ( node._node == nullptr || (node._materialReady && !Attorney::SceneGraphNodeRenderPassManager::canDraw( node._node, stagePass )) )
            {
                node._materialReady = false;
                ret = true;
            }
        }
        return ret;
    }

    void RenderPassExecutor::doCustomPass( const PlayerIndex playerIdx, Camera* camera, RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        assert( params._stagePass._stage == _stage );

        if ( !camera->updateLookAt() )
        {
            NOP();
        }
        const CameraSnapshot& camSnapshot = camera->snapshot();

        GFX::BeginDebugScopeCommand* beginDebugScopeCmd = GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut );
        if ( params._passName.empty() )
        {
            Util::StringFormat<Str<64>>( beginDebugScopeCmd->_scopeName, "Custom pass ( {} )", TypeUtil::RenderStageToString( _stage ) );
        }
        else
        {
            Util::StringFormat<Str<64>>( beginDebugScopeCmd->_scopeName, "Custom pass ( {} - {} )", TypeUtil::RenderStageToString( _stage ), params._passName );
        }

        

        RenderTarget* target = _context.renderTargetPool().getRenderTarget( params._target );

        _visibleNodesCache.reset();
        if ( params._singleNodeRenderGUID == 0 ) [[unlikely]]
        {
            // Render nothing!
            NOP();
        }
        else if ( params._singleNodeRenderGUID == -1 ) [[likely]]
        {
            // Cull the scene and grab the visible nodes
            I64 ignoreGUID = params._sourceNode == nullptr ? -1 : params._sourceNode->getGUID();

            NodeCullParams cullParams = {};
            Attorney::ProjectManagerRenderPass::initDefaultCullValues( _parent.parent().projectManager().get(), _stage, cullParams );

            cullParams._clippingPlanes = params._clippingPlanes;
            cullParams._stage = _stage;
            cullParams._minExtents = params._minExtents;
            cullParams._ignoredGUIDS = { &ignoreGUID, 1 };
            cullParams._cameraEyePos = camSnapshot._eye;
            cullParams._frustum = &camera->getFrustum();
            cullParams._cullMaxDistance = std::min( cullParams._cullMaxDistance, camSnapshot._zPlanes.y );
            cullParams._maxLoD = params._maxLoD;

            U16 cullFlags = to_base( CullOptions::DEFAULT_CULL_OPTIONS );
            if ( !( params._drawMask & to_U8( 1 << to_base( RenderPassParams::Flags::DRAW_DYNAMIC_NODES ) ) ) )
            {
                cullFlags |= to_base( CullOptions::CULL_DYNAMIC_NODES );
            }
            if ( !(params._drawMask & to_U8( 1 << to_base( RenderPassParams::Flags::DRAW_STATIC_NODES ) ) ) )
            {
                cullFlags |= to_base( CullOptions::CULL_STATIC_NODES );
            }
            Attorney::ProjectManagerRenderPass::cullScene( _parent.parent().projectManager().get(), cullParams, cullFlags, _visibleNodesCache );
        }
        else
        {
            Attorney::ProjectManagerRenderPass::findNode( _parent.parent().projectManager().get(), camera->snapshot()._eye, params._singleNodeRenderGUID, _visibleNodesCache );
        }

        const bool drawTranslucents = (params._drawMask & to_U8( 1 << to_base( RenderPassParams::Flags::DRAW_TRANSLUCENT_NODES))) && _stage != RenderStage::SHADOW;

        constexpr bool doMainPass = true;
        // PrePass requires a depth buffer
        const bool doPrePass = _stage != RenderStage::SHADOW &&
                               params._target != INVALID_RENDER_TARGET_ID &&
                               target->usesAttachment( RTAttachmentType::DEPTH );
        const bool doOITPass = drawTranslucents && params._targetOIT != INVALID_RENDER_TARGET_ID;
        const bool doOcclusionPass = doPrePass && params._targetHIZ != INVALID_RENDER_TARGET_ID;

        bool hasInvalidNodes = false;
        {
            PROFILE_SCOPE( "doCustomPass: Validate draw", Profiler::Category::Scene );
            if ( doPrePass )
            {
                params._stagePass._passType = RenderPassType::PRE_PASS;
                hasInvalidNodes = validateNodesForStagePass( params._stagePass ) || hasInvalidNodes;
            }
            if ( doMainPass )
            {
                params._stagePass._passType = RenderPassType::MAIN_PASS;
                hasInvalidNodes = validateNodesForStagePass( params._stagePass ) || hasInvalidNodes;
            }
            if ( doOITPass )
            {
                params._stagePass._passType = RenderPassType::OIT_PASS;
                hasInvalidNodes = validateNodesForStagePass( params._stagePass ) || hasInvalidNodes;
            }
            else if ( drawTranslucents )
            {
                params._stagePass._passType = RenderPassType::TRANSPARENCY_PASS;
                hasInvalidNodes = validateNodesForStagePass( params._stagePass ) || hasInvalidNodes;
            }
        }

        if ( params._feedBackContainer != nullptr )
        {
            params._feedBackContainer->resize( _visibleNodesCache.size() );
            std::memcpy( params._feedBackContainer->data(), _visibleNodesCache.data(), _visibleNodesCache.size() * sizeof( VisibleNode ) );
            if ( hasInvalidNodes )
            {
                // This may hurt ... a lot ... -Ionut
                dvd_erase_if( *params._feedBackContainer, []( VisibleNode& node )
                              {
                                  return node._node == nullptr || !node._materialReady;
                              } );
            };
        }

        // Tell the Rendering API to draw from our desired PoV
        GFX::SetCameraCommand* camCmd = GFX::EnqueueCommand<GFX::SetCameraCommand>( bufferInOut );
        camCmd->_cameraSnapshot = camSnapshot;

        if ( params._refreshLightData )
        {
            Attorney::ProjectManagerRenderPass::prepareLightData( _parent.parent().projectManager().get(), _stage, camSnapshot, memCmdInOut );
        }

        GFX::EnqueueCommand<GFX::SetClipPlanesCommand>( bufferInOut )->_clippingPlanes =  params._clippingPlanes;

        // We prepare all nodes for the MAIN_PASS rendering. PRE_PASS and OIT_PASS are support passes only. Their order and sorting are less important.
        params._stagePass._passType = RenderPassType::MAIN_PASS;
        const size_t visibleNodeCount = prepareNodeData( playerIdx, params, camSnapshot, hasInvalidNodes, doPrePass, doOITPass, bufferInOut, memCmdInOut );

#   pragma region PRE_PASS
        // We need the pass to be PRE_PASS even if we skip the prePass draw stage as it is the default state subsequent operations expect
        params._stagePass._passType = RenderPassType::PRE_PASS;
        if ( doPrePass )
        {
            prePass( params, camSnapshot, bufferInOut, memCmdInOut );
            if ( _stage == RenderStage::DISPLAY )
            {
                resolveMainScreenTarget( params, bufferInOut );
            }
        }
#   pragma endregion
#   pragma region HI_Z
        if ( doOcclusionPass )
        {
            //ToDo: Find a way to skip occlusion culling for low number of nodes in view but also keep light culling up and running -Ionut
            assert( params._stagePass._passType == RenderPassType::PRE_PASS );

            // This also renders into our HiZ texture that we may want to use later in PostFX
            occlusionPass( playerIdx, camSnapshot, visibleNodeCount, RenderTargetNames::SCREEN, params._targetHIZ, bufferInOut );
        }
#   pragma endregion

#   pragma region LIGHT_PASS
        _context.getRenderer().prepareLighting( _stage, { 0, 0, target->getWidth(), target->getHeight() }, camSnapshot, bufferInOut );
#   pragma endregion

#   pragma region MAIN_PASS
        // Same as for PRE_PASS. Subsequent operations expect a certain state
        params._stagePass._passType = RenderPassType::MAIN_PASS;
        if ( _stage == RenderStage::DISPLAY )
        {
            _context.getRenderer().postFX().prePass( playerIdx, camSnapshot, bufferInOut );
        }
        if ( doMainPass ) [[likely]]
        {
            // If we draw translucents, no point in resolving now. We can resolve after translucent pass
            mainPass( params, camSnapshot, *target, doPrePass, doOcclusionPass, bufferInOut, memCmdInOut );
        }
        else [[unlikely]]
        {
            DIVIDE_UNEXPECTED_CALL();
        }
#   pragma endregion

#   pragma region TRANSPARENCY_PASS
        if ( drawTranslucents )
        {
            // If doIOTPass is false, use forward pass shaders (i.e. MAIN_PASS again for transparents)
            if ( doOITPass )
            {
                params._stagePass._passType = RenderPassType::OIT_PASS;
                woitPass( params, camSnapshot, bufferInOut, memCmdInOut );
            }
            else
            {
                params._stagePass._passType = RenderPassType::TRANSPARENCY_PASS;
                transparencyPass( params, camSnapshot, bufferInOut, memCmdInOut );
            }
        }
#   pragma endregion

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }

    U32 RenderPassExecutor::renderQueueSize() const
    {
        return to_U32( _renderQueuePackages.size() );
    }

    void RenderPassExecutor::OnRenderingComponentCreation( RenderingComponent* rComp )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        bool resizeBuffers = false;
        U32 indirectionIDX = Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry(rComp);
        DIVIDE_ASSERT(indirectionIDX == NodeIndirectionData::INVALID_IDX);

        {
            LockGuard<SharedMutex> w_lock(s_indirectionBuffer._data._freeListLock);
            for ( size_t i = 0u; i < s_indirectionBuffer._data._freeList.size(); ++i )
            {
                if (s_indirectionBuffer._data._freeList[i] )
                {
                    s_indirectionBuffer._data._freeList[i] = false;
                    indirectionIDX = to_U32(i);
                    break;
                }
            }
            // Cache miss
            if (indirectionIDX == NodeIndirectionData::INVALID_IDX)
            {
                indirectionIDX = to_U32(s_indirectionBuffer._data._freeList.size());
                s_indirectionBuffer._data._freeList.emplace_back(false);
                s_indirectionBuffer._data._gpuData.emplace_back();
                resizeBuffers = true;
            }
        }

       Attorney::RenderingCompRenderPassExecutor::setIndirectionBufferEntry( rComp, indirectionIDX);

       U32 transformIDX = Attorney::RenderingCompRenderPassExecutor::getIndirectionNodeData(rComp)._transformIDX;

        DIVIDE_ASSERT(transformIDX == NodeIndirectionData::INVALID_IDX);
        {
            LockGuard<SharedMutex> w_lock(s_transformBuffer._data._freeListLock);
            for (size_t i = 0u; i < s_transformBuffer._data._freeList.size(); ++i)
            {
                if (s_transformBuffer._data._freeList[i])
                {
                    s_transformBuffer._data._freeList[i] = false;
                    transformIDX = to_U32(i);
                    break;
                }
            }
            // Cache miss
            if (transformIDX == NodeIndirectionData::INVALID_IDX)
            {
                transformIDX = to_U32(s_transformBuffer._data._freeList.size());
                s_transformBuffer._data._freeList.emplace_back(false);
                s_transformBuffer._data._gpuData.emplace_back();
                resizeBuffers = true;
            }
        }

        s_indirectionBuffer._data._gpuData[indirectionIDX]._transformIDX = transformIDX;
        UpdateBufferRange(s_indirectionBuffer._updateRange, s_indirectionBuffer._rangeLock, indirectionIDX);

        Attorney::RenderingCompRenderPassExecutor::setTransformIDX(rComp, transformIDX);
        UpdateBufferRange(s_transformBuffer._updateRange, s_transformBuffer._rangeLock, transformIDX);

        if ( resizeBuffers )
        {
            s_resizeBufferQueued = true;
        }
    }

    void RenderPassExecutor::OnRenderingComponentDestruction( RenderingComponent* rComp )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        const U32 indirectionIDX = Attorney::RenderingCompRenderPassExecutor::getIndirectionBufferEntry( rComp );
        DIVIDE_ASSERT (indirectionIDX != NodeIndirectionData::INVALID_IDX);
        {
            UpdateBufferRange(s_indirectionBuffer._updateRange, s_indirectionBuffer._rangeLock, indirectionIDX);

            LockGuard<SharedMutex> w_lock(s_indirectionBuffer._data._freeListLock);
            DIVIDE_ASSERT( !s_indirectionBuffer._data._freeList[indirectionIDX] );
            s_indirectionBuffer._data._freeList[indirectionIDX] = true;
            s_indirectionBuffer._data._gpuData[indirectionIDX] = {};
        }
        Attorney::RenderingCompRenderPassExecutor::setIndirectionBufferEntry(rComp, NodeIndirectionData::INVALID_IDX);

        const NodeIndirectionData& data = Attorney::RenderingCompRenderPassExecutor::getIndirectionNodeData(rComp);
        DIVIDE_ASSERT( data._transformIDX != NodeIndirectionData::INVALID_IDX );
        {
            UpdateBufferRange(s_transformBuffer._updateRange, s_transformBuffer._rangeLock, data._transformIDX);

            LockGuard<SharedMutex> w_lock(s_transformBuffer._data._freeListLock);
            s_transformBuffer._data._freeList[data._transformIDX] = true;
            s_transformBuffer._data._gpuData[data._transformIDX] = {};
            Attorney::RenderingCompRenderPassExecutor::setTransformIDX(rComp, NodeIndirectionData::INVALID_IDX);
        }
    }

} //namespace Divide
