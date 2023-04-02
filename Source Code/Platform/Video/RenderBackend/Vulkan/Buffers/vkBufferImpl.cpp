#include "stdafx.h"

#include "Headers/vkBufferImpl.h"
#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkLockManager.h"

namespace Divide
{
    VertexInputDescription getVertexDescription( const AttributeMap& vertexFormat )
    {
        VertexInputDescription description;

        for ( const VertexBinding& vertBinding : vertexFormat._vertexBindings )
        {
            VkVertexInputBindingDescription mainBinding = {};
            mainBinding.binding = vertBinding._bufferBindIndex;
            mainBinding.stride = to_U32( vertBinding._strideInBytes );
            mainBinding.inputRate = vertBinding._perVertexInputRate ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;

            description.bindings.push_back( mainBinding );
        }

        for ( U8 idx = 0u; idx < to_base( AttribLocation::COUNT ); ++idx )
        {
            const AttributeDescriptor& descriptor = vertexFormat._attributes[idx];
            if ( descriptor._dataType == GFXDataFormat::COUNT )
            {
                continue;
            }

            VkVertexInputAttributeDescription attribute = {};
            attribute.binding = descriptor._vertexBindingIndex;
            attribute.location = idx;
            attribute.format = VKUtil::InternalFormat( descriptor._dataType, descriptor._componentsPerElement, descriptor._normalized );
            attribute.offset = to_U32( descriptor._strideInBytes );

            description.attributes.push_back( attribute );
        }

        return description;
    }

    VMABuffer::VMABuffer( const BufferParams params )
        : _params(params)
    {
    }

    VMABuffer::~VMABuffer()
    {
        if ( _buffer != VK_NULL_HANDLE )
        {
            VK_API::RegisterCustomAPIDelete( [buf = _buffer, alloc = _allocation]( [[maybe_unused]] VkDevice device )
                                             {
                                                 LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
                                                 vmaDestroyBuffer( *VK_API::GetStateTracker()._allocatorInstance._allocator, buf, alloc );
                                             }, true );
        }
    }

