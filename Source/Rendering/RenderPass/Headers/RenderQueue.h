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
#ifndef DVD_RENDER_QUEUE_H_
#define DVD_RENDER_QUEUE_H_

#include "RenderBin.h"
#include "Core/Headers/KernelComponent.h"
#include "Platform/Video/Headers/RenderStagePass.h"

namespace Divide {

class SceneNode;
struct RenderSubPassCmd;

FWD_DECLARE_MANAGED_CLASS(Material);
FWD_DECLARE_MANAGED_CLASS(SceneNode);

/// This class manages all of the RenderBins and renders them in the correct order
class RenderQueue final : public KernelComponent {
  public: 
    using RenderBinArray = std::array<RenderBin, to_base(RenderBinType::COUNT)>;

    struct PopulateQueueParams {
        RenderStagePass _stagePass;
        /// One bit per RenderBinType
        U16 _binMask{ 0u };

        [[nodiscard]] static constexpr U16 BinBit( const RenderBinType binType ) noexcept
        {
            return to_U16( 1u << to_base( binType ) );
        }

        static constexpr U16 ALL_BINS = to_U16( ( 1u << to_base( RenderBinType::COUNT ) ) - 1u );

        [[nodiscard]] static constexpr PopulateQueueParams AllBins( const RenderStagePass stagePass ) noexcept
        {
            return { stagePass, ALL_BINS };
        }

        [[nodiscard]] static constexpr PopulateQueueParams ForPass( const RenderStagePass stagePass, const RenderBinContent content ) noexcept
        {
            if ( IsShadowPass( stagePass ) )
            {
                return AllBins( stagePass );
            }

            if ( content == RenderBinContent::ALPHA_TESTED )
            {
                return { stagePass, BinBit( RenderBinType::ALPHA_TESTED ) };
            }
            if ( content == RenderBinContent::TRANSLUCENT )
            {
                return { stagePass, BinBit( RenderBinType::TRANSLUCENT ) };
            }

            return { stagePass, to_U16( ALL_BINS & ~( BinBit( RenderBinType::ALPHA_TESTED ) | BinBit( RenderBinType::TRANSLUCENT ) ) ) };
        }

        [[nodiscard]] constexpr bool includes( const RenderBinType binType ) const noexcept
        {
            return binType != RenderBinType::COUNT && ( _binMask & BinBit( binType ) ) != 0u;
        }

        [[nodiscard]] static constexpr bool RequiresMaterialForPreparation( const RenderStagePass stagePass,
                                                                            const RenderBinType binType,
                                                                            const bool drawState ) noexcept
        {
            // Every visible node prepares its main pass draw. The alpha-tested pass reuses it.
            if ( stagePass._passType == RenderPassType::MAIN_PASS )
            {
                return true;
            }

            const bool transparencyPass = stagePass._passType == RenderPassType::OIT_PASS ||
                                          stagePass._passType == RenderPassType::TRANSPARENCY_PASS;
            return drawState && ForPass( stagePass, transparencyPass ? RenderBinContent::TRANSLUCENT : RenderBinContent::OPAQUE ).includes( binType );
        }
    };

  public:
    explicit RenderQueue(Kernel& parent, RenderStage stage);

    [[nodiscard]] static RenderBinType getBinForNode(const SceneGraphNode* node, Handle<Material> matInstance);
    void populateRenderQueues(const PopulateQueueParams& params, RenderQueuePackages& queueInOut);

    /// Only the bins drawn by the pass are visited: other bins may still hold nodes from an earlier pass.
    void postRender(const SceneRenderState& renderState, const PopulateQueueParams& params, GFX::CommandBuffer& bufferInOut);
    void sort(RenderStagePass stagePass, RenderBinType targetBinType = RenderBinType::COUNT, RenderingOrder renderOrder = RenderingOrder::COUNT);
    void clear(RenderBinType targetBinType = RenderBinType::COUNT) noexcept;
    void addNodeToQueue(const SceneGraphNode* sgn,
                        RenderStagePass stagePass,
                        F32 minDistToCameraSq,
                        RenderBinType targetBinType = RenderBinType::COUNT,
                        RenderBinType nodeBinType = RenderBinType::COUNT);

    [[nodiscard]] const RenderBin& getBin(const RenderBinType rbType) const noexcept { return _renderBins[to_base(rbType)]; }
    [[nodiscard]] RenderBinArray& getBins() noexcept { return _renderBins; }

    size_t getSortedQueues(const vector<RenderBinType>& binTypes, RenderBin::SortedQueues& queuesOut) const;

  private:

    [[nodiscard]] RenderingOrder getSortOrder(RenderStagePass stagePass, RenderBinType rbType) const;

    [[nodiscard]] FORCE_INLINE RenderBin& getBin(const RenderBinType rbType) noexcept { return _renderBins[to_base(rbType)]; }
  private:
    const RenderStage _stage;
    RenderBinArray _renderBins;
};

}  // namespace Divide

#endif //DVD_RENDER_QUEUE_H_
