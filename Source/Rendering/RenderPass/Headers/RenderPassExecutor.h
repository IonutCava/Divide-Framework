/*
   Copyright (c) 2018-2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_RENDER_PASS_EXECUTOR_H_
#define DVD_RENDER_PASS_EXECUTOR_H_

#include "NodeBufferedData.h"
#include "RenderPass.h"
#include "RenderBin.h"
#include "Core/Headers/RingBuffer.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/UniformDataSnapshots.h"
#include "Rendering/RenderPass/Headers/RenderPassCuller.h"
#include "Rendering/Lighting/Headers/LightUploadState.h"

namespace Divide {
struct PerPassData;
struct RenderStagePass;
struct RenderPassParams;

class RenderTarget;
class ShaderProgram;
class RenderPassManager;
class RenderingComponent;

namespace GFX {
    class CommandBuffer;
}

FWD_DECLARE_MANAGED_CLASS(RenderQueue);
FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

class RenderPassExecutor
{
public:
    static constexpr size_t INVALID_MAT_HASH = SIZE_MAX;
    static constexpr size_t INVALID_TEX_HASH = SIZE_MAX;

    struct BufferUpdateRange
    {
        U32 _firstIDX{ U32_MAX };
        U32 _lastIDX{ 0u };

        U32 range() const noexcept;
        void reset() noexcept;
        void alignStart( size_t elementSize, size_t alignment ) noexcept;

        bool operator==(const BufferUpdateRange&) const = default;
    };

    [[nodiscard]] static BufferUpdateRange TakeBufferUpdateRange( BufferUpdateRange& pending,
                                                                  std::span<BufferUpdateRange> slots,
                                                                  size_t slotIndex ) noexcept;
    [[nodiscard]] static U16 BufferQueueLengthForPlayers( U8 playerCount ) noexcept;

    class IndirectCommandBufferTracker
    {
    public:
        static constexpr U16 QUEUE_LENGTH = Config::MAX_FRAMES_IN_FLIGHT + U16_ONE;

        template<typename RecordCommands>
        [[nodiscard]] static U32 CollectCommands( const U16 passIndex, DrawCommandContainer& commands, RecordCommands&& record )
        {
            commands.clear();
            const U32 regularOffset = Config::MAX_VISIBLE_NODES * passIndex;
            record( regularOffset );
            if ( commands.size() <= Config::MAX_VISIBLE_NODES )
            {
                return regularOffset;
            }

            const size_t count = commands.size();
            commands.clear();
            record( 0u );
            if ( commands.size() != count )
            {
                DIVIDE_UNEXPECTED_CALL_MSG( "Indirect commands changed during preparation" );
            }
            return 0u;
        }

        [[nodiscard]] static bool TryOverflowCapacity( const U32 commandCount,
                                                       const size_t alignment,
                                                       const size_t maxBytes,
                                                       U32& capacityOut ) noexcept
        {
            if ( commandCount == 0u || alignment == 0u )
            {
                return false;
            }

            size_t common = alignment;
            size_t divisor = sizeof( IndirectIndexedDrawCommand );
            while ( divisor != 0u )
            {
                const size_t remainder = common % divisor;
                common = divisor;
                divisor = remainder;
            }

            const size_t elementAlignment = alignment / common;
            const size_t remainder = to_size( commandCount ) % elementAlignment;
            const size_t padding = remainder == 0u ? 0u : elementAlignment - remainder;
            if ( padding > to_size( U32_MAX ) - commandCount )
            {
                return false;
            }
            const size_t capacity = to_size( commandCount ) + padding;
            if ( capacity > maxBytes / sizeof( IndirectIndexedDrawCommand ) )
            {
                return false;
            }
            capacityOut = to_U32( capacity );
            return true;
        }

        struct Allocation
        {
            size_t _pageIndex;
            U16 _queueIndex;
            bool _overflow;
        };

        void beginFrame() noexcept
        {
            _passUses.fill( 0u );
            _overflowUses = 0u;
            _slots.incQueue();
            _frameStarted = true;
        }

        [[nodiscard]] Allocation acquire( const U16 passIndex, const U32 commandCount ) noexcept
        {
            if ( !_frameStarted || passIndex >= _passUses.size() )
            {
                DIVIDE_UNEXPECTED_CALL_MSG( "Invalid indirect command pass or missing frame preparation" );
            }
            const bool overflow = commandCount > Config::MAX_VISIBLE_NODES;
            return { overflow ? _overflowUses++ : _passUses[passIndex]++, _slots.queueIndex(), overflow };
        }

    private:
        RingBuffer _slots{ QUEUE_LENGTH };
        std::array<size_t, U8_MAX + 1u> _passUses{};
        size_t _overflowUses = 0u;
        bool _frameStarted = false;
    };

    struct MaterialLookupInfo
    {
        // Remove materials that haven't been indexed in this amount of frames to make space for new ones
        static constexpr size_t MAX_FRAME_LIFETIME = 6u;

        size_t _hash{ INVALID_MAT_HASH };
        size_t _framesSinceLastUsed{ MAX_FRAME_LIFETIME };
    };

    template<typename T, size_t COUNT, typename FREE_LIST_TYPE>
    struct PerNodeData
    {
        using DataContainer = fixed_vector<T, COUNT, true>;
        DataContainer _gpuData{};

        SharedMutex _freeListLock;
        fixed_vector<FREE_LIST_TYPE, COUNT, true> _freeList{};
    };

    using BufferTransformData = PerNodeData<NodeTransformData, Config::MAX_VISIBLE_NODES, bool>;
    using BufferIndirectionData = PerNodeData<NodeIndirectionData, Config::MAX_VISIBLE_NODES, bool>;
    using BufferMaterialData = PerNodeData<NodeMaterialData, Config::MAX_CONCURRENT_MATERIALS, MaterialLookupInfo>;

    template<typename DataContainer>
    struct ExecutorBuffer
    {
        DataContainer _data;

        SharedMutex _rangeLock;
        BufferUpdateRange _updateRange;
        std::array<BufferUpdateRange, (Config::MAX_FRAMES_IN_FLIGHT + 1u) * Config::MAX_LOCAL_PLAYER_COUNT> _slotUpdateRanges{};

        Mutex _nodeProcessedLock;
        eastl::fixed_set<U32, Config::MAX_VISIBLE_NODES, true> _nodeProcessedThisFrame;

        ShaderBuffer_uptr _gpuBuffer{ nullptr };
    };

public:
    enum class OITCompositionVariant : U8
    {
        COLOUR,
        COLOUR_MSAA,
        COLOUR_NORMALS,
        COLOUR_NORMALS_MSAA,
        COUNT
    };

    using OITCompositionShaders = std::array<Handle<ShaderProgram>, to_base( OITCompositionVariant::COUNT )>;

    [[nodiscard]] static constexpr OITCompositionVariant SelectOITCompositionVariant( const bool writeNormals, const bool useMSAA ) noexcept
    {
        if ( writeNormals )
        {
            return useMSAA ? OITCompositionVariant::COLOUR_NORMALS_MSAA : OITCompositionVariant::COLOUR_NORMALS;
        }
        return useMSAA ? OITCompositionVariant::COLOUR_MSAA : OITCompositionVariant::COLOUR;
    }

    [[nodiscard]] static constexpr bool UsesScreenSpaceReflections( const RenderStage stage, const bool enabled ) noexcept
    {
        return stage == RenderStage::DISPLAY && enabled;
    }

    template<typename Owner, typename Package, typename Callback>
    static void VisitPreparationOwners( vector<std::pair<Owner*, Package*>>& owners, Callback&& callback )
    {
        std::sort( owners.begin(), owners.end(), []( const auto& lhs, const auto& rhs )
        {
            return std::less<Owner*>{}( lhs.first, rhs.first );
        } );
        for ( size_t i = 0u; i < owners.size(); )
        {
            Owner* owner = owners[i].first;
            Package* prepared = nullptr;
            do
            {
                if ( owners[i].second != nullptr )
                {
                    prepared = owners[i].second;
                }
                ++i;
            }
            while ( i < owners.size() && owners[i].first == owner );
            callback( *owner, prepared );
        }
    }

    explicit RenderPassExecutor(RenderPassManager& parent, GFXDevice& context, RenderStage stage, U8 lane = 0u, U8 laneCount = 1u);

    void doCustomPass(PlayerIndex idx, Camera* camera, RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);
    void doCustomPass(PlayerIndex idx, const CameraSnapshot& camera, const Frustum& frustum, RenderPassParams params,
                      LightDataBinding lightData, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut,
                      bool independentView = true);
    [[nodiscard]] RenderPass::PassData passData() noexcept
    {
        return { &_lastCommandCount, &_lastNodeCount, &_uniforms };
    }
    static void PostInit(GFXDevice& context,
                         const OITCompositionShaders& OITShaders,
                         Handle<ShaderProgram> ResolveGBufferShaderMS);

    static void ResizeGPUBuffers(GFXDevice& context, size_t indirectionCount, size_t transformCount, size_t materialCount, U16 ringLength);
    static void FlushBuffersToGPU(GFX::MemoryBarrierCommand& memCmdInOut);
    static void PrepareGPUBuffers(GFXDevice& context, U8 playerCount);
    void prepareCommandBuffers(PlayerIndex index);
    static void OnStartup(GFXDevice& gfx);
    static void OnShutdown(GFXDevice& gfx);

private:
    struct ParseResult
    {
        bool _updateBuffer{false};
        bool _updateIndirection{false};
    };

    struct PreparedNodeData
    {
        size_t _visibleNodeCount;
        ShaderBuffer* _commandBuffer;
    };

    // Returns false if we skipped the pre-pass step
    void prePass(const RenderPassParams& params,
                 const CameraSnapshot& cameraSnapshot,
                 GFX::CommandBuffer& bufferInOut,
                 GFX::MemoryBarrierCommand& memCmdInOut);

    void occlusionPass(ShaderBuffer& commandBuffer,
                       const CameraSnapshot& cameraSnapshot,
                       size_t visibleNodeCount,
                       const RenderTargetID& sourceDepthBuffer,
                       const RenderTargetID& targetHiZBuffer,
                       GFX::CommandBuffer& bufferInOut);

    void mainPass(const RenderPassParams& params,
                  const CameraSnapshot& cameraSnapshot,
                  RenderTarget& target,
                  bool prePassExecuted,
                  bool hasHiZ,
                  GFX::CommandBuffer& bufferInOut,
                  GFX::MemoryBarrierCommand& memCmdInOut);

    void transparencyPass(const RenderPassParams& params,
                          const CameraSnapshot& cameraSnapshot,
                          GFX::CommandBuffer& bufferInOut,
                          GFX::MemoryBarrierCommand& memCmdInOut );

    void woitPass(const RenderPassParams& params,
                  const CameraSnapshot& cameraSnapshot,
                  GFX::CommandBuffer& bufferInOut,
                  GFX::MemoryBarrierCommand& memCmdInOut );

    void prepareRenderQueues(const RenderPassParams& params,
                             const CameraSnapshot& cameraSnapshot,
                             bool transparencyPass,
                             RenderingOrder renderOrder,
                             GFX::CommandBuffer& bufferInOut,
                             GFX::MemoryBarrierCommand& memCmdInOut);

    ParseResult processVisibleNodeTransform(RenderingComponent* rComp, D64 interpolationFactor, U32& transformIDXOut);

    // Returns true on a cache hit
    ParseResult processVisibleNodeMaterial( RenderingComponent* rComp, U32& materialIDXOut, U32& indirectionIDXOut );

    PreparedNodeData buildDrawCommands(PlayerIndex index,
                                       const RenderPassParams& params,
                                       bool doPrePass,
                                       bool doOITPass,
                                       GFX::CommandBuffer& bufferInOut,
                                       GFX::MemoryBarrierCommand& memCmdInOut);

    PreparedNodeData prepareNodeData(PlayerIndex index,
                                     const RenderPassParams& params,
                                     const CameraSnapshot& cameraSnapshot,
                                     bool hasInvalidNodes,
                                     const bool doPrePass,
                                     const bool doOITPass,
                                     GFX::CommandBuffer& bufferInOut,
                                     GFX::MemoryBarrierCommand& memCmdInOut);

    [[nodiscard]] U32 renderQueueSize() const;

    void resolveMainScreenTarget(const RenderPassParams& params, GFX::CommandBuffer& bufferInOut) const;

    [[nodiscard]] bool validateNodesForStagePass(RenderStagePass stagePass, bool* pendingShaderOut);
    void parseTransformRange(RenderBin::SortedQueue& queue, U32 start, U32 end, const PlayerIndex index, D64 interpolationFactor);
    void parseMaterialRange(RenderBin::SortedQueue& queue, U32 start, U32 end);
    [[nodiscard]] ShaderBuffer_uptr createCommandBuffer(PlayerIndex index, size_t pageIndex, U32 overflowCapacity = 0u) const;
    /// Appends instance-count patch records after the draw commands. Returns the total number of entries to upload.
    [[nodiscard]] U32 appendInstanceCountRecords(U32 cmdCount);
    /// Clamps the current pass's draw instance counts to the GPU-written counts of their sources.
    void patchInstanceCounts(ShaderBuffer& commandBuffer, GFX::CommandBuffer& bufferInOut);

private:
    friend class RenderingComponent;
    static void OnRenderingComponentCreation(RenderingComponent* rComp);
    static void OnRenderingComponentDestruction(RenderingComponent* rComp);

private:
    RenderPassManager& _parent;
    GFXDevice& _context;
    const RenderStage _stage;
    const U8 _lane;
    const U8 _laneCount;

    bool _lightsUploaded = false;
    U32 _lastCommandCount = 0u;
    U32 _lastNodeCount = 0u;
    UniformData _uniforms;

    RenderQueue_uptr _renderQueue{nullptr};
    LightDataBinding _lightDataBinding{};
    UniformDataSnapshots _occlusionSnapshots;
    RenderBin::SortedQueues _sortedQueues{};
    DrawCommandContainer _drawCommands{};
    RenderQueuePackages _renderQueuePackages{};
    vector<std::pair<RenderingComponent*, RenderPackage*>> _preparationOwners{};
    vector<RenderPackage*> _preparedPackages{};
    BufferLocks _computeBatchRanges{};

    struct InstanceCountPatch
    {
        ShaderBuffer* _source{ nullptr };
        U32 _commandIndex{ 0u };
        U32 _element{ 0u };
    };
    struct InstanceCountPatchRange
    {
        ShaderBuffer* _source{ nullptr };
        U32 _recordOffset{ 0u };
        U32 _recordCount{ 0u };
    };
    vector<InstanceCountPatch> _instanceCountPatches{};
    vector<InstanceCountPatchRange> _instanceCountPatchRanges{};
    bool _instanceCountPatchOverflowLogged{ false };

    struct PlayerCommandBuffers
    {
        IndirectCommandBufferTracker _tracker;
        vector<ShaderBuffer_uptr> _pages;
        vector<ShaderBuffer_uptr> _overflowPages;
    };
    std::array<PlayerCommandBuffers, Config::MAX_LOCAL_PLAYER_COUNT> _commandBuffers;

    static ExecutorBuffer<BufferMaterialData>    s_materialBuffer;
    static ExecutorBuffer<BufferTransformData>   s_transformBuffer;
    static ExecutorBuffer<BufferIndirectionData> s_indirectionBuffer;

    VisibleNodeList<> _visibleNodesCache;

    static bool s_globalDataInit;
    static bool s_resizeBufferQueued;

    static std::array<Pipeline*, to_base( OITCompositionVariant::COUNT )> s_OITCompositionPipelines;
    static Pipeline* s_ResolveGBufferPipeline;
};

FWD_DECLARE_MANAGED_CLASS(RenderPassExecutor);

} //namespace Divide

#endif //DVD_RENDER_PASS_EXECUTOR_H_
