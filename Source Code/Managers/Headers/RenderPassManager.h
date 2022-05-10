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
#ifndef _MANAGERS_RENDER_PASS_MANAGER_H_
#define _MANAGERS_RENDER_PASS_MANAGER_H_

#include "Core/Headers/KernelComponent.h"

#include "Rendering/RenderPass/Headers/RenderPass.h"
#include "Rendering/RenderPass/Headers/RenderQueue.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Rendering/RenderPass/Headers/RenderPassExecutor.h"

namespace Divide {
struct NodeTransformData;
struct NodeMaterialData;
struct PerPassData;
struct RTDrawDescriptor;

class Camera;
class SceneGraph;
class RenderTarget;
class SceneEnvironmentProbePool;

enum class RenderStage : U8;

struct RenderPassParams
{
    enum class Flags : U8 {
        DRAW_STATIC_NODES = 0,
        DRAW_DYNAMIC_NODES,
        DRAW_SKY_NODES,
        COUNT
    };

    FrustumClipPlanes _clippingPlanes{};
    vec3<F32> _minExtents = { 0.0f };
    // source node is used to determine if the current pass is triggered by a specific node:
    // e.g. a light node for shadow mapping, a reflector for reflection (or refraction), etc
    // safe to be set to null
    FeedBackContainer* _feedBackContainer = nullptr;
    const SceneGraphNode* _sourceNode = nullptr;
    Str64 _passName = "";
    I32 _maxLoD = -1; //-1 = all

    RTDrawDescriptor _targetDescriptorPrePass = {};
    RTDrawDescriptor _targetDescriptorMainPass = {};
    RTDrawDescriptor _targetDescriptorComposition= {};

    RenderTargetID _target = INVALID_RENDER_TARGET_ID;
    RenderTargetID _targetHIZ = INVALID_RENDER_TARGET_ID;
    RenderTargetID _targetOIT = INVALID_RENDER_TARGET_ID;
    RenderStagePass _stagePass = {};

    RenderTarget::DrawLayerParams _layerParams = {};

    U8 _drawMask = 1 << to_base(Flags::DRAW_DYNAMIC_NODES) |
                   1 << to_base(Flags::DRAW_STATIC_NODES);
};

class RenderPassManager final : public KernelComponent {

public:
    // Just an upper cap for containers. Increasing it will not break anything
    static constexpr U32 MAX_RENDER_PASSES = 16u;

    struct RenderParams {
        SceneRenderState* _sceneRenderState = nullptr;
        Rect<I32> _targetViewport = {};
        Time::ProfileTimer* _parentTimer = nullptr;
        PlayerIndex _playerPass = 0u;
        bool _editorRunning = false;
    };

    explicit RenderPassManager(Kernel& parent, GFXDevice& context);
    ~RenderPassManager();

    /// Call every render queue's render function in order
    void render(const RenderParams& params);

    /// Add a new pass that will run once for each of the RenderStages specified
    RenderPass& setRenderPass(RenderStage renderStage,  const vector<RenderStage>& dependencies = {});

    [[nodiscard]] U32 getLastTotalBinSize(RenderStage renderStage) const noexcept;
    [[nodiscard]] I32 drawCallCount(const RenderStage stage) const noexcept { return _drawCallCount[to_base(stage)]; }

    void doCustomPass(Camera* camera, RenderPassParams params, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);
    void postInit();

private:
    void startRenderTasks(const RenderParams& params, TaskPool& pool, const CameraSnapshot& cameraSnapshot);

private:
    friend class RenderPassExecutor;
    [[nodiscard]] const RenderPass& getPassForStage(RenderStage renderStage) const noexcept;

private:
    GFXDevice& _context;

    ShaderProgram_ptr _OITCompositionShader = nullptr;
    ShaderProgram_ptr _OITCompositionShaderMS = nullptr;
    ShaderProgram_ptr _gbufferResolveShader = nullptr;

    struct RenderPassData {
        Task* _workTask{ nullptr };
        RenderPass* _pass{nullptr};
        GFX::CommandBuffer* _cmdBuffer{ nullptr };
        GFX::MemoryBarrierCommand _memCmd{};
    };
    std::array<RenderPassData, MAX_RENDER_PASSES> _renderPassData{};

    Task* _postFXWorkTask{ nullptr };
    GFX::CommandBuffer* _postFXCmdBuffer{ nullptr };

    std::array<eastl::unique_ptr<RenderPassExecutor>, to_base(RenderStage::COUNT)> _executors;
    std::array<Time::ProfileTimer*, to_base(RenderStage::COUNT)> _processCommandBufferTimer = create_array<to_base(RenderStage::COUNT), Time::ProfileTimer*>(nullptr);
    std::array<I32, to_base(RenderStage::COUNT)> _drawCallCount = create_array<to_base(RenderStage::COUNT), I32>(0);

    GFX::CommandBuffer* _postRenderBuffer = nullptr;
    GFX::CommandBuffer* _skyLightRenderBuffer = nullptr;

    Time::ProfileTimer* _renderPassTimer = nullptr;
    Time::ProfileTimer* _buildCommandBufferTimer = nullptr;
    Time::ProfileTimer* _processGUITimer = nullptr;
    Time::ProfileTimer* _flushCommandBufferTimer = nullptr;
    Time::ProfileTimer* _postFxRenderTimer = nullptr;
    Time::ProfileTimer* _blitToDisplayTimer = nullptr;
};

} // namespace Divide

#endif
