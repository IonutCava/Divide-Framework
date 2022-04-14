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

class VK_API final : public RenderAPIWrapper {
  public:
    VK_API(GFXDevice& context) noexcept;

  protected:
      void idle(bool fast) noexcept override;
      void beginFrame(DisplayWindow& window, bool global = false) noexcept override;
      void endFrame(DisplayWindow& window, bool global = false) noexcept override;

      ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) noexcept override;
      void closeRenderingAPI() override;
      [[nodiscard]] const PerformanceMetrics& getPerformanceMetrics() const noexcept override;
      void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;
      void flushCommand(const GFX::CommandBuffer::CommandEntry& entry, const GFX::CommandBuffer& commandBuffer) noexcept override;
      void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) noexcept override;
      [[nodiscard]] vec2<U16> getDrawableSize(const DisplayWindow& window) const noexcept override;
      [[nodiscard]] U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const noexcept override;
      bool setViewport(const Rect<I32>& newViewport) noexcept override;
      void onThreadCreated(const std::thread::id& threadID) noexcept override;

private:
    void initSwapChains(const DisplayWindow& window);
    void initCommands();
    void initDefaultRenderpass();
    void initFramebuffers(const DisplayWindow& window);
    void initSyncStructures();
    void initPipelines();

    void recreateSwapChain(const DisplayWindow& window);
    void cleanupSwapChain();


    //loads a shader module from a spir-v file. Returns false if it errors
    [[nodiscard]] bool loadShaderModule(const char* filePath, VkShaderModule* outShaderModule);

private:
    GFXDevice& _context;

    VkInstance _instance{ nullptr }; // Vulkan library handle
    VkDebugUtilsMessengerEXT _debugMessenger{ nullptr }; // Vulkan debug output handle
    VkPhysicalDevice _chosenGPU{ nullptr }; // GPU chosen as the default device
    VkDevice _device{ nullptr }; // Vulkan device for commands
    VkSurfaceKHR _surface{ nullptr }; // Vulkan window surface
    VkSemaphore _presentSemaphore{ nullptr }, _renderSemaphore{ nullptr };
    VkFence _renderFence{ nullptr };

    VkQueue _graphicsQueue{ nullptr };
    U32 _graphicsQueueFamily{0u};

    VkCommandPool _commandPool{ nullptr };
    VkCommandBuffer _mainCommandBuffer{ nullptr };

    VkRenderPass _renderPass{nullptr};
    VkPipelineLayout _trianglePipelineLayout{ nullptr };
    VkPipeline _trianglePipeline{nullptr};
    VkSwapchainKHR _swapchain{ nullptr };
    VkFormat _swapchainImageFormat;
    VkExtent2D _windowExtents{};
    vector<VkFramebuffer> _framebuffers;
    vector<VkImage> _swapchainImages;
    vector<VkImageView> _swapchainImageViews;

    U32 _swapchainImageIndex{0u};
    bool _skipEndFrame{ false };
};

};  // namespace Divide
#endif //_VK_WRAPPER_H_
