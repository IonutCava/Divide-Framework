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
#ifndef DVD_VK_WRAPPER_H_
#define DVD_VK_WRAPPER_H_

#include "vkDevice.h"
#include "vkSwapChain.h"
#include "vkMemAllocatorInclude.h"

#include "Platform/Video/Headers/Commands.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/GPUBuffer.h"

#include "vkDescriptors.h"

namespace Divide
{

class Pipeline;
struct VKAPITestAccessor;
enum class ShaderResult : U8;

class VK_API final : public RenderAPIWrapper
{

#if defined(ENABLE_UNIT_TESTING)
    friend struct VKAPITestAccessor;
#endif //ENABLE_UNIT_TESTING

public:
    static constexpr VkPipelineStageFlagBits2 ALL_SHADER_STAGES_NO_MESH = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                                          VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
                                                                          VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
                                                                          VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
                                                                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                                                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    static constexpr VkPipelineStageFlagBits2 ALL_SHADER_STAGES_WITH_MESH = ALL_SHADER_STAGES_NO_MESH |
                                                                            VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                                                            VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;

    static VkPipelineStageFlagBits2 AllShaderStages() noexcept;

public:
 
    VK_API(GFXDevice& context) noexcept;

    [[nodiscard]] VKDevice* getDevice() { return _device.get(); }

    [[nodiscard]] GFXDevice& context() noexcept { return _context; }
    [[nodiscard]] const GFXDevice& context() const noexcept { return _context; }

protected:
    [[nodiscard]] VkCommandBuffer getCurrentCommandBuffer() const noexcept;

    void idle(bool fast) noexcept override;

    [[nodiscard]] bool drawToWindow( DisplayWindow& window ) override;
                  void onRenderThreadLoopStart() override;
                  void onRenderThreadLoopEnd() override;
                  void prepareFlushWindow( DisplayWindow& window ) override;
                  void flushWindow( DisplayWindow& window ) override;
    [[nodiscard]] bool frameStarted() override;
    [[nodiscard]] bool frameEnded() override;

    [[nodiscard]] ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) noexcept override;
    void closeRenderingAPI() override;
    void preFlushCommandBuffer( Handle<GFX::CommandBuffer> commandBuffer) override;
    void flushCommand( GFX::CommandBase* cmd ) noexcept override;
    void postFlushCommandBuffer( Handle<GFX::CommandBuffer> commandBuffer ) noexcept override;
    bool setViewportInternal(const Rect<I32>& newViewport) noexcept override;
    bool setScissorInternal(const Rect<I32>& newScissor) noexcept override;

    void onThreadCreated( const size_t threadIndex, const std::thread::id& threadID, bool isMainRenderThread ) noexcept override;
    void initDescriptorSets() override;

    [[nodiscard]] RenderTarget_uptr newRenderTarget( const RenderTargetDescriptor& descriptor ) const override;
    [[nodiscard]] GPUBuffer_uptr    newGPUBuffer( U32 ringBufferLength, const std::string_view name ) const override;
    [[nodiscard]] ShaderBuffer_uptr newShaderBuffer( const ShaderBufferDescriptor& descriptor ) const override;

private:
    void initStatePerWindow( VKPerWindowState& windowState );
    void destroyStatePerWindow( VKPerWindowState& windowState );
    void recreateSwapChain( VKPerWindowState& windowState );

    bool setViewportInternal( const Rect<I32>& newViewport, VkCommandBuffer cmdBuffer ) noexcept;
    bool setScissorInternal( const Rect<I32>& newScissor, VkCommandBuffer cmdBuffer ) noexcept;
    void destroyPipelineCache();
    void destroyPipeline( CompiledPipeline& pipeline, bool defer );
    void flushPushConstantsLocks();
    VkDescriptorSetLayout createLayoutFromBindings( const DescriptorSetUsage usage, const ShaderProgram::BindingsPerSetArray& bindings, DynamicBindings& dynamicBindings );

    ShaderResult bindPipeline(const Pipeline& pipeline, VkCommandBuffer cmdBuffer);
    void bindDynamicState(const RenderStateBlock& currentState, const RTBlendStates& blendStates, VkCommandBuffer cmdBuffer) noexcept;
    [[nodiscard]] bool bindShaderResources( const DescriptorSetEntries& descriptorSetEntries ) override;



    static bool Draw(GenericDrawCommand cmd, VkCommandBuffer cmdBuffer);
public:
    [[nodiscard]] static VKStateTracker& GetStateTracker() noexcept;
    [[nodiscard]] static VkSampler GetSamplerHandle(SamplerDescriptor sampler, size_t& samplerHashInOut);

    static void RegisterCustomAPIDelete(DELEGATE<void, VkDevice>&& cbk, bool isResourceTransient);
    static void RegisterTransferRequest(const VKTransferQueue::TransferRequest& request);
    static void FlushBufferTransferRequests( VkCommandBuffer cmdBuffer );
    static void SubmitTransferRequest(const VKTransferQueue::TransferRequest& request, VkCommandBuffer cmd);

    static void AddDebugMessage( const Configuration& config, VkCommandBuffer cmdBuffer, const char* message, U32 id = U32_MAX);
    static void PushDebugMessage( const Configuration& config, VkCommandBuffer cmdBuffer, const char* message, U32 id = U32_MAX);
    static void PopDebugMessage( const Configuration& config, VkCommandBuffer cmdBuffer);

    static void OnShaderReloaded( vkShaderProgram* program);
private:
    GFXDevice& _context;
    vkb::Instance _vkbInstance;
    VKDevice_uptr _device{ nullptr };
    VmaAllocator _allocator{VK_NULL_HANDLE};
    VkPipelineCache _pipelineCache{ VK_NULL_HANDLE };
    VkDescriptorSet _dummyDescriptorSet{VK_NULL_HANDLE};

    GFX::MemoryBarrierCommand _uniformsMemCommand{};
    DescriptorLayoutCache_uptr _descriptorLayoutCache{nullptr};

    hashMap<I64, VKPerWindowState> _perWindowState;
    hashMap<size_t, CompiledPipeline> _compiledPipelines;

    std::array<VkDescriptorSet, to_base( DescriptorSetUsage::COUNT )> _descriptorSets;
    std::array<DynamicBindings, to_base( DescriptorSetUsage::COUNT )> _descriptorDynamicBindings;
    std::array<VkDescriptorSetLayout, to_base( DescriptorSetUsage::COUNT )> _descriptorSetLayouts;


    bool _uniformsNeedLock{ false };
    
private:
    using SamplerObjectMap = hashMap<size_t, VkSampler, NoHash<size_t>>;

    static SharedMutex s_samplerMapLock;
    static SamplerObjectMap s_samplerMap;
    static VKStateTracker s_stateTracker;
    static VKDeletionQueue s_transientDeleteQueue;
    static VKDeletionQueue s_deviceDeleteQueue;
    static VKTransferQueue s_transferQueue;

    static eastl::stack<vkShaderProgram*> s_reloadedShaders;

public:
    struct DepthFormatInformation
    {
        bool _d24s8Supported{false};
        bool _d32s8Supported{false};
        bool _d24x8Supported{false};
        bool _d32FSupported{false};
    };

    static bool s_hasDebugMarkerSupport;
    static bool s_hasDescriptorBufferSupport;
    static bool s_hasDynamicBlendStateSupport;
    static DepthFormatInformation s_depthFormatInformation;
    static VkResolveModeFlags s_supportedDepthResolveModes;
};

};  // namespace Divide

#endif //DVD_VK_WRAPPER_H_
