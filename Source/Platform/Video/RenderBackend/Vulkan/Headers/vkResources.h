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
#ifndef VK_RESOURCES_H
#define VK_RESOURCES_H

#include "Platform/Video/Headers/RenderAPIWrapper.h"

#include "vkInitializers.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Headers/BlendingProperties.h"
#include "Platform/Video/RenderBackend/Vulkan/Vulkan-Descriptor-Allocator/descriptor_allocator.h"

namespace vke
{
    FWD_DECLARE_MANAGED_CLASS( DescriptorAllocatorPool );
};

VK_DEFINE_HANDLE( VmaAllocator )

namespace NS_GLIM {
    enum class GLIM_ENUM : int;
}; //namespace NS_GLIM

// Custom define for better code readability
#define VK_FLAGS_NONE 0

// Default fence timeout in nanoseconds
#define DEFAULT_FENCE_TIMEOUT 100000000000


namespace Divide {

class VKDevice;
class vkShaderProgram;
enum class DescriptorSetBindingType : U8;

FWD_DECLARE_MANAGED_CLASS( VKSwapChain );


namespace Debug {
    extern PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT;
    extern PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT;
    extern PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT;
    extern PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;
    extern PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTagEXT;

    void SetObjectName(VkDevice device, uint64_t object, VkObjectType objectType, const char* name);
    void SetObjectTag(VkDevice device, uint64_t object, const VkObjectType objectType, size_t tagSize, void* tagData, uint64_t tagName);
};

constexpr U32 INVALID_VK_QUEUE_INDEX = U32_MAX;

enum class QueueType : U8
{
    GRAPHICS,
    COMPUTE,
    TRANSFER,
    COUNT
};

struct VKQueue {
    VkQueue _queue{};
    VkCommandPool _pool{ VK_NULL_HANDLE };
    U32 _index{ INVALID_VK_QUEUE_INDEX };
    QueueType _type{QueueType::COUNT};
};

struct CompiledPipeline
{
    VkPipelineBindPoint _bindPoint{ VK_PIPELINE_BIND_POINT_MAX_ENUM };
    vkShaderProgram* _program{ nullptr };
    VkPipeline _vkPipeline{ VK_NULL_HANDLE };
    VkPipeline _vkPipelineWireframe{ VK_NULL_HANDLE };
    VkPipelineLayout _vkPipelineLayout{ VK_NULL_HANDLE };
    PrimitiveTopology _topology{ PrimitiveTopology::COUNT };
    VkShaderStageFlags _stageFlags{ VK_FLAGS_NONE };
    VkDescriptorSetLayout* _descriptorSetlayout{ nullptr };
    bool _isValid{false};
};

struct PipelineBuilder
{
    fixed_vector<VkPipelineShaderStageCreateInfo, to_base(ShaderType::COUNT)> _shaderStages;
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
    VkViewport _viewport;
    VkRect2D _scissor;
    VkPipelineRasterizationStateCreateInfo _rasterizer;
    fixed_vector<VkPipelineColorBlendAttachmentState, to_base( RTColourAttachmentSlot::COUNT )> _colorBlendAttachments;
    VkPipelineMultisampleStateCreateInfo _multisampling;
    VkPipelineLayout _pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo _depthStencil;
    VkPipelineTessellationStateCreateInfo _tessellation;

    VkPipeline build_pipeline( VkDevice device, VkPipelineCache pipelineCache, bool graphics );

    private:
    VkPipeline build_compute_pipeline( VkDevice device, VkPipelineCache pipelineCache );
    VkPipeline build_graphics_pipeline( VkDevice device, VkPipelineCache pipelineCache );
};

struct VkPipelineEntry
{
    VkPipeline _pipeline{ VK_NULL_HANDLE };
    VkPipelineLayout _layout{ VK_NULL_HANDLE };
};

struct DynamicBinding
{
    U32 _offset{ 0u };
    U8 _slot{ U8_MAX };
};

using DynamicBindings = fixed_vector<DynamicBinding, MAX_BINDINGS_PER_DESCRIPTOR_SET>;

struct VKImmediateCmdContext
{
    static constexpr U8 BUFFER_COUNT = 4u;

    using FlushCallback = std::function<void( VkCommandBuffer cmd, QueueType queue, U32 queueIndex )>;
    explicit VKImmediateCmdContext( const Configuration& config, VKDevice& context, QueueType type );
    ~VKImmediateCmdContext();

    void flushCommandBuffer( FlushCallback&& function, const char* scopeName, bool waitForFinish = false );

  private:

    VKDevice& _context;
    const Configuration& _config;
    const QueueType _type;
    const U32 _queueIndex;

    VkCommandPool _commandPool;
    Mutex _submitLock;


    std::array<VkFence, BUFFER_COUNT> _bufferFences;
    std::array<VkCommandBuffer, BUFFER_COUNT> _commandBuffers;
    U64 _wrapCounter{ 0u };
    U8 _bufferIndex{ 0u };
};

FWD_DECLARE_MANAGED_STRUCT( VKImmediateCmdContext );

struct VMAAllocatorInstance
{
    VmaAllocator* _allocator{ nullptr };
    Mutex _allocatorLock;
};

struct DescriptorAllocator
{
    U8 _frameCount{ 1u };
    vke::DescriptorAllocatorHandle _handle{};
    vke::DescriptorAllocatorPool_uptr _allocatorPool{ nullptr };
};

struct VKPerWindowState
{
    DisplayWindow* _window{ nullptr };
    VKSwapChain_uptr _swapChain{ nullptr };
    VkSurfaceKHR _surface{ VK_NULL_HANDLE }; // Vulkan window surface

    VkExtent2D _windowExtents{};
    bool _skipEndFrame{ false };

    struct VKDynamicState
    {
        RenderStateBlock _block{};
        RTBlendStates _blendStates{};
        bool _isSet{ false };

    } _activeState;
};

struct VKStateTracker
{
    void init( const Configuration& config, VKDevice* device, VKPerWindowState* mainWindow );
    void reset();
    void setDefaultState();

    VKImmediateCmdContext* IMCmdContext( QueueType type ) const;

    VKDevice* _device{ nullptr };
    VKPerWindowState* _activeWindow{ nullptr };


    VMAAllocatorInstance _allocatorInstance{};
    std::array<DescriptorAllocator, to_base( DescriptorSetUsage::COUNT )> _descriptorAllocators;
    CompiledPipeline _pipeline{};

    VkPipelineRenderingCreateInfo _pipelineRenderInfo{};

    VkBuffer _drawIndirectBuffer{ VK_NULL_HANDLE };
    size_t _drawIndirectBufferOffset{ 0u };

    VkShaderStageFlags _pipelineStageMask{ VK_FLAGS_NONE };

    RenderTargetID _activeRenderTargetID{ INVALID_RENDER_TARGET_ID };
    size_t _renderTargetFormatHash{0u};
    size_t _activeRenderTargetColourAttachmentCount{ 0u };
    vec2<U16> _activeRenderTargetDimensions{ 1u };

    U8 _activeMSAASamples{ 1u };
    bool _pushConstantsValid{ false };

    bool* _enabledAPIDebugging;
    bool* _assertOnAPIError;


   private:
    std::array<VKImmediateCmdContext_uptr, to_base( QueueType::COUNT )> _cmdContexts;
};

FWD_DECLARE_MANAGED_STRUCT( VKStateTracker );


struct VKDeletionQueue
{
    using QueuedItem = DELEGATE<void, VkDevice>;

    enum class Flags : U8
    {
        TREAT_AS_TRANSIENT = toBit( 1 ),
        COUNT = 1
    };

    void push( QueuedItem&& function );
    void flush( VkDevice device, bool force = false );
    void onFrameEnd();

    [[nodiscard]] bool empty() const;

    mutable Mutex _deletionLock;
    std::deque<std::pair<QueuedItem, U8>> _deletionQueue;
    PROPERTY_RW( U32, flags, 0u );
};

struct VKTransferQueue
{
    struct TransferRequest
    {
        VkDeviceSize srcOffset{ 0u };
        VkDeviceSize dstOffset{ 0u };
        VkDeviceSize size{ 0u };
        VkBuffer     srcBuffer{ VK_NULL_HANDLE };
        VkBuffer     dstBuffer{ VK_NULL_HANDLE };

        VkAccessFlags2 dstAccessMask{ VK_ACCESS_2_NONE };
        VkPipelineStageFlags2 dstStageMask{ VK_PIPELINE_STAGE_2_NONE };
    };

    moodycamel::ConcurrentQueue<TransferRequest> _requests;
    std::atomic_bool _dirty{ false };
};

struct VKImageBarrierQueue
{
    Mutex _lock;
    fixed_vector<VkImageMemoryBarrier2, 16, true> _imageBarriers;
};

struct VKSubmitSempahore
{
    using Container = fixed_vector<VkSemaphore, 16, true>;

    Mutex _lock;
    Container _pendingSubmitSemaphores;
};
//ref:  SaschaWillems / Vulkan / VulkanTools
inline std::string VKErrorString(VkResult errorCode)
{
    switch (errorCode)
    {
#define STR(r) case VK_ ##r: return #r
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
#undef STR
    default:
        return "UNKNOWN_ERROR";
    }
}
#ifndef VK_CHECK
#define VK_CHECK(x)                                                                            \
    do                                                                                         \
    {                                                                                          \
        VkResult err = x;                                                                      \
        if (err)                                                                               \
        {                                                                                      \
            Console::errorfn(LOCALE_STR("ERROR_GENERIC_VK"), VKErrorString(err).c_str());\
            DIVIDE_UNEXPECTED_CALL();                                                          \
        }                                                                                      \
    } while (0)
#endif //VK_CHECK

#ifndef VK_PROFILE
#define VK_PROFILE(FUNCTION, ...) \
do                                \
{                                 \
    PROFILE_VK_EVENT(#FUNCTION);  \
    FUNCTION(__VA_ARGS__);        \
} while ( 0 )

#endif //VK_PROFILE

// Conditionally executes code only when unit testing is enabled, allowing tests to pass VK_NULL_HANDLE
#ifndef VK_UT_IF_CHECK
#   if defined(ENABLE_UNIT_TESTING)
#       define VK_UT_IF_CHECK( X ) if ( (X) )
#       define VK_NON_UT_ASSERT( X )
#   else //ENABLE_UNIT_TESTING
#       define VK_UT_IF_CHECK( X ) /* nothing */
#       define VK_NON_UT_ASSERT( X ) DIVIDE_ASSERT( (X) )
#   endif //ENABLE_UNIT_TESTING
#endif //VK_UT_IF_CHECK
    struct VulkanQueryType
    {
        VkQueryType _queryType { VK_QUERY_TYPE_MAX_ENUM };
        VkQueryPipelineStatisticFlagBits _statistics { VK_QUERY_PIPELINE_STATISTIC_FLAG_BITS_MAX_ENUM };
        bool _accurate{false};
    };
    extern std::array<VkBlendFactor, to_base(BlendProperty::COUNT)> vkBlendTable;
    extern std::array<VkBlendOp, to_base(BlendOperation::COUNT)> vkBlendOpTable;
    extern std::array<VkCompareOp, to_base(ComparisonFunction::COUNT)> vkCompareFuncTable;
    extern std::array<VkStencilOp, to_base(StencilOperation::COUNT)> vkStencilOpTable;
    extern std::array<VkCullModeFlags, to_base(CullMode::COUNT)> vkCullModeTable;
    extern std::array<VkPolygonMode, to_base(FillMode::COUNT)> vkFillModeTable;
    extern std::array<VkImageType, to_base(TextureType::COUNT)> vkTextureTypeTable;
    extern std::array<VkImageViewType, to_base(TextureType::COUNT)> vkTextureViewTypeTable;
    extern std::array<VkPrimitiveTopology, to_base(PrimitiveTopology::COUNT)> vkPrimitiveTypeTable;
    extern std::array<VkSamplerAddressMode, to_base(TextureWrap::COUNT)> vkWrapTable;
    extern std::array<VkShaderStageFlagBits, to_base(ShaderType::COUNT)> vkShaderStageTable;
    extern std::array<VulkanQueryType, to_base( QueryType::COUNT )> vkQueryTypeTable;

    struct GenericDrawCommand;
namespace VKUtil {
    constexpr U8 k_invalidSyncID = U8_MAX;

    ///Note: If internal format is not GL_NONE, an indexed draw is issued!
    void SubmitRenderCommand( const GenericDrawCommand& drawCommand,
                              const VkCommandBuffer commandBuffer,
                              bool indexed = false );


    void OnStartup(VkDevice device);

    [[nodiscard]] VkFormat InternalFormat(GFXImageFormat baseFormat, GFXDataFormat dataType, GFXImagePacking packing) noexcept;
    [[nodiscard]] VkFormat InternalFormat(GFXDataFormat format, U8 componentCount, bool normalized) noexcept;
    [[nodiscard]] VkDescriptorType vkDescriptorType(DescriptorSetBindingType type ) noexcept;
}; //namespace VKUtil
}; //namespace Divide

inline bool operator==(const VkDescriptorSetLayoutBinding& lhs, const VkDescriptorSetLayoutBinding& rhs) noexcept 
{
    return lhs.binding == rhs.binding &&
           lhs.descriptorType == rhs.descriptorType &&
           lhs.descriptorCount == rhs.descriptorCount &&
           lhs.stageFlags == rhs.stageFlags &&
           lhs.pImmutableSamplers == rhs.pImmutableSamplers;
}

inline bool operator!=( const VkDescriptorSetLayoutBinding& lhs, const VkDescriptorSetLayoutBinding& rhs ) noexcept
{
    return lhs.binding != rhs.binding ||
           lhs.descriptorType != rhs.descriptorType ||
           lhs.descriptorCount != rhs.descriptorCount ||
           lhs.stageFlags != rhs.stageFlags ||
           lhs.pImmutableSamplers !=  rhs.pImmutableSamplers;
}

inline bool operator==(const VkViewport& lhs, const VkViewport& rhs) noexcept
{
    return lhs.x == rhs.x &&
           lhs.y == rhs.y &&
           lhs.width == rhs.width &&
           lhs.height == rhs.height &&
           lhs.minDepth == rhs.minDepth &&
           lhs.maxDepth == rhs.maxDepth;
}
inline bool operator!=(const VkViewport& lhs, const VkViewport& rhs) noexcept
{
    return lhs.x != rhs.x ||
           lhs.y != rhs.y ||
           lhs.width != rhs.width ||
           lhs.height != rhs.height ||
           lhs.minDepth != rhs.minDepth ||
           lhs.maxDepth != rhs.maxDepth;
}
inline bool operator==(const VkRect2D& lhs, const VkRect2D& rhs) noexcept
{
    return lhs.offset.x == rhs.offset.x &&
           lhs.offset.y == rhs.offset.y &&
           lhs.extent.width == rhs.extent.width &&
           lhs.extent.height == rhs.extent.height;
}
inline bool operator!=(const VkRect2D& lhs, const VkRect2D& rhs) noexcept 
{
    return lhs.offset.x != rhs.offset.x ||
           lhs.offset.y != rhs.offset.y ||
           lhs.extent.width != rhs.extent.width ||
           lhs.extent.height != rhs.extent.height;
}
#endif //VK_RESOURCES_H
