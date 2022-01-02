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
#ifndef _RENDER_BIN_H_
#define _RENDER_BIN_H_

#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"

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
    RenderingComponent* _renderable = nullptr;
    size_t _stateHash = 0u;
    I64 _shaderKey = std::numeric_limits<I64>::lowest();
    I32 _textureKey = std::numeric_limits<I32>::lowest();
    F32 _distanceToCameraSq = 0.f;
};

enum class RenderingOrder : U8 {
    NONE = 0,
    FRONT_TO_BACK,
    BACK_TO_FRONT,
    BY_STATE,
    WATER_FIRST, //Hack, but improves rendering perf :D
    COUNT
};

//Bins can hold certain node types. This is also the order in which nodes will be rendered!
enum class RenderBinType : U8 {
    OPAQUE,      ///< Opaque objects will occlude a lot of the terrain and terrain is REALLY expensive to render, so maybe draw them first?
    TERRAIN_AUX, ///< Water, infinite ground plane, etc. Ground, but not exactly ground. Still oclude a lot of terrain AND cheaper to render
    TERRAIN,     ///< Actual terrain. It should cover most of the remaining empty screen space
    SKY,         ///< Sky needs to be drawn after ALL opaque geometry to save on fillrate
    TRANSLUCENT, ///< Translucent items use a [0.0...1.0] alpha values supplied via an opacity map or the albedo's alpha channel
    IMPOSTOR,    ///< Impostors should be overlayed over everything since they are a debugging tool
    COUNT
};
namespace Names {
    static const char* renderBinType[] = {
        "OPAQUE", "TERRAIN_AUX", "TERRAIN", "SKY", "TRANSLUCENT", "IMPOSTOR", "UNKNOWN"
    };
};

static_assert(ArrayCount(Names::renderBinType) == to_base(RenderBinType::COUNT) + 1, "RenderBinType name array out of sync!");

struct RenderPackage;

class SceneRenderState;
class RenderPassManager;

enum class RenderQueueListType : U8 {
    OCCLUDERS = 0,
    OCCLUDEES,
    COUNT
};

class RenderBin;
using RenderQueuePackages = vector_fast<std::pair<RenderingComponent*, RenderPackage*>>;

/// This class contains a list of "RenderBinItem"'s and stores them sorted depending on designation
class RenderBin {
   public:
    using RenderBinStack = eastl::array<RenderBinItem, Config::MAX_VISIBLE_NODES>;
    using SortedQueue = vector<RenderingComponent*>;
    using SortedQueues = std::array<SortedQueue, to_base(RenderBinType::COUNT)>;

    friend class RenderQueue;

    explicit RenderBin(RenderBinType rbType, RenderStage stage);

    void sort(RenderingOrder renderOrder);
    void populateRenderQueue(RenderStagePass stagePass, RenderQueuePackages& queueInOut) const;
    void postRender(const SceneRenderState& renderState, RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut);

    void addNodeToBin(const SceneGraphNode* sgn, RenderStagePass renderStagePass, F32 minDistToCameraSq);

    [[nodiscard]] U16 getSortedNodes(SortedQueue& nodes) const;

    FORCE_INLINE               void                 refresh()                      noexcept { _renderBinIndex.store(0u); }
    FORCE_INLINE [[nodiscard]] const RenderBinItem& getItem(const U16 index) const          { assert(index < getBinSize()); return _renderBinStack[index]; }
    FORCE_INLINE [[nodiscard]] U16                  getBinSize()             const noexcept { return _renderBinIndex.load(); }
    FORCE_INLINE [[nodiscard]] bool                 empty()                  const noexcept { return getBinSize() == 0; }
    FORCE_INLINE [[nodiscard]] RenderBinType        getType()                const noexcept { return _rbType; }

   private:
    const RenderBinType _rbType;
    const RenderStage _stage;

    RenderBinStack _renderBinStack{};
    std::atomic_ushort _renderBinIndex;
};

};  // namespace Divide
#endif