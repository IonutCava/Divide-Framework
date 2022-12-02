#include "stdafx.h"

#include "Headers/vkShaderBuffer.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{
    vkShaderBuffer::vkShaderBuffer( GFXDevice& context, const ShaderBufferDescriptor& descriptor )
        : ShaderBuffer( context, descriptor )
    {
        const size_t targetElementSize = Util::GetAlignmentCorrected( _params._elementSize, AlignmentRequirement( _usage ) );
        if ( targetElementSize > _params._elementSize )
        {
            DIVIDE_ASSERT( (_params._elementSize * _params._elementCount) % AlignmentRequirement( _usage ) == 0u,
                           "ERROR: vkShaderBuffer - element size and count combo is less than the minimum alignment requirement for current hardware! Pad the element size and or count a bit" );
        }
        else
        {
            DIVIDE_ASSERT( _params._elementSize == targetElementSize,
                           "ERROR: vkShaderBuffer - element size is less than the minimum alignment requirement for current hardware! Pad the element size a bit" );
        }

        _alignedBufferSize = _params._elementCount * _params._elementSize;
        _alignedBufferSize = static_cast<ptrdiff_t>(realign_offset( _alignedBufferSize, AlignmentRequirement( _usage ) ));

        VkBufferUsageFlags usageFlags = (_usage == Usage::UNBOUND_BUFFER || _usage == Usage::COMMAND_BUFFER)
            ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        if ( _usage == Usage::COMMAND_BUFFER )
        {
            usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        }

        const size_t dataSize = _alignedBufferSize * queueLength();
        _bufferImpl = eastl::make_unique<AllocatedBuffer>( BufferUsageType::SHADER_BUFFER );
        _bufferImpl->_params = _params;

        const string bufferName = _name.empty() ? Util::StringFormat( "DVD_GENERAL_SHADER_BUFFER_%zu", getGUID() ) : (_name + "_SHADER_BUFFER");

        _stagingBuffer = VKUtil::createStagingBuffer( dataSize, bufferName );
        Byte* mappedRange = (Byte*)_stagingBuffer->_allocInfo.pMappedData;

        for ( U32 i = 0u; i < queueLength(); ++i )
        {
            if ( descriptor._initialData.first == nullptr )
            {
                memset( &mappedRange[i * _alignedBufferSize], 0, _alignedBufferSize );
            }
            else
            {
                memcpy( &mappedRange[i * _alignedBufferSize], descriptor._initialData.first, descriptor._initialData.second > 0 ? descriptor._initialData.second : _alignedBufferSize );
            }
        }

        // Let the VMA library know that this buffer should be readable by the GPU only
        VmaAllocationCreateInfo vmaallocInfo = {};
        vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaallocInfo.flags = 0;
        if ( _params._updateFrequency == BufferUpdateFrequency::OFTEN )
        {
            // If we write to this buffer often (e.g. GPU uniform blocks), we might as well persistently map it and use
            // a lock manager to protect writes (same as GL_API's lockManager system)
            // A staging buffer is just way too slow for multiple writes per frame.
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            vmaallocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
        else if ( _params._hostVisible )
        {
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }

        // Allocate the shader buffer
        {
            const VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo( usageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dataSize );

            UniqueLock<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
            VK_CHECK( vmaCreateBuffer( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                       &bufferInfo,
                                       &vmaallocInfo,
                                       &_bufferImpl->_buffer,
                                       &_bufferImpl->_allocation,
                                       &_bufferImpl->_allocInfo ) );

            VkMemoryPropertyFlags memPropFlags;
            vmaGetAllocationMemoryProperties( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                              _bufferImpl->_allocation,
                                              &memPropFlags );

            vmaSetAllocationName( *VK_API::GetStateTracker()._allocatorInstance._allocator, _bufferImpl->_allocation, bufferName.c_str() );
            Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)_bufferImpl->_buffer, VK_OBJECT_TYPE_BUFFER, bufferName.c_str() );
        }

        // Queue a command to copy from the staging buffer to the vertex buffer
        VKTransferQueue::TransferRequest request{};
        request.srcOffset = 0u;
        request.dstOffset = 0u;
        request.size = dataSize;
        request.srcBuffer = _stagingBuffer->_buffer;
        request.dstBuffer = _bufferImpl->_buffer;
        request.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        request._immediate = true;
        VK_API::RegisterTransferRequest( request );

        if ( _params._updateFrequency == BufferUpdateFrequency::ONCE ||
             _params._updateFrequency == BufferUpdateFrequency::OFTEN )
        {
            _stagingBuffer.reset();
        }
    }

    void vkShaderBuffer::writeBytesInternal( const BufferRange range, bufferPtr data ) noexcept
    {
        if ( !_lockManager.waitForLockedRange( range._startOffset, range._length ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        UniqueLock<Mutex> w_lock( _stagingBufferLock );

        Byte* mappedRange = nullptr;
        if ( _params._updateFrequency != BufferUpdateFrequency::OFTEN )
        {
            mappedRange = (Byte*)_stagingBuffer->_allocInfo.pMappedData;
        }
        else
        {
            mappedRange = (Byte*)_bufferImpl->_allocInfo.pMappedData;
        }

        if ( data == nullptr )
        {
            memset( &mappedRange[range._startOffset], 0, range._length );
        }
        else
        {
            memcpy( &mappedRange[range._startOffset], data, range._length );
        }

        if ( _params._updateFrequency == BufferUpdateFrequency::OFTEN )
        {
            if ( !_lockManager.lockRange( range._startOffset, range._length, _lockManager.createSyncObject() ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
        else
        {
            // Queue a command to copy from the staging buffer to the vertex buffer
            VKTransferQueue::TransferRequest request{};
            request.srcOffset = range._startOffset;
            request.dstOffset = range._startOffset;
            request.size = range._length;
            request.srcBuffer = _stagingBuffer->_buffer;
            request.dstBuffer = _bufferImpl->_buffer;
            request.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            VK_API::RegisterTransferRequest( request );
        }
    }

    void vkShaderBuffer::readBytesInternal( BufferRange range, std::pair<bufferPtr, size_t> outData ) noexcept
    {
        if ( !_lockManager.waitForLockedRange( range._startOffset, range._length ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        UniqueLock<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
        if ( _params._updateFrequency == BufferUpdateFrequency::OFTEN )
        {
            Byte* mappedRange = (Byte*)_bufferImpl->_allocInfo.pMappedData;
            memcpy( outData.first, &mappedRange[range._startOffset], std::min( std::min( range._length, outData.second ), _alignedBufferSize ) );
        }
        else
        {
            void* mappedData;
            vmaMapMemory( *VK_API::GetStateTracker()._allocatorInstance._allocator, _bufferImpl->_allocation, &mappedData );
            Byte* mappedRange = (Byte*)mappedData;
            memcpy( outData.first, &mappedRange[range._startOffset], std::min( std::min( range._length, outData.second ), _alignedBufferSize ) );
            vmaUnmapMemory( *VK_API::GetStateTracker()._allocatorInstance._allocator, _bufferImpl->_allocation );
        }
    }

}; //namespace Divide
