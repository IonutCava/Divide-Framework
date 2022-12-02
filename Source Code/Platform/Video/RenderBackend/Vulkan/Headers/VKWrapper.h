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

#include "vkDevice.h"
#include "vkSwapChain.h"
#include "vkMemAllocatorInclude.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"

#include "vkDescriptors.h"

namespace vke
{
    FWD_DECLARE_MANAGED_CLASS( DescriptorAllocatorPool );
};

namespace Divide {

class Pipeline;
class vkShaderProgram;
enum class ShaderResult : U8;

struct VKDynamicState
{
    U32 _stencilRef{ 0u };
    U32 _stencilMask{ 0xFFFFFFFF };
    U32 _stencilWriteMask{ 0xFFFFFFFF };
    F32 _zBias{ 0.0f };
    F32 _zUnits{ 0.0f };
};

struct CompiledPipeline
{
    VkPipelineBindPoint _bindPoint{ VK_PIPELINE_BIND_POINT_MAX_ENUM };
    vkShaderProgram* _program{ nullptr };
    VKDynamicState _dynamicState{};
    VkPipeline _vkPipeline{ VK_NULL_HANDLE };
    VkPipeline _vkPipelineWireframe{ VK_NULL_HANDLE };
    VkPipelineLayout _vkPipelineLayout{ VK_NULL_HANDLE };
    PrimitiveTopology _topology{PrimitiveTopology::COUNT};
    VkShaderStageFlags _stageFlags{VK_FLAGS_NONE};
};

struct PipelineBuilder {
    std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
    VkViewport _viewport;
    VkRect2D _scissor;
    VkPipelineRasterizationStateCreateInfo _rasterizer;
    eastl::fixed_vector<VkPipelineColorBlendAttachmentState, to_base( RTColourAttachmentSlot::COUNT ), false> _colorBlendAttachments;
    VkPipelineMultisampleStateCreateInfo _multisampling;
    VkPipelineLayout _pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo _depthStencil;
    VkPipelineTessellationStateCreateInfo _tessellation;

    VkPipeline build_pipeline( VkDevice device, VkPipelineCache pipelineCache, bool graphics);

private:
    VkPipeline build_compute_pipeline(VkDevice device, VkPipelineCache pipelineCache);
    VkPipeline build_graphics_pipeline(VkDevice device, VkPipelineCache pipelineCache );
};

struct VkPipelineEntry {
    VkPipeline _pipeline{VK_NULL_HANDLE};
    VkPipelineLayout _layout{VK_NULL_HANDLE};
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
    VKSwapChain* _swapChain{ nullptr };
    VKImmediateCmdContext* _cmdContext{ nullptr };

    std::array<std::pair<Str256, U32>, 32> _debugScope;

    VMAAllocatorInstance _allocatorInstance{};
    vke::DescriptorAllocatorHandle _perDrawDescriptorAllocator;
    vke::DescriptorAllocatorHandle _perFrameDescriptorAllocator;
    CompiledPipeline _pipeline{};

    struct PipelineRenderInfo
    {
        VkPipelineRenderingCreateInfo _vkInfo{};
        size_t _hash{0u};
    } _pipelineRenderInfo;
    

    VkBuffer _drawIndirectBuffer{ VK_NULL_HANDLE };
    size_t _drawIndirectBufferOffset{ 0u };

    U64 _lastSyncedFrameNumber{ 0u };

    VkShaderStageFlags _pipelineStageMask{ VK_FLAGS_NONE };

    RenderTargetID _activeRenderTargetID{ INVALID_RENDER_TARGET_ID };
    U8 _activeMSAASamples{ 1u };
    U8 _debugScopeDepth = 0u;
    bool _descriptorsUpdated{false};
    bool _pushConstantsValid{false};
    void setDefaultState();
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

struct VKTransferQueue
{
    struct TransferRequest {
        VkDeviceSize srcOffset{0u};
        VkDeviceSize dstOffset{0u};
        VkDeviceSize size{0u};
        VkBuffer     srcBuffer{VK_NULL_HANDLE};
        VkBuffer     dstBuffer{VK_NULL_HANDLE};
        VkAccessFlags2 dstAccessMask{ VK_ACCESS_2_NONE };
        VkPipelineStageFlags2 dstStageMask{ VK_PIPELINE_STAGE_2_NONE };
        bool _immediate{ false };
    };

    mutable Mutex _lock;
    std::deque<TransferRequest> _requests;
};

class RenderStateBlock;
class VK_API final : public RenderAPIWrapper {
  public:
    VK_API(GFXDevice& context) noexcept;

    [[nodiscard]] VKDevice* getDevice() { return _device.get(); }

    [[nodiscard]] GFXDevice& context() noexcept { return _context; };
    [[nodiscard]] const GFXDevice& context() const noexcept { return _context; };

