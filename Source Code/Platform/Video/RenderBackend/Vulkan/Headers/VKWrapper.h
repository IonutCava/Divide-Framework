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

#include "Platform/Video/Headers/CommandsImpl.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"

#include "vkDescriptors.h"

namespace vke
{
    FWD_DECLARE_MANAGED_CLASS( DescriptorAllocatorPool );
};

struct FONScontext;

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
    VkDescriptorSetLayout* _descriptorSetlayout{nullptr};
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
    VkPipeline build_graphics_pipeline(VkDevice device, VkPipelineCache pipelineCache);
};

struct VkPipelineEntry
{
    VkPipeline _pipeline{VK_NULL_HANDLE};
    VkPipelineLayout _layout{VK_NULL_HANDLE};
};

struct VKImmediateCmdContext
{
    static constexpr U8 BUFFER_COUNT = 4u;

    explicit VKImmediateCmdContext(VKDevice& context);
    ~VKImmediateCmdContext();

    void flushCommandBuffer(std::function<void(VkCommandBuffer cmd)>&& function, const char* scopeName);

private:

   VKDevice& _context;
   VkCommandPool _commandPool;
   Mutex _submitLock;

   std::array<VkFence, BUFFER_COUNT> _bufferFences;
   std::array<VkCommandBuffer, BUFFER_COUNT> _commandBuffers;

   U8 _bufferIndex{0u};
};

FWD_DECLARE_MANAGED_STRUCT(VKImmediateCmdContext);

struct VMAAllocatorInstance {
    VmaAllocator* _allocator{ nullptr };
    Mutex _allocatorLock;
};

struct DescriptorAllocator
{
    U8 _frameCount{1u};
    vke::DescriptorAllocatorHandle _handle{};
    vke::DescriptorAllocatorPool_uptr _allocatorPool{nullptr};
};

struct VKPerWindowState
{
    DisplayWindow* _window{nullptr};
    VKSwapChain_uptr _swapChain{ nullptr };
    VkSurfaceKHR _surface{ VK_NULL_HANDLE }; // Vulkan window surface

    VkExtent2D _windowExtents{};
    bool _skipEndFrame{ false };
};

struct VKStateTracker {
    VKDevice* _device{ nullptr };
    VKPerWindowState* _activeWindow{ nullptr };
    
    std::array<std::pair<Str256, U32>, 32> _debugScope;

    VMAAllocatorInstance _allocatorInstance{};
    std::array<DescriptorAllocator, to_base(DescriptorSetUsage::COUNT)> _descriptorAllocators;
    CompiledPipeline _pipeline{};

    VkPipelineRenderingCreateInfo _pipelineRenderInfo{};

    VkBuffer _drawIndirectBuffer{ VK_NULL_HANDLE };
    size_t _drawIndirectBufferOffset{ 0u };

    U64 _lastSyncedFrameNumber{ 0u };

    VkShaderStageFlags _pipelineStageMask{ VK_FLAGS_NONE };

    RenderTargetID _activeRenderTargetID{ INVALID_RENDER_TARGET_ID };
    vec2<U16> _activeRenderTargetDimensions{1u};

    U8 _activeMSAASamples{ 1u };
    U8 _debugScopeDepth { 0u };

    VKImmediateCmdContext_uptr _cmdContext{ nullptr };

    bool _descriptorsUpdated{false};
    bool _pushConstantsValid{false};
    bool _assertOnAPIError{false};

    void setDefaultState();

};

FWD_DECLARE_MANAGED_STRUCT(VKStateTracker);

struct vkUserData : VDIUserData {
    VkCommandBuffer* _cmdBuffer = nullptr;
};

struct VKDeletionQueue
{
    using QueuedItem = DELEGATE<void, VkDevice>;

    enum class Flags : U8 {
        TREAT_AS_TRANSIENT = toBit(1),
        COUNT = 1
    };

    void push( QueuedItem&& function );
    void flush(VkDevice device, bool force = false);
    void onFrameEnd();

    [[nodiscard]] bool empty() const;

    mutable Mutex _deletionLock;
    std::deque<std::pair<QueuedItem, U8>> _deletionQueue;
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
    };

    mutable Mutex _lock;
    std::deque<TransferRequest> _requests;
    std::atomic_bool _dirty;
};

class RenderStateBlock;
class VK_API final : public RenderAPIWrapper {
  public:
  constexpr static VkPipelineStageFlagBits2 ALL_SHADER_STAGES = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                                VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
                                                                VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
                                                                VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
                                                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  public:
 
