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
#ifndef _VK_WRAPPER_H_
#define _VK_WRAPPER_H_

#include "VKPlaceholderObjects.h"
#include "VKDevice.h"
#include "VKSwapChain.h"
#include "VMAInclude.h"

#include "Platform/Video/Headers/RenderAPIWrapper.h"

namespace Divide {

class PipelineBuilder {
public:

    std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
    VkViewport _viewport;
    VkRect2D _scissor;
    VkPipelineRasterizationStateCreateInfo _rasterizer;
    VkPipelineColorBlendAttachmentState _colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo _multisampling;
    VkPipelineLayout _pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo _depthStencil;
    VkPipelineTessellationStateCreateInfo _tessellation;
    VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
};

struct VkPipelineEntry {
    VkPipeline _pipeline{VK_NULL_HANDLE};
    VkPipelineLayout _layout{VK_NULL_HANDLE};
};

struct VKDynamicState {
    PROPERTY_RW(U32, stencilRef, 0u);
    PROPERTY_RW(U32, stencilMask, 0xFFFFFFFF);
    PROPERTY_RW(U32, stencilWriteMask, 0xFFFFFFFF);
    PROPERTY_RW(F32, zBias, 0.0f);
    PROPERTY_RW(F32, zUnits, 0.0f);
    VkRect2D _activeScissor{};
    VkViewport _activeViewport{};
};

struct VKImmediateCmdContext {
    explicit VKImmediateCmdContext(VKDevice& context);
    ~VKImmediateCmdContext();

    VkFence _submitFence{};
    VkCommandPool _commandPool;
    VkCommandBuffer _commandBuffer;
    void flushCommandBuffer(std::function<void(VkCommandBuffer cmd)>&& function);

private:
   VKDevice& _context;
   Mutex _submitLock;
};
FWD_DECLARE_MANAGED_STRUCT(VKImmediateCmdContext);

struct VMAAllocatorInstance {
    VmaAllocator* _allocator{ nullptr };
    Mutex _allocatorLock;
};

struct VKStateTracker {
    VKDevice* _device{ nullptr };
    VKImmediateCmdContext* _cmdContext{ nullptr };
    VMAAllocatorInstance _allocatorInstance{};
    std::array < std::pair<Str64, U32>, 32 > _debugScope;
    U8 _debugScopeDepth = 0u;
    PrimitiveTopology _activeTopology{ PrimitiveTopology::COUNT };
    U8 _activeMSAASamples{ 1u };
    bool _alphaToCoverage{ false };
    VKDynamicState _dynamicState{};
    DescriptorSet const* _perDrawSet{ nullptr };
};
FWD_DECLARE_MANAGED_STRUCT(VKStateTracker);

struct vkUserData : VDIUserData {
    VkCommandBuffer* _cmdBuffer = nullptr;
};

struct VKDeletionQueue
{
    enum class Flags : U8 {
        TREAT_AS_TRANSIENT = toBit(1),
        COUNT = 1
    };

    void push(DELEGATE<void, VkDevice>&& function);
    void flush(VkDevice device);
    [[nodiscard]] bool empty() const;

    mutable Mutex _deletionLock;
    std::deque<DELEGATE<void, VkDevice>> _deletionQueue;
    PROPERTY_RW(U32, flags, 0u);
};

class RenderStateBlock;
class VK_API final : public RenderAPIWrapper {
  public:
    VK_API(GFXDevice& context) noexcept;

    [[nodiscard]] VKDevice* getDevice() { return _device.get(); }

  protected:
      [[nodiscard]] VkCommandBuffer getCurrentCommandBuffer() const;

      void idle(bool fast) noexcept override;
      [[nodiscard]] bool beginFrame(DisplayWindow& window, bool global = false) noexcept override;
      void endFrame(DisplayWindow& window, bool global = false) noexcept override;

      [[nodiscard]] ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) noexcept override;
      void closeRenderingAPI() override;
      void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;
      void flushCommand(GFX::CommandBase* cmd) noexcept override;
      void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) noexcept override;
      [[nodiscard]] vec2<U16> getDrawableSize(const DisplayWindow& window) const noexcept override;
      [[nodiscard]] U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const noexcept override;
      bool setViewport(const Rect<I32>& newViewport) noexcept override;
      bool setScissor(const Rect<I32>& newScissor) noexcept;
      void onThreadCreated(const std::thread::id& threadID) noexcept override;
      void createSetLayout(DescriptorSetUsage usage, const DescriptorSet& set) override;

private:
    void initPipelines();

    void recreateSwapChain(const DisplayWindow& window);
    void drawText(const TextElementBatch& batch);
    bool draw(const GenericDrawCommand& cmd, VkCommandBuffer& cmdBuffer) const;

    //loads a shader module from a spir-v file. Returns false if it errors
    [[nodiscard]] bool loadShaderModule(const char* filePath, VkShaderModule* outShaderModule);

    ShaderResult bindPipeline(const Pipeline& pipeline, VkCommandBuffer& cmdBuffer) const;
    void bindDynamicState(const RenderStateBlock& currentState, VkCommandBuffer& cmdBuffer) const;

public:
    static const VKStateTracker_uptr& GetStateTracker() noexcept;
    static void RegisterCustomAPIDelete(DELEGATE<void, VkDevice>&& cbk, bool isResourceTransient);

private:
    static void InsertDebugMessage(VkCommandBuffer cmdBuffer, const char* message, U32 id = std::numeric_limits<U32>::max());
    static void PushDebugMessage(VkCommandBuffer cmdBuffer, const char* message, U32 id = std::numeric_limits<U32>::max());
    static void PopDebugMessage(VkCommandBuffer cmdBuffer);

private:
    GFXDevice& _context;
    VmaAllocator _allocator;

    VKDevice_uptr _device{ nullptr };
    VKSwapChain_uptr _swapChain{ nullptr };
    VKImmediateCmdContext_uptr _cmdContext{ nullptr };
    vkb::Instance _vkbInstance;

    VkDebugUtilsMessengerEXT _debugMessenger{ VK_NULL_HANDLE }; // Vulkan debug output handle

    VkSurfaceKHR _surface{ VK_NULL_HANDLE }; // Vulkan window surface

    vector<VkCommandBuffer> _commandBuffers{};
    U8 _currentFrameIndex{ 0u };

    VkPipelineEntry _trianglePipeline{};

    VkExtent2D _windowExtents{};
    bool _skipEndFrame{ false };

private:
    static VKStateTracker_uptr s_stateTracker;
    static bool s_hasDebugMarkerSupport;
    static VKDeletionQueue s_transientDeleteQueue;
    static VKDeletionQueue s_deviceDeleteQueue;
};

};  // namespace Divide

#endif //_VK_WRAPPER_H_
