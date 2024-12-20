/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

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
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Rendering/RenderPass/Headers/RenderPassCuller.h"

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


        bool operator==(const BufferUpdateRange&) const = default;
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
        using DataContainer = eastl::fixed_vector<T, COUNT, true>;
        DataContainer _gpuData{};

        SharedMutex _freeListLock;
        eastl::fixed_vector<FREE_LIST_TYPE, COUNT, true> _freeList{};
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
        BufferUpdateRange _prevUpdateRange;

        Mutex _nodeProcessedLock;
        eastl::fixed_set<U32, Config::MAX_VISIBLE_NODES, true> _nodeProcessedThisFrame;

        ShaderBuffer_uptr _gpuBuffer{ nullptr };
    };

public:
    explicit RenderPassExecutor(RenderPassManager& parent, GFXDevice& context, RenderStage stage);

    void doCustomPass(PlayerIndex idx, Camera* camera, RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);
    static void PostInit(GFXDevice& context,
                         Handle<ShaderProgram> OITCompositionShader,
                         Handle<ShaderProgram> OITCompositionShaderMS,
                         Handle<ShaderProgram> ResolveGBufferShaderMS);

    static void ResizeGPUBuffers(GFXDevice& context, size_t indirectionCount, size_t transformCount, size_t materialCount);
    static void FlushBuffersToGPU(GFX::MemoryBarrierCommand& memCmdInOut);
    static void PrepareGPUBuffers(GFXDevice& context);
    static void OnStartup(GFXDevice& gfx);
    static void OnShutdown(GFXDevice& gfx);

private:
    struct ParseResult
    {
        bool _updateBuffer{false};
        bool _updateIndirection{false};
    };

    // Returns false if we skipped the pre-pass step
    void prePass(const RenderPassParams& params,
                 const CameraSnapshot& cameraSnapshot,
                 GFX::CommandBuffer& bufferInOut,
                 GFX::MemoryBarrierCommand& memCmdInOut);

    void occlusionPass(PlayerIndex idx, 
                       const CameraSnapshot& cameraSnapshot,
                       size_t visibleNodeCount,
                       const RenderTargetID& sourceDepthBuffer,
                       const RenderTargetID& targetHiZBuffer,
                       GFX::CommandBuffer& bufferInOut) const;

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

    size_t buildDrawCommands(PlayerIndex index, 
                             const RenderPassParams& params,
                             bool doPrePass,
                             bool doOITPass,
                             GFX::CommandBuffer& bufferInOut,
                             GFX::MemoryBarrierCommand& memCmdInOut);

    size_t prepareNodeData(PlayerIndex index, 
                           const RenderPassParams& params,
                           const CameraSnapshot& cameraSnapshot,
                           bool hasInvalidNodes,
                           const bool doPrePass,
                           const bool doOITPass,
                           GFX::CommandBuffer& bufferInOut,
                           GFX::MemoryBarrierCommand& memCmdInOut);

    [[nodiscard]] U32 renderQueueSize() const;

    void resolveMainScreenTarget(const RenderPassParams& params, GFX::CommandBuffer& bufferInOut) const;

    [[nodiscard]] bool validateNodesForStagePass(RenderStagePass stagePass);
    void parseTransformRange(RenderBin::SortedQueue& queue, U32 start, U32 end, const PlayerIndex index, D64 interpolationFactor);
    void parseMaterialRange(RenderBin::SortedQueue& queue, U32 start, U32 end);

private:
    friend class RenderingComponent;
    static void OnRenderingComponentCreation(RenderingComponent* rComp);
    static void OnRenderingComponentDestruction(RenderingComponent* rComp);

private:
    RenderPassManager& _parent;
    GFXDevice& _context;
    const RenderStage _stage;

    bool _lightsUploaded = false;

    RenderQueue_uptr _renderQueue{nullptr};
    RenderBin::SortedQueues _sortedQueues{};
    DrawCommandContainer _drawCommands{};
    RenderQueuePackages _renderQueuePackages{};

    ShaderBuffer_uptr _cmdBuffer;

    static ExecutorBuffer<BufferMaterialData>    s_materialBuffer;
    static ExecutorBuffer<BufferTransformData>   s_transformBuffer;
    static ExecutorBuffer<BufferIndirectionData> s_indirectionBuffer;

    VisibleNodeList<> _visibleNodesCache;

    static bool s_globalDataInit;
    static bool s_resizeBufferQueued;

    static Pipeline* s_OITCompositionPipeline;
    static Pipeline* s_OITCompositionMSPipeline;
    static Pipeline* s_ResolveGBufferPipeline;
};

FWD_DECLARE_MANAGED_CLASS(RenderPassExecutor);

} //namespace Divide

#endif //DVD_RENDER_PASS_EXECUTOR_H_