  protected:
      [[nodiscard]] VkCommandBuffer getCurrentCommandBuffer() const noexcept;

      void idle(bool fast) noexcept override;
      [[nodiscard]] bool beginFrame(DisplayWindow& window, bool global = false) noexcept override;
      void endFrame(DisplayWindow& window, bool global = false) noexcept override;

      [[nodiscard]] ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) noexcept override;
      void closeRenderingAPI() override;
      void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;
      void flushCommand(GFX::CommandBase* cmd) noexcept override;
      void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) noexcept override;
      [[nodiscard]] vec2<U16> getDrawableSize(const DisplayWindow& window) const noexcept override;
      bool setViewportInternal(const Rect<I32>& newViewport) noexcept override;
      bool setScissorInternal(const Rect<I32>& newScissor) noexcept override;

      void onThreadCreated(const std::thread::id& threadID) noexcept override;
      void initDescriptorSets() override;

private:
    void recreateSwapChain(const DisplayWindow& window);
    void drawText(const TextElementBatch& batch);
    bool draw(const GenericDrawCommand& cmd, VkCommandBuffer cmdBuffer) const;
    bool setViewportInternal( const Rect<I32>& newViewport, VkCommandBuffer cmdBuffer ) noexcept;
    bool setScissorInternal( const Rect<I32>& newScissor, VkCommandBuffer cmdBuffer ) noexcept;
    void destroyPipelineCache();

    ShaderResult bindPipeline(const Pipeline& pipeline, VkCommandBuffer cmdBuffer);
    void bindDynamicState(const VKDynamicState& currentState, VkCommandBuffer cmdBuffer) noexcept;
    [[nodiscard]] bool bindShaderResources(DescriptorSetUsage usage, const DescriptorSet& bindings, bool isDirty) override;

public:
    static VKStateTracker& GetStateTracker() noexcept;
    static void RegisterCustomAPIDelete(DELEGATE<void, VkDevice>&& cbk, bool isResourceTransient);
    static void RegisterTransferRequest(const VKTransferQueue::TransferRequest& request);
    static [[nodiscard]] VkSampler GetSamplerHandle(size_t samplerHash);

private:
    static void InsertDebugMessage(VkCommandBuffer cmdBuffer, const char* message, U32 id = U32_MAX);
    static void PushDebugMessage(VkCommandBuffer cmdBuffer, const char* message, U32 id = U32_MAX);
    static void PopDebugMessage(VkCommandBuffer cmdBuffer);

private:
    GFXDevice& _context;
    VmaAllocator _allocator{VK_NULL_HANDLE};
    VkPipelineCache _pipelineCache{ VK_NULL_HANDLE };
    VKDevice_uptr _device{ nullptr };
    VKSwapChain_uptr _swapChain{ nullptr };
    VKImmediateCmdContext_uptr _cmdContext{ nullptr };
    vkb::Instance _vkbInstance;
    hashMap<size_t, CompiledPipeline> _compiledPipelines;

    enum class DescriptorAllocatorUsage
    {
        PER_DRAW = 0u,
        PER_FRAME,
        COUNT
    };
    std::array<vke::DescriptorAllocatorPool_uptr, to_base(DescriptorAllocatorUsage::COUNT)> _descriptorAllocatorPools{};
    DescriptorLayoutCache_uptr _descriptorLayoutCache{nullptr};
    VkDebugUtilsMessengerEXT _debugMessenger{ VK_NULL_HANDLE }; // Vulkan debug output handle

    VkSurfaceKHR _surface{ VK_NULL_HANDLE }; // Vulkan window surface

    vector<VkCommandBuffer> _commandBuffers{};
    U8 _currentFrameIndex{ 0u };

    VkExtent2D _windowExtents{};
    bool _skipEndFrame{ false };

    VkRenderPassBeginInfo _defaultRenderPass;
    std::array<VkDescriptorSetLayout, to_base( DescriptorSetUsage::COUNT )> _descriptorSetLayouts;
    std::array<VkDescriptorSet, to_base(DescriptorSetUsage::COUNT)> _descriptorSets;

private:
    using SamplerObjectMap = hashMap<size_t, VkSampler, NoHash<size_t>>;

    static SharedMutex s_samplerMapLock;
    static SamplerObjectMap s_samplerMap;
    static VKStateTracker s_stateTracker;
    static VKDeletionQueue s_transientDeleteQueue;
    static VKDeletionQueue s_deviceDeleteQueue;
    static VKTransferQueue s_transferQueue;

public:
    struct DepthFormatInformation
    {
        bool _d24s8Supported{false};
        bool _d32s8Supported{false};
        bool _d24x8Supported{false};
        bool _d32FSupported{false};
    };

    static bool s_hasDebugMarkerSupport;
    static DepthFormatInformation s_depthFormatInformation;
};

};  // namespace Divide

#endif //_VK_WRAPPER_H_
