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
#ifndef _RENDER_QUEUE_H_
#define _RENDER_QUEUE_H_

#include "RenderBin.h"
#include "Core/Headers/KernelComponent.h"

namespace Divide {

class SceneNode;
struct RenderSubPassCmd;

FWD_DECLARE_MANAGED_CLASS(Material);
FWD_DECLARE_MANAGED_CLASS(SceneNode);

/// This class manages all of the RenderBins and renders them in the correct order
class RenderQueue final : public KernelComponent {
  public: 
    using RenderBinArray = std::array<eastl::unique_ptr<RenderBin>, to_base(RenderBinType::COUNT)>;

  public:
    explicit RenderQueue(Kernel& parent, RenderStage stage);

    //binAndFlag: if true, populate from bin, if false, populate from everything except bin
    void populateRenderQueues(RenderStagePass stagePass, std::pair<RenderBinType, bool> binAndFlag, RenderQueuePackages& queueInOut);

    void postRender(const SceneRenderState& renderState, RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut);
    void sort(RenderStagePass stagePass, RenderBinType targetBinType = RenderBinType::COUNT, RenderingOrder renderOrder = RenderingOrder::COUNT);
    void refresh(RenderBinType targetBinType = RenderBinType::COUNT) noexcept;
    void addNodeToQueue(const SceneGraphNode* sgn, RenderStagePass stagePass, F32 minDistToCameraSq, RenderBinType targetBinType = RenderBinType::COUNT);
    [[nodiscard]] U16 getRenderQueueStackSize() const noexcept;

    [[nodiscard]] RenderBin* getBin(const U16 renderBin) const noexcept { return _renderBins[renderBin].get(); }
    [[nodiscard]] RenderBin* getBin(const RenderBinType rbType) const noexcept { return getBin(to_base(rbType)); }

    [[nodiscard]] RenderBinArray& getBins() noexcept {
        return _renderBins;
    }

    U16 getSortedQueues(const vector<RenderBinType>& binTypes, RenderBin::SortedQueues& queuesOut) const;

  private:

    [[nodiscard]] RenderingOrder getSortOrder(RenderStagePass stagePass, RenderBinType rbType) const;

    [[nodiscard]] RenderBin* getBinForNode(const SceneGraphNode* node, const Material_ptr& matInstance);

  private:
    const RenderStage _stage;
    RenderBinArray _renderBins = {};
};

}  // namespace Divide

#endif