    VK_API(GFXDevice& context) noexcept;

    [[nodiscard]] VKDevice* getDevice() { return _device.get(); }

    [[nodiscard]] GFXDevice& context() noexcept { return _context; };
    [[nodiscard]] const GFXDevice& context() const noexcept { return _context; };

  protected:
      [[nodiscard]] VkCommandBuffer getCurrentCommandBuffer() const noexcept;

      void idle(bool fast) noexcept override;

      [[nodiscard]] bool drawToWindow( DisplayWindow& window ) override;
                    void flushWindow( DisplayWindow& window ) override;
      [[nodiscard]] bool frameStarted() override;
      [[nodiscard]] bool frameEnded() override;

      [[nodiscard]] ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) noexcept override;
      void closeRenderingAPI() override;
      void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) override;
      void flushCommand(GFX::CommandBase* cmd) noexcept override;
      void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) noexcept override;
      bool setViewportInternal(const Rect<I32>& newViewport) noexcept override;
      bool setScissorInternal(const Rect<I32>& newScissor) noexcept override;

      void onThreadCreated(const std::thread::id& threadID) noexcept override;
      void initDescriptorSets() override;

private:
    void initStatePerWindow( VKPerWindowState& windowState );
    void destroyStatePerWindow( VKPerWindowState& windowState );
    void recreateSwapChain( VKPerWindowState& windowState );

    bool draw(const GenericDrawCommand& cmd, VkCommandBuffer cmdBuffer) const;
    bool setViewportInternal( const Rect<I32>& newViewport, VkCommandBuffer cmdBuffer ) noexcept;
    bool setScissorInternal( const Rect<I32>& newScissor, VkCommandBuffer cmdBuffer ) noexcept;
    void destroyPipelineCache();
    void flushPushConstantsLocks();
    VkDescriptorSetLayout createLayoutFromBindings( const DescriptorSetUsage usage, const ShaderProgram::BindingsPerSetArray& bindings );

    ShaderResult bindPipeline(const Pipeline& pipeline, VkCommandBuffer cmdBuffer);
    void bindDynamicState(const VKDynamicState& currentState, VkCommandBuffer cmdBuffer) noexcept;
    [[nodiscard]] bool bindShaderResources(DescriptorSetUsage usage, const DescriptorSet& bindings, bool isDirty) override;

public:
    static [[nodiscard]] VKStateTracker& GetStateTracker() noexcept;
    static [[nodiscard]] VkSampler GetSamplerHandle(size_t samplerHash);

    static void RegisterCustomAPIDelete(DELEGATE<void, VkDevice>&& cbk, bool isResourceTransient);
    static void RegisterTransferRequest(const VKTransferQueue::TransferRequest& request);
    static void FlushBufferTransferRequests( VkCommandBuffer cmdBuffer );
    static void FlushBufferTransferRequests( );
    static void SubmitTransferRequest(const VKTransferQueue::TransferRequest& request, VkCommandBuffer cmd);

    static void InsertDebugMessage(VkCommandBuffer cmdBuffer, const char* message, U32 id = U32_MAX);
    static void PushDebugMessage(VkCommandBuffer cmdBuffer, const char* message, U32 id = U32_MAX);
    static void PopDebugMessage(VkCommandBuffer cmdBuffer);

private:
    GFXDevice& _context;
    vkb::Instance _vkbInstance;
    VKDevice_uptr _device{ nullptr };
    VmaAllocator _allocator{VK_NULL_HANDLE};
    VkPipelineCache _pipelineCache{ VK_NULL_HANDLE };

    GFX::MemoryBarrierCommand _pushConstantsMemCommand{};
    DescriptorLayoutCache_uptr _descriptorLayoutCache{nullptr};

    hashMap<I64, VKPerWindowState> _perWindowState;
    hashMap<size_t, CompiledPipeline> _compiledPipelines;

    std::array<VkDescriptorSet, to_base( DescriptorSetUsage::COUNT )> _descriptorSets;
    std::array<VkDescriptorSetLayout, to_base( DescriptorSetUsage::COUNT )> _descriptorSetLayouts;

    U8 _currentFrameIndex{ 0u };
    bool _pushConstantsNeedLock{ false };
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
    static bool s_hasValidationFeaturesSupport;
    static DepthFormatInformation s_depthFormatInformation;
};

};  // namespace Divide

#endif //_VK_WRAPPER_H_
