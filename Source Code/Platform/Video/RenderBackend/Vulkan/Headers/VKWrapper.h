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
#include "vkIMPrimitive.h"

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

    VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
};

struct VKStateTracker {
    VKDevice* _device = nullptr;

    std::array < std::pair<Str64, U32>, 32 > _debugScope;
    U8 _debugScopeDepth = 0u;
};

class VK_API final : public RenderAPIWrapper {
   private:
    using IMPrimitivePool = MemoryPool<vkIMPrimitive, 1 << 15>;

  public:
    VK_API(GFXDevice& context) noexcept;

    [[nodiscard]] VkCommandBuffer getCurrentCommandBuffer() const;

    static [[nodiscard]] IMPrimitive* NewIMP(Mutex& lock, GFXDevice& parent);
    static [[nodiscard]] bool DestroyIMP(Mutex& lock, IMPrimitive*& primitive);

  protected:
      void idle(bool fast) noexcept override;
      [[nodiscard]] bool beginFrame(DisplayWindow& window, bool global = false) noexcept override;
      void endFrame(DisplayWindow& window, bool global = false) noexcept override;

      [[nodiscard]] ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) noexcept override;
      void closeRenderingAPI() override;
      [[nodiscard]] const PerformanceMetrics& getPerformanceMetrics() const noexcept override;
      void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;
      void flushCommand(GFX::CommandBase* cmd) noexcept override;
      void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) noexcept override;
      [[nodiscard]] vec2<U16> getDrawableSize(const DisplayWindow& window) const noexcept override;
      [[nodiscard]] U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const noexcept override;
      [[nodiscard]] bool setViewport(const Rect<I32>& newViewport) noexcept override;
      void onThreadCreated(const std::thread::id& threadID) noexcept override;

private:
    void initPipelines();
    void destroyPipelines();

    void recreateSwapChain(const DisplayWindow& window);
    void drawText(const TextElementBatch& batch);
    void drawIMGUI(const ImDrawData* data, I64 windowGUID);
    bool draw(const GenericDrawCommand& cmd) const;

    //loads a shader module from a spir-v file. Returns false if it errors
    [[nodiscard]] bool loadShaderModule(const char* filePath, VkShaderModule* outShaderModule);

public:
    static VKStateTracker* GetStateTracker() noexcept;

private:
    static void InsertDebugMessage(VkCommandBuffer cmdBuffer, const char* message, U32 id = std::numeric_limits<U32>::max());
    static void PushDebugMessage(VkCommandBuffer cmdBuffer, const char* message, U32 id = std::numeric_limits<U32>::max());
    static void PopDebugMessage(VkCommandBuffer cmdBuffer);

private:
    GFXDevice& _context;

    eastl::unique_ptr<VKDevice> _device = nullptr;
    eastl::unique_ptr<VKSwapChain> _swapChain = nullptr;

    vkb::Instance _vkbInstance;

    VkDebugUtilsMessengerEXT _debugMessenger{ VK_NULL_HANDLE }; // Vulkan debug output handle

    VkSurfaceKHR _surface{ VK_NULL_HANDLE }; // Vulkan window surface

    VKDevice::Queue _graphicsQueue{};
    VKDevice::Queue _computeQueue{};
    VKDevice::Queue _transferQueue{};

    U32 _graphicsQueueFamily{0u};

    VkCommandPool _commandPool{ VK_NULL_HANDLE };
    vector<VkCommandBuffer> _commandBuffers{};
    U8 _currentFrameIndex{ 0u };

    VkPipelineLayout _trianglePipelineLayout{ VK_NULL_HANDLE };
    VkPipeline _trianglePipeline{VK_NULL_HANDLE};
    VkExtent2D _windowExtents{};

    bool _skipEndFrame{ false };

private:
    static eastl::unique_ptr<VKStateTracker> s_stateTracker;
    static bool s_hasDebugMarkerSupport;
    static IMPrimitivePool s_IMPrimitivePool;
};

};  // namespace Divide
#endif //_VK_WRAPPER_H_
