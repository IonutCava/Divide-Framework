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
#ifndef DVD_RENDERING_RENDER_PASS_RENDERPASS_H_
#define DVD_RENDERING_RENDER_PASS_RENDERPASS_H_

#include "Platform/Video/Headers/PushConstants.h"

namespace Divide {

namespace Time {
    class ProfileTimer;
}

namespace GFX {
    class CommandBuffer;
    struct MemoryBarrierCommand;
}

struct Task;
struct RenderStagePass;
struct Configuration;

class GFXDevice;
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
       struct PassData
       {
           U32* _lastCommandCount = nullptr;
           U32* _lastNodeCount = nullptr;
           UniformData* _uniforms = nullptr;
       };

  public:
    // passStageFlags: the first stage specified will determine the data format used by the additional stages in the list
    explicit RenderPass(RenderPassManager& parent, GFXDevice& context, RenderStage renderStage, const vector<RenderStage>& dependencies);
    ~RenderPass() = default;

    void render(PlayerIndex idx, const Task& parentTask, const SceneRenderState& renderState, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) const;

    [[nodiscard]] inline U32 getLastTotalBinSize() const noexcept { return _lastNodeCount; }
    [[nodiscard]] inline const Str<64>& name() const noexcept { return _name; }

    [[nodiscard]] inline RenderStage stageFlag() const noexcept { return _stageFlag; }

    PassData getPassData() const noexcept;

    PROPERTY_RW(vector<RenderStage>, dependencies);

   private:
    GFXDevice& _context;
    RenderPassManager& _parent;
    Configuration& _config;

    mutable UniformData _uniforms;
    mutable U32 _lastCmdCount = 0u;
    mutable U32 _lastNodeCount = 0u;

    Str<64> _name = "";
    U32 _transformIndexOffset = 0u;
    RenderStage _stageFlag = RenderStage::COUNT;
};

}  // namespace Divide

#endif //DVD_RENDERING_RENDER_PASS_RENDERPASS_H_
