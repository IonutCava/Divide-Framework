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

/*The system is similar to the one used in Torque3D (RenderPassMgr / RenderBinManager) as it was used as an inspiration.
  All credit goes to GarageGames for the idea:
  - http://garagegames.com/
  - https://github.com/GarageGames/Torque3D
  */

#pragma once
#ifndef DVD_RENDER_BIN_H_
#define DVD_RENDER_BIN_H_

namespace Divide {

struct Task;
class GFXDevice;
class SceneGraphNode;
class RenderingComponent;
struct RenderStagePass;

namespace GFX {
    class CommandBuffer;
};

struct RenderBinItem {
    RenderingComponent* _renderable{ nullptr };
    size_t _stateHash{ 0u };
    I64 _shaderKey{ I64_LOWEST };
    I64 _textureKey{ I64_LOWEST };
    F32 _distanceToCameraSq{ 0.f };
    bool _hasTransparency{ false };
};

enum class RenderingOrder : U8 {
    NONE = 0,
    FRONT_TO_BACK,
    FRONT_TO_BACK_ALPHA_LAST,
    BACK_TO_FRONT,
    BY_STATE,
    COUNT
};

//Bins can hold certain node types. This is also the order in which nodes will be rendered!
enum class RenderBinType : U8 {
    OPAQUE,      ///< Opaque objects will occlude a lot of the terrain and terrain is REALLY expensive to render, so maybe draw them first?
    WATER,       ///< Water might end up being as expensive as terrain, so these will probably need reshuffling
    TERRAIN,     ///< Actual terrain. It should cover most of the remaining empty screen space
    TERRAIN_AUX, ///< E.g. infinite ground plane
    SKY,         ///< Sky needs to be drawn after ALL opaque geometry to save on fillrate
    TRANSLUCENT, ///< Translucent items use a [0.0...1.0] alpha value supplied via an opacity map or via the albedo's alpha channel
    IMPOSTOR,    ///< Impostors should be overlayed over everything since they are a debugging tool
    COUNT
};

namespace Names {
    static const char* renderBinType[] = {
        "OPAQUE", "WATER", "TERRAIN", "TERRAIN_AUX", "SKY", "TRANSLUCENT", "IMPOSTOR", "UNKNOWN"
    };
};

static_assert(ArrayCount( Names::renderBinType ) == to_base( RenderBinType::COUNT ) + 1, "RenderBinType name array out of sync!");

struct RenderPackage;

class SceneRenderState;
class RenderPassManager;

enum class RenderQueueListType : U8 {
    OCCLUDERS = 0,
    OCCLUDEES,
    COUNT
};

class RenderBin;
struct RenderQueuePackage {
    RenderingComponent* _rComp = nullptr;
    RenderPackage* _rPackage = nullptr;
};

using RenderQueuePackages = eastl::fixed_vector<RenderQueuePackage, Config::MAX_VISIBLE_NODES, false>;

/// This class contains a list of "RenderBinItem"'s and stores them sorted depending on designation
class RenderBin {
   public:
    using RenderBinStack = eastl::array<RenderBinItem, Config::MAX_VISIBLE_NODES>;
    using SortedQueue = eastl::fixed_vector<RenderingComponent*, Config::MAX_VISIBLE_NODES, false>;
    using SortedQueues = std::array<SortedQueue, to_base(RenderBinType::COUNT)>;

    RenderBin() = default;

    void sort(RenderBinType type, RenderingOrder renderOrder);
    void populateRenderQueue(RenderStagePass stagePass, RenderQueuePackages& queueInOut) const;
    void postRender(const SceneRenderState& renderState, RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut);

    void addNodeToBin(const SceneGraphNode* sgn, RenderStagePass renderStagePass, F32 minDistToCameraSq);
    void clear() noexcept;

    [[nodiscard]]       U16            getSortedNodes(SortedQueue& nodes) const;
    [[nodiscard]]       U16            getBinSize()                       const noexcept;
    [[nodiscard]] const RenderBinItem& getItem(const U16 index)           const;

    [[nodiscard]] inline bool          empty()                  const noexcept { return getBinSize() == 0; }

   private:
    std::atomic_ushort _renderBinIndex;
    RenderBinStack _renderBinStack;
};

FWD_DECLARE_MANAGED_CLASS(RenderBin);

};  // namespace Divide

#endif //DVD_RENDER_BIN_H_