    vkBufferImpl::vkBufferImpl( const BufferParams& params,
                                const size_t alignedBufferSize,
                                const size_t ringQueueLength,
                                std::pair<bufferPtr, size_t> initialData,
                                const char* bufferName ) noexcept
        : VMABuffer(params)
        , _alignedBufferSize( alignedBufferSize )
    {
        _lockManager = eastl::make_unique<vkLockManager>();

        VkAccessFlags2 dstAccessMask = VK_ACCESS_2_NONE;
        VkPipelineStageFlags2 dstStageMask = VK_PIPELINE_STAGE_2_NONE;

        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        switch ( _params._flags._usageType )
        {
            case BufferUsageType::STAGING_BUFFER:
            {
                usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                usageFlags &= ~VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
                dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            } break;
            case BufferUsageType::VERTEX_BUFFER:
            {
                usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
                dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            } break;
            case BufferUsageType::INDEX_BUFFER:
            {
                usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT;
                dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            } break;
            case BufferUsageType::CONSTANT_BUFFER:
            {
                usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                dstStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            } break;
            case BufferUsageType::UNBOUND_BUFFER:
            {
                usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                dstStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            } break;
            case BufferUsageType::COMMAND_BUFFER:
            {
                usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                dstStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            } break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }

        // Let the VMA library know that this buffer should be readable by the GPU only
        VmaAllocationCreateInfo vmaallocInfo{};
        vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaallocInfo.flags = 0;
        if ( _params._flags._updateFrequency != BufferUpdateFrequency::ONCE )
        {
            // If we write to this buffer often (e.g. GPU uniform blocks), we might as well persistently map it and use
            // a lock manager to protect writes (same as GL_API's lockManager system)
            // A staging buffer is just way too slow for multiple writes per frame.
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }
        else if ( _params._hostVisible )
        {
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }
        // Allocate the vertex buffer (as a vb buffer and transfer destination)
        VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo( usageFlags, alignedBufferSize * ringQueueLength);
        {
            LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
            VK_CHECK( vmaCreateBuffer( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                       &bufferInfo,
                                       &vmaallocInfo,
                                       &_buffer,
                                       &_allocation,
                                       &_allocInfo ) );

            VkMemoryPropertyFlags memPropFlags;
            vmaGetAllocationMemoryProperties( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                              _allocation,
                                              &memPropFlags );

            vmaSetAllocationName( *VK_API::GetStateTracker()._allocatorInstance._allocator, _allocation, bufferName );

            Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)_buffer, VK_OBJECT_TYPE_BUFFER, bufferName );
            _isMemoryMappable = memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        }

        _isLockable = _isMemoryMappable;

        Byte* mappedRange = nullptr;
        if (!_isMemoryMappable)
        {
            _stagingBuffer = VKUtil::createStagingBuffer( alignedBufferSize * ringQueueLength, bufferName, false );
            mappedRange = (Byte*)_stagingBuffer->_allocInfo.pMappedData;
        }
        else
        {
            mappedRange = (Byte*)_allocInfo.pMappedData;
        }

        const size_t localDataSize = initialData.second > 0 ? initialData.second : _alignedBufferSize;
        for ( U32 i = 0u; i < ringQueueLength; ++i )
        {
            if ( initialData.first == nullptr )
            {
                memset( &mappedRange[i * _alignedBufferSize], 0, _alignedBufferSize );
            }
            else
            {
                memcpy( &mappedRange[i * _alignedBufferSize], initialData.first, localDataSize );
            }

        }

        if ( !_isMemoryMappable )
        {
            const auto scopeName = Util::StringFormat( "Immediate Buffer Upload [ %s ]", bufferName );

            // Queue a command to copy from the staging buffer to the vertex buffer
            VKTransferQueue::TransferRequest request{};
            request.srcOffset = 0u;
            request.dstOffset = 0u;
            request.size = alignedBufferSize * ringQueueLength;
            request.srcBuffer = _stagingBuffer->_buffer;
            request.dstBuffer = _buffer;
            request.dstAccessMask = dstAccessMask;
            request.dstStageMask = dstStageMask;
            VK_API::GetStateTracker().IMCmdContext(QueueType::GRAPHICS)->flushCommandBuffer([&request](VkCommandBuffer cmd, const QueueType queue, const bool isDedicatedQueue)
            {
                VK_API::SubmitTransferRequest( request, cmd );
            }, scopeName.c_str() );
        }

        if ( _params._flags._updateFrequency == BufferUpdateFrequency::ONCE || _isMemoryMappable)
        {
            _stagingBuffer.reset();
        }
        else
        {
            // Try and recover some VRAM
            _stagingBuffer = VKUtil::createStagingBuffer( alignedBufferSize, bufferName, false );
        }
    }


    BufferLock vkBufferImpl::writeBytes( const BufferRange range,
                                         VkAccessFlags2 dstAccessMask,
                                         VkPipelineStageFlags2 dstStageMask,
                                         bufferPtr data)
    {

        Byte* mappedRange = nullptr;
        size_t mappedOffset = 0u;
        if ( !_isMemoryMappable )
        {
            DIVIDE_ASSERT(_stagingBuffer != nullptr && _stagingBuffer->_params._elementSize >= range._length);
            mappedRange = (Byte*)_stagingBuffer->_allocInfo.pMappedData;
        }
        else
        {
            mappedOffset = range._startOffset;
            mappedRange = (Byte*)_allocInfo.pMappedData;
        }

        if ( data == nullptr )
        {
            memset( &mappedRange[mappedOffset], 0, range._length );
        }
        else
        {
            memcpy( &mappedRange[mappedOffset], data, range._length );
        }

        BufferLock ret = {
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = this
        };

        if ( !_isMemoryMappable )
        {
            // Queue a command to copy from the staging buffer to the vertex buffer
            VKTransferQueue::TransferRequest request{};
            request.srcOffset = mappedOffset;
            request.dstOffset = range._startOffset;
            request.size = range._length;
            request.srcBuffer = _stagingBuffer->_buffer;
            request.dstBuffer = _buffer;
            request.dstAccessMask = dstAccessMask;
            request.dstStageMask = dstStageMask;
            VK_API::RegisterTransferRequest( request );
        }
        else
        {
            ret._range = range;
        }

        return ret;
    }

    void vkBufferImpl::readBytes( const BufferRange range, std::pair<bufferPtr, size_t> outData )
    {
        if (_isMemoryMappable )
        {
            Byte* mappedRange = (Byte*)_allocInfo.pMappedData;
            memcpy( outData.first, &mappedRange[range._startOffset], range._length );
        }
        else
        {
            void* mappedData;
            LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
            vmaMapMemory( *VK_API::GetStateTracker()._allocatorInstance._allocator, _allocation, &mappedData );

            Byte* mappedRange = (Byte*)mappedData;
            memcpy( outData.first, &mappedRange[range._startOffset], range._length );

            vmaUnmapMemory( *VK_API::GetStateTracker()._allocatorInstance._allocator, _allocation );
        }
    }

    namespace VKUtil
    {

        VMABuffer_uptr createStagingBuffer( const size_t size, const std::string_view bufferName, const bool isCopySource )
        {
            BufferParams params{};
            params._flags._usageType = BufferUsageType::STAGING_BUFFER;
            params._flags._updateFrequency = BufferUpdateFrequency::OFTEN;
            params._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;
            params._elementCount = 1u;
            params._elementSize = size;

            VMABuffer_uptr ret = eastl::make_unique<VMABuffer>( params );

            VmaAllocationCreateInfo vmaallocInfo = {};
            // Let the VMA library know that this data should be writable by CPU only
            vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            // Allocate staging buffer
            const VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo( isCopySource ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size );
            // Allocate the buffer
            LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
            VK_CHECK( vmaCreateBuffer( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                       &bufferInfo,
                                       &vmaallocInfo,
                                       &ret->_buffer,
                                       &ret->_allocation,
                                       &ret->_allocInfo ) );

            Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)ret->_buffer, VK_OBJECT_TYPE_BUFFER, Util::StringFormat( "%s_staging_buffer", bufferName.data() ).c_str() );

            return ret;
        }
    } //namespace VKUtil
}; //namespace Divide