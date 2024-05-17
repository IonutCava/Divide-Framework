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
    static constexpr U16 g_invalidMaterialIndex = Config::MAX_CONCURRENT_MATERIALS;
    static constexpr U32 g_invalidTexturesIndex = g_invalidMaterialIndex;

    struct BufferUpdateRange
    {
        U32 _firstIDX{ U32_MAX };
        U32 _lastIDX{ 0u };

        U32 range() const noexcept;
        void reset() noexcept;


        bool operator==(const BufferUpdateRange&) const = default;
    };

    using NodeIndirectionData = vec4<U32>;
    static constexpr U8 TRANSFORM_IDX = 0u;
    static constexpr U8 MATERIAL_IDX = 1u;
    static constexpr U8 TEXTURES_IDX = 2u;
    static constexpr U8 SELECTION_FLAG = 3u;

    // 2Mb worth of data
    static constexpr U32 MAX_INDIRECTION_ENTRIES = (2 * 1024 * 1024) / sizeof(NodeIndirectionData);

    struct BufferMaterialData
    {
        static_assert(Config::MAX_CONCURRENT_MATERIALS <= U16_MAX);

        struct LookupInfo {
            size_t _hash{ INVALID_MAT_HASH };
            U16 _framesSinceLastUsed{ g_invalidMaterialIndex };
        };

        using LookupInfoContainer = std::array<LookupInfo, Config::MAX_CONCURRENT_MATERIALS>;
        using MaterialDataContainer = std::array<NodeMaterialData, Config::MAX_CONCURRENT_MATERIALS>;
        MaterialDataContainer _gpuData{};
        LookupInfoContainer _lookupInfo{};
    };

    struct BufferTransformData
    {
        using TransformDataContainer = std::array<NodeTransformData, Config::MAX_VISIBLE_NODES>;
        TransformDataContainer _gpuData{};

        Mutex _freeListlock;
        std::array<bool, Config::MAX_VISIBLE_NODES> _freeList{};
    };

    using BufferIndirectionData = std::array<NodeIndirectionData, MAX_INDIRECTION_ENTRIES>;
    
    struct ExecutorBufferRange
    {
        SharedMutex _lock;
        BufferUpdateRange _bufferUpdateRange;
        BufferUpdateRange _bufferUpdateRangePrev;
        vector<BufferUpdateRange> _bufferUpdateRangeHistory;
        U32 _highWaterMark{ 0u };
    };

    template<typename DataContainer>
    struct ExecutorBuffer
    {
        DataContainer _data;
        ExecutorBufferRange _range;

        SharedMutex _nodeProcessedLock;
        eastl::fixed_vector<U32, MAX_INDIRECTION_ENTRIES, false> _nodeProcessedThisFrame;

        ShaderBuffer_uptr _gpuBuffer{ nullptr };
        U32 _queueLength{ Config::MAX_FRAMES_IN_FLIGHT + 1u };
    };

public:
    explicit RenderPassExecutor(RenderPassManager& parent, GFXDevice& context, RenderStage stage);

    void doCustomPass(PlayerIndex idx, Camera* camera, RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);
    void postInit(Handle<ShaderProgram> OITCompositionShader, 
                  Handle<ShaderProgram> OITCompositionShaderMS,
                  Handle<ShaderProgram> ResolveGBufferShaderMS);

    void postRender();

    static void OnStartup(const GFXDevice& gfx);
    static void OnShutdown(const GFXDevice& gfx);

private:
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

    void processVisibleNodeTransform( PlayerIndex index, RenderingComponent* rComp );

    [[nodiscard]] U16 processVisibleNodeMaterial(RenderingComponent* rComp, bool& cacheHit);

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

    ExecutorBuffer<BufferMaterialData> _materialBuffer;
    ExecutorBuffer<BufferTransformData> _transformBuffer;
    ExecutorBuffer<BufferIndirectionData> _indirectionBuffer;
    VisibleNodeList<> _visibleNodesCache;

    static bool s_globalDataInit;
    static std::array<bool, MAX_INDIRECTION_ENTRIES> s_indirectionFreeList;
    static Mutex s_indirectionGlobalLock;
    static Pipeline* s_OITCompositionPipeline;
    static Pipeline* s_OITCompositionMSPipeline;
    static Pipeline* s_ResolveGBufferPipeline;
};

FWD_DECLARE_MANAGED_CLASS(RenderPassExecutor);

} //namespace Divide

#endif //DVD_RENDER_PASS_EXECUTOR_H_
