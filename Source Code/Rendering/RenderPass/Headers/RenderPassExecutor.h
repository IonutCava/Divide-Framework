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
struct RenderTargetID;
struct RenderStagePass;
struct RenderPassParams;

class RenderTarget;
class RenderPassManager;
class RenderingComponent;

namespace GFX {
    class CommandBuffer;
}

class RenderPassExecutor
{
public:
    struct MaterialUpdateRange
    {
        U16 _firstIDX = U16_MAX;
        U16 _lastIDX = 0u;

        [[nodiscard]] U16 range() const noexcept { return _lastIDX >= _firstIDX ? _lastIDX - _firstIDX + 1u : 0u; }

        void reset() noexcept {
            _firstIDX = U16_MAX;
            _lastIDX = 0u;
        }
    };

    struct PerRingEntryMaterialData
    {
        using LookupInfoContainer = vector<std::pair<size_t, U16>>;
        using MaterialDataContainer = vector<NodeMaterialData>;
        MaterialUpdateRange _matUpdateRange{};
        MaterialDataContainer _nodeMaterialData{};
        LookupInfoContainer _nodeMaterialLookupInfo{};
    };

public:
    explicit RenderPassExecutor(RenderPassManager& parent, GFXDevice& context, RenderStage stage);

    void doCustomPass(RenderPassParams params, GFX::CommandBuffer& bufferInOut);
    void postInit(const ShaderProgram_ptr& OITCompositionShader, 
                  const ShaderProgram_ptr& OITCompositionShaderMS,
                  const ShaderProgram_ptr& ResolveScreenTargetsShaderMS) const;

private:
    // Returns false if we skipped the pre-pass step
    void prePass(const VisibleNodeList<>& nodes,
                 const RenderPassParams& params,
                 GFX::CommandBuffer& bufferInOut);

    void occlusionPass(const VisibleNodeList<>& nodes,
                       U32 visibleNodeCount,
                       const RenderStagePass& stagePass,
                       const Camera& camera,
                       const RenderTargetID& sourceDepthBuffer,
                       const RenderTargetID& targetDepthBuffer,
                       GFX::CommandBuffer& bufferInOut) const;
    void mainPass(const VisibleNodeList<>& nodes,
                  const RenderPassParams& params,
                  RenderTarget& target,
                  bool prePassExecuted,
                  bool hasHiZ,
                  GFX::CommandBuffer& bufferInOut);

    void transparencyPass(const VisibleNodeList<>& nodes,
                          const RenderPassParams& params,
                          GFX::CommandBuffer& bufferInOut);

    void woitPass(const VisibleNodeList<>& nodes,
                  const RenderPassParams& params,
                  GFX::CommandBuffer& bufferInOut);


    void postRender(const RenderStagePass& stagePass,
                    const Camera& camera,
                    RenderQueue& renderQueue,
                    GFX::CommandBuffer& bufferInOut) const;

    void prepareRenderQueues(const RenderPassParams& params,
                             const VisibleNodeList<>& nodes,
                             bool transparencyPass,
                             RenderingOrder renderOrder = RenderingOrder::COUNT);

    void processVisibleNodeTransform(RenderingComponent* rComp,
                                     RenderStage stage,
                                     D64 interpolationFactor,
                                     U16 nodeIndex);

    U16 processVisibleNodeMaterial(RenderingComponent* rComp,
                                   U32 materialElementOffset);

    U16 buildDrawCommands(const RenderPassParams& params, bool doPrePass, bool doOITPass, GFX::CommandBuffer& bufferInOut);
    U16 prepareNodeData(VisibleNodeList<>& nodes, const RenderPassParams& params, bool hasInvalidNodes, const bool doPrePass, const bool doOITPass, GFX::CommandBuffer& bufferInOut);

    void renderQueueToSubPasses(GFX::CommandBuffer& commandsInOut,
                                RenderPackage::MinQuality qualityRequirement = RenderPackage::MinQuality::COUNT) const;

    [[nodiscard]] U32 renderQueueSize(RenderPackage::MinQuality qualityRequirement = RenderPackage::MinQuality::COUNT) const;

    void setMaterialInfoAt(size_t idx, PerRingEntryMaterialData::MaterialDataContainer& dataInOut, const NodeMaterialData& tempData, const NodeMaterialTextures& tempTextures);

    void resolveMainScreenTarget(const RenderPassParams& params, GFX::CommandBuffer& bufferInOut) const;
private:
    RenderPassManager& _parent;
    GFXDevice& _context;
    const RenderStage _stage;
    RenderQueue _renderQueue;
    RenderBin::SortedQueues _sortedQueues{};
    DrawCommandContainer _drawCommands{};
    RenderQueuePackages _renderQueuePackages{};

    U32 _materialBufferIndex = 0u;

    std::array<NodeTransformData, Config::MAX_VISIBLE_NODES> _nodeTransformData{};

    SharedMutex _matDataLock;
    eastl::fixed_vector<PerRingEntryMaterialData, 3, true> _materialData{};

    eastl::set<SamplerAddress> _uniqueTextureAddresses{};

    static Pipeline* s_OITCompositionPipeline;
    static Pipeline* s_OITCompositionMSPipeline;
    static Pipeline* s_ResolveScreenTargetsPipeline;
};
} //namespace Divide

#endif //_RENDER_PASS_EXECUTOR_H_