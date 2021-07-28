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
#ifndef _RENDERING_RENDER_PASS_RENDERPASS_H_
#define _RENDERING_RENDER_PASS_RENDERPASS_H_

#include "Platform/Video/Headers/RenderAPIWrapper.h"

namespace Divide {

namespace Time {
    class ProfileTimer;
}

struct Task;
struct RenderStagePass;

class SceneGraph;
class ShaderBuffer;
class SceneRenderState;
class RenderPassManager;
enum class RenderStage : U8;

// A RenderPass may contain multiple linked stages.
// Useful to avoid having multiple RenderQueues per pass if 2 stages depend on one:
// E.g.: PRE_PASS + MAIN_PASS share the same RenderQueue
class RenderPass final : NonCopyable {
   public:
       struct BufferData {
           ShaderBuffer* _transformBuffer = nullptr;
           ShaderBuffer* _materialBuffer = nullptr;
           ShaderBuffer* _commandBuffer = nullptr;
           ShaderBuffer* _cullCounterBuffer = nullptr;
           U32* _lastCommandCount = nullptr;
           U32* _lastNodeCount = nullptr;
           U32 _transformElementOffset = 0u;
           U32 _materialElementOffset = 0u;
           U32 _commandElementOffset = 0u;
       };

  public:
    // passStageFlags: the first stage specified will determine the data format used by the additional stages in the list
    explicit RenderPass(RenderPassManager& parent, GFXDevice& context, Str64 name, U8 sortKey, RenderStage passStageFlag, const vectorEASTL<U8>& dependencies, bool performanceCounters = false);

    void render(const Task& parentTask, const SceneRenderState& renderState, GFX::CommandBuffer& bufferInOut) const;
    void postRender() const;

    [[nodiscard]] inline U8 sortKey() const noexcept { return _sortKey; }
    [[nodiscard]] inline const vectorEASTL<U8>& dependencies() const noexcept { return _dependencies; }
    [[nodiscard]] inline U32 getLastTotalBinSize() const noexcept { return _lastNodeCount; }
    [[nodiscard]] inline const Str64& name() const noexcept { return _name; }

    [[nodiscard]] inline RenderStage stageFlag() const noexcept { return _stageFlag; }

    BufferData getBufferData(const RenderStagePass& stagePass) const;

    void initBufferData();

   private:
    GFXDevice& _context;
    RenderPassManager& _parent;
    Configuration& _config;

    mutable U32 _lastCmdCount = 0u;
    mutable U32 _lastNodeCount = 0u;

    U8 _sortKey = 0;
    vectorEASTL<U8> _dependencies{};
    Str64 _name = "";
    RenderStage _stageFlag = RenderStage::COUNT;

    ShaderBuffer* _transformData = nullptr;
    ShaderBuffer* _materialData = nullptr;
    ShaderBuffer* _cullCounter = nullptr;
    ShaderBuffer* _cmdBuffer = nullptr;

    const bool _performanceCounters;
};

}  // namespace Divide

#endif
