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
#ifndef _RENDER_PASS_EXECUTOR_H_
#define _RENDER_PASS_EXECUTOR_H_

#include "NodeBufferedData.h"
#include "RenderPass.h"
#include "RenderQueue.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Rendering/RenderPass/Headers/RenderPassCuller.h"

namespace Divide {
struct PerPassData;
struct RenderStagePass;
struct RenderPassParams;

class RenderTarget;
class RenderPassManager;
class RenderingComponent;

namespace GFX {
    class CommandBuffer;
}

FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

class RenderPassExecutor
{
public:
    static constexpr size_t INVALID_MAT_HASH = std::numeric_limits<size_t>::max();
    static constexpr size_t INVALID_TEX_HASH = std::numeric_limits<size_t>::max();
    static constexpr U16 g_invalidMaterialIndex = Config::MAX_CONCURRENT_MATERIALS;
    static constexpr U32 g_invalidTexturesIndex = g_invalidMaterialIndex;

    struct BufferUpdateRange
    {
        U32 _firstIDX = U32_MAX;
        U32 _lastIDX = 0u;

        [[nodiscard]] U32 range() const noexcept {
            return _lastIDX >= _firstIDX ? _lastIDX - _firstIDX + 1u : 0u;
        }

        void reset() noexcept {
            _firstIDX = U32_MAX;
            _lastIDX = 0u;
        }
    };

    using NodeIndirectionData = vec4<U32>;
    static constexpr U8 TRANSFORM_IDX = 0u;
    static constexpr U8 MATERIAL_IDX = 1u;
    static constexpr U8 TEXTURES_IDX = 2u;

    // 2Mb worth of data
    static constexpr U32 MAX_INDIRECTION_ENTRIES = (2 * 1024 * 1024) / sizeof(NodeIndirectionData);

    struct BufferMaterialData
    {
        static_assert(Config::MAX_CONCURRENT_MATERIALS <= U16_MAX);

        struct LookupInfo {
            size_t _hash = INVALID_MAT_HASH;
            U16 _framesSinceLastUsed = g_invalidMaterialIndex;
        };

        using FlagContainer = std::array<std::atomic_bool, Config::MAX_VISIBLE_NODES>;
        using LookupInfoContainer = std::array<LookupInfo, Config::MAX_CONCURRENT_MATERIALS>;
        using MaterialDataContainer = std::array<NodeMaterialData, Config::MAX_CONCURRENT_MATERIALS>;
        MaterialDataContainer _gpuData{};
        LookupInfoContainer _lookupInfo{};
    };

    struct BufferTexturesData
    {
        struct LookupInfo {
            size_t _hash = INVALID_TEX_HASH;
            U32 _framesSinceLastUsed = g_invalidTexturesIndex;
        };
        using FlagContainer = std::array<std::atomic_bool, Config::MAX_VISIBLE_NODES>;
        using LookupInfoContainer = std::array<LookupInfo, Config::MAX_VISIBLE_NODES>;
        using TexturesDataContainer = std::array<NodeMaterialTextures, Config::MAX_VISIBLE_NODES>;
        TexturesDataContainer _gpuData{};
        LookupInfoContainer _lookupInfo{};

    };
    struct BufferTransformData
    {
        using FlagContainer = std::array<std::atomic_bool, Config::MAX_VISIBLE_NODES>;
        using TransformDataContainer = std::array<NodeTransformData, Config::MAX_VISIBLE_NODES>;
        TransformDataContainer _gpuData{};
        std::array<bool, Config::MAX_VISIBLE_NODES> _freeList{};
    };

    struct BufferIndirectionData {
        std::array<NodeIndirectionData, MAX_INDIRECTION_ENTRIES> _gpuData;
    };
    
    template<typename DataContainer>
    struct ExecutorBuffer {
        U32 _highWaterMark = 0u;
        ShaderBuffer_uptr _gpuBuffer = nullptr;
        Mutex _lock;
        DataContainer _data;
        BufferUpdateRange _bufferUpdateRange;
        BufferUpdateRange _bufferUpdateRangePrev;
        vector<BufferUpdateRange> _bufferUpdateRangeHistory;

        SharedMutex _proccessedLock;
        eastl::fixed_set<U32, MAX_INDIRECTION_ENTRIES, false> _nodeProcessedThisFrame;
    };

public:
    explicit RenderPassExecutor(RenderPassManager& parent, GFXDevice& context, RenderStage stage);

    void doCustomPass(PlayerIndex idx, Camera* camera, RenderPassParams params, GFX::CommandBuffer& bufferInOut);
    void postInit(const ShaderProgram_ptr& OITCompositionShader, 
                  const ShaderProgram_ptr& OITCompositionShaderMS,
                  const ShaderProgram_ptr& ResolveGBufferShaderMS);

    void postRender();

    static void OnStartup(const GFXDevice& gfx);
    static void OnShutdown(const GFXDevice& gfx);

private:
    ShaderBuffer* getCommandBufferForStagePass(RenderStagePass stagePass);

    // Returns false if we skipped the pre-pass step
    void prePass(const VisibleNodeList<>& nodes,
                 const RenderPassParams& params,
                 const CameraSnapshot& cameraSnapshot,
                 GFX::CommandBuffer& bufferInOut);

    void occlusionPass(PlayerIndex idx, 
                       const VisibleNodeList<>& nodes,
                       const CameraSnapshot& cameraSnapshot,
                       U32 visibleNodeCount,
                       RenderStagePass stagePass,
                       const RenderTargetID& sourceDepthBuffer,
                       const RenderTargetID& targetDepthBuffer,
                       GFX::CommandBuffer& bufferInOut) const;
    void mainPass(const VisibleNodeList<>& nodes,
                  const RenderPassParams& params,
                  const CameraSnapshot& cameraSnapshot,
                  RenderTarget& target,
                  bool prePassExecuted,
                  bool hasHiZ,
                  GFX::CommandBuffer& bufferInOut);

    void transparencyPass(const VisibleNodeList<>& nodes,
                          const RenderPassParams& params,
                          const CameraSnapshot& cameraSnapshot,
                          GFX::CommandBuffer& bufferInOut);

    void woitPass(const VisibleNodeList<>& nodes,
                  const RenderPassParams& params,
                  const CameraSnapshot& cameraSnapshot,
                  GFX::CommandBuffer& bufferInOut);

    void prepareRenderQueues(const RenderPassParams& params,
                             const VisibleNodeList<>& nodes,
                             const CameraSnapshot& cameraSnapshot,
                             bool transparencyPass,
                             RenderingOrder renderOrder,
                             GFX::CommandBuffer& bufferInOut);

    void processVisibleNodeTransform(RenderingComponent* rComp,
                                     D64 interpolationFactor);
    
    [[nodiscard]] U32 processVisibleNodeTextures(RenderingComponent* rComp, bool& cacheHit);
    [[nodiscard]] U16 processVisibleNodeMaterial(RenderingComponent* rComp, bool& cacheHit);

    U16 buildDrawCommands(const RenderPassParams& params, bool doPrePass, bool doOITPass, GFX::CommandBuffer& bufferInOut);
    U16 prepareNodeData(VisibleNodeList<>& nodes,
                        const RenderPassParams& params,
                        const CameraSnapshot& cameraSnapshot,
                        bool hasInvalidNodes,
                        const bool doPrePass,
                        const bool doOITPass,
                        GFX::CommandBuffer& bufferInOut);

    [[nodiscard]] U32 renderQueueSize() const;

    void resolveMainScreenTarget(const RenderPassParams& params,
                                 bool resolveDepth,
                                 bool resolveGBuffer,
                                 bool resolveColourBuffer,
                                 GFX::CommandBuffer& bufferInOut) const;

    [[nodiscard]] bool validateNodesForStagePass(VisibleNodeList<>& nodes, RenderStagePass stagePass);
    void parseTextureRange(RenderBin::SortedQueue& queue, const U32 start, const U32 end);
    void parseMaterialRange(RenderBin::SortedQueue& queue, U32 start, U32 end);

private:
    friend class RenderingComponent;
    static void OnRenderingComponentCreation(RenderingComponent* rComp);
    static void OnRenderingComponentDestruction(RenderingComponent* rComp);

private:
    RenderPassManager& _parent;
    GFXDevice& _context;
    const RenderStage _stage;
    RenderQueue _renderQueue;
    RenderBin::SortedQueues _sortedQueues{};
    DrawCommandContainer _drawCommands{};
    RenderQueuePackages _renderQueuePackages{};

    vector<ShaderBuffer_uptr> _cmdBuffers;

    eastl::set<SamplerAddress> _uniqueTextureAddresses{};

    ExecutorBuffer<BufferMaterialData> _materialBuffer;
    ExecutorBuffer<BufferTexturesData> _texturesBuffer;
    ExecutorBuffer<BufferTransformData> _transformBuffer;
    ExecutorBuffer<BufferIndirectionData> _indirectionBuffer;

    static bool s_globalDataInit;
    static std::array<bool, MAX_INDIRECTION_ENTRIES> s_indirectionFreeList;
    static Mutex s_indirectionGlobalLock;
    static SamplerAddress s_defaultTextureSamplerAddress;
    static Pipeline* s_OITCompositionPipeline;
    static Pipeline* s_OITCompositionMSPipeline;
    static Pipeline* s_ResolveGBufferPipeline;
};
} //namespace Divide

#endif //_RENDER_PASS_EXECUTOR_H_