#include "stdafx.h"

#include "Headers/vkGenericVertexData.h"

#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {
    vkGenericVertexData::vkGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
        : GenericVertexData(context, ringBufferLength, name)
    {
    }

    void vkGenericVertexData::reset() {
        _bufferObjects.clear();
        _idxBuffers.clear();
    }

    void vkGenericVertexData::draw(const GenericDrawCommand& command, VDIUserData* userData) noexcept {
        vkUserData* vkData = static_cast<vkUserData*>(userData);

        for (const auto& buffer : _bufferObjects) 
        
        {
            bindBufferInternal(buffer._bindConfig, *vkData->_cmdBuffer);
        }

        const auto& idxBuffer = _idxBuffers[command._bufferFlag];
        if (idxBuffer._buffer != nullptr) {
            vkCmdBindIndexBuffer(*vkData->_cmdBuffer, idxBuffer._buffer->_buffer, 0u, idxBuffer._data.smallIndices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        }

        // Submit the draw command
        VKUtil::SubmitRenderCommand( command, *vkData->_cmdBuffer, idxBuffer._buffer != nullptr, renderIndirect() );
    }

    void vkGenericVertexData::bindBufferInternal(const SetBufferParams::BufferBindConfig& bindConfig, VkCommandBuffer& cmdBuffer) {
        GenericBufferImpl* impl = nullptr;
        for (auto& bufferImpl : _bufferObjects) {
            if (bufferImpl._bindConfig._bufferIdx == bindConfig._bufferIdx) {
                impl = &bufferImpl;
                break;
            }
        }

        if (impl == nullptr) {
            return;
        }

        const BufferParams& bufferParams = impl->_buffer->_params;
        VkDeviceSize offsetInBytes = 0u;

        if (impl->_ringSizeFactor > 1) {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        vkCmdBindVertexBuffers(cmdBuffer, bindConfig._bindIdx, 1, &impl->_buffer->_buffer, &offsetInBytes);
    }

    BufferLock vkGenericVertexData::setBuffer(const SetBufferParams& params) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( params._bufferParams._flags._usageType != BufferUsageType::COUNT );

        // Make sure we specify buffers in order.
        GenericBufferImpl* impl = nullptr;
        for (auto& buffer : _bufferObjects) {
            if (buffer._bindConfig._bufferIdx == params._bindConfig._bufferIdx) {
                impl = &buffer;
                break;
            }
        }

        if (impl == nullptr) {
            impl = &_bufferObjects.emplace_back();
        }

        const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_VTX_BUFFER_%d", handle()._id) : (_name + "_VTX_BUFFER");

        const size_t ringSizeFactor = params._useRingBuffer ? queueLength() : 1;
        const size_t bufferSizeInBytes = params._bufferParams._elementCount * params._bufferParams._elementSize;
        const size_t dataSize = bufferSizeInBytes * ringSizeFactor;

        const size_t elementStride = params._elementStride == SetBufferParams::INVALID_ELEMENT_STRIDE
                                                            ? params._bufferParams._elementSize
                                                            : params._elementStride;
        impl->_ringSizeFactor = ringSizeFactor;
        impl->_bindConfig = params._bindConfig;
        impl->_elementStride = elementStride;

        if ( impl->_buffer != nullptr && impl->_buffer->_params == params._bufferParams )
        {
            return updateBuffer( params._bindConfig._bufferIdx, 0, params._bufferParams._elementCount, params._initialData.first);
        }

        impl->_buffer = eastl::make_unique<vkAllocatedLockableBuffer>(params._bufferParams, impl->_lockManager.get());

        // Let the VMA library know that this buffer should be readable by the GPU only
        VmaAllocationCreateInfo vmaallocInfo{};
        vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaallocInfo.flags = 0;
        if (params._bufferParams._flags._updateFrequency != BufferUpdateFrequency::OFTEN)
        {
            // If we write to this buffer often (e.g. GPU uniform blocks), we might as well persistently map it and use
            // a lock manager to protect writes (same as GL_API's lockManager system)
            // A staging buffer is just way too slow for multiple writes per frame.
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }
        else if ( params._bufferParams._hostVisible )
        {
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }

        impl->_isMemoryMappable = false;
        // Allocate the vertex buffer (as a vb buffer and transfer destination)
        VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dataSize);
        {
            LockGuard<Mutex> w_lock(VK_API::GetStateTracker()._allocatorInstance._allocatorLock);
            VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()._allocatorInstance._allocator,
                                        &bufferInfo,
                                        &vmaallocInfo,
                                        &impl->_buffer->_buffer,
                                        &impl->_buffer->_allocation,
                                        &impl->_buffer->_allocInfo));

            VkMemoryPropertyFlags memPropFlags;
            vmaGetAllocationMemoryProperties(*VK_API::GetStateTracker()._allocatorInstance._allocator,
                                                impl->_buffer->_allocation,
                                                &memPropFlags);

            vmaSetAllocationName(*VK_API::GetStateTracker()._allocatorInstance._allocator, impl->_buffer->_allocation, bufferName.c_str());

            Debug::SetObjectName(VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)impl->_buffer->_buffer, VK_OBJECT_TYPE_BUFFER, bufferName.c_str());
            if ( params._bufferParams._flags._updateFrequency != BufferUpdateFrequency::ONCE )
            {
                impl->_isMemoryMappable = memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            }
        }

        Byte* mappedRange = nullptr;
        if ( !impl->_isMemoryMappable )
        {
            impl->_stagingBuffer = VKUtil::createStagingBuffer( dataSize, bufferName );
            mappedRange = (Byte*)impl->_stagingBuffer->_allocInfo.pMappedData;
        }
        else
        {
            if ( impl->_stagingBuffer )
            {
                impl->_stagingBuffer.reset();
            }

            mappedRange = (Byte*)impl->_buffer->_allocInfo.pMappedData;
        }

        const size_t localDataSize = params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes;
        for ( U32 i = 0u; i < ringSizeFactor; ++i )
        {
            if ( params._initialData.first == nullptr )
            {
                memset( &mappedRange[i * bufferSizeInBytes], 0, bufferSizeInBytes );
            }
            else
            {
                memcpy( &mappedRange[i * bufferSizeInBytes], params._initialData.first, localDataSize );
            }
        }

        if (!impl->_isMemoryMappable )
        {
            // Queue a command to copy from the staging buffer to the vertex buffer
            VKTransferQueue::TransferRequest request{};
            request.srcOffset = 0u;
            request.dstOffset = 0u;
            request.size = dataSize;
            request.srcBuffer = impl->_stagingBuffer->_buffer;
            request.dstBuffer = impl->_buffer->_buffer;
            request.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            request._immediate = true;
            VK_API::RegisterTransferRequest(request);
        }

        return BufferLock
        {
            ._range = {0u, dataSize},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = impl->_buffer.get()
        };
    }

    BufferLock vkGenericVertexData::setIndexBuffer(const IndexBuffer& indices) {
        IndexBufferEntry* oldIdxBufferEntry = nullptr;

        bool found = false;
        for (auto& idxBuffer : _idxBuffers) {
            if (idxBuffer._data.id == indices.id) {
                oldIdxBufferEntry = &idxBuffer;
                found = true;
                break;
            }
        }

        if (!found)
        {
            _idxBuffers.emplace_back();
            oldIdxBufferEntry = &_idxBuffers.back();
            oldIdxBufferEntry->_data = indices;
        }
        else if (oldIdxBufferEntry->_buffer != nullptr &&
                 oldIdxBufferEntry->_buffer->_buffer != VK_NULL_HANDLE &&
                 (!AreCompatible(oldIdxBufferEntry->_data, indices) || indices.count == 0u))
        {
            if ( !oldIdxBufferEntry->_lockManager->waitForLockedRange( 0, U32_MAX ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            oldIdxBufferEntry->_buffer.reset();
            oldIdxBufferEntry->_stagingBuffer.reset();
        }

        BufferParams params{};
        params._flags._updateFrequency = indices.dynamic ? BufferUpdateFrequency::OFTEN : BufferUpdateFrequency::OCASSIONAL;
        params._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;
        params._flags._usageType = BufferUsageType::INDEX_BUFFER;
        params._elementSize = indices.smallIndices ? sizeof( U16 ) : sizeof( U32 );
        params._elementCount = to_U32(indices.count);
        if ( oldIdxBufferEntry->_buffer != nullptr )
        {
            params._elementCount = std::max(to_U32(oldIdxBufferEntry->_data.count), params._elementCount);
        }

        if (oldIdxBufferEntry->_buffer == nullptr || oldIdxBufferEntry->_buffer->_params != params)
        {
            oldIdxBufferEntry->_buffer = eastl::make_unique<vkAllocatedLockableBuffer>(params, oldIdxBufferEntry->_lockManager.get());
        }

        if (indices.count > 0u)
        {
            vector_fast<U16> smallIndicesTemp;
            bufferPtr data = indices.data;

            if (indices.indicesNeedCast) {
                smallIndicesTemp.resize(indices.count);
                const U32* const dataIn = reinterpret_cast<U32*>(data);
                for (size_t i = 0u; i < indices.count; ++i) {
                    smallIndicesTemp[i] = to_U16(dataIn[i]);
                }
                data = smallIndicesTemp.data();
            }

            const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_IDX_BUFFER_%d", handle()._id) : (_name + "_IDX_BUFFER");

            bool isNewBuffer = false;
            if (oldIdxBufferEntry->_buffer->_buffer == VK_NULL_HANDLE)
            {
                isNewBuffer = true;
                _indexBufferSize = std::max( indices.count * params._elementSize, _indexBufferSize );

                //allocate vertex buffer
                const VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, _indexBufferSize);

                //let the VMA library know that this data should be writeable by CPU, but also readable by GPU
                VmaAllocationCreateInfo vmaallocInfo = {};
                vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                vmaallocInfo.flags = 0;

                oldIdxBufferEntry->_isMemoryMappable = false;
                if ( indices.dynamic )
                {
                    // If we write to this buffer often (e.g. GPU uniform blocks), we might as well persistently map it and use
                    // a lock manager to protect writes (same as GL_API's lockManager system)
                    // A staging buffer is just way too slow for multiple writes per frame.
                    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                }
                else if ( params._hostVisible )
                {
                    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                }

                //allocate the buffer
                LockGuard<Mutex> w_lock(VK_API::GetStateTracker()._allocatorInstance._allocatorLock);
                VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()._allocatorInstance._allocator,
                                         &bufferInfo,
                                         &vmaallocInfo,
                                         &oldIdxBufferEntry->_buffer->_buffer,
                                         &oldIdxBufferEntry->_buffer->_allocation,
                                         &oldIdxBufferEntry->_buffer->_allocInfo));

                VkMemoryPropertyFlags memPropFlags;
                vmaGetAllocationMemoryProperties(*VK_API::GetStateTracker()._allocatorInstance._allocator,
                                                 oldIdxBufferEntry->_buffer->_allocation,
                                                 &memPropFlags);

                vmaSetAllocationName(*VK_API::GetStateTracker()._allocatorInstance._allocator, oldIdxBufferEntry->_buffer->_allocation, bufferName.c_str());

                Debug::SetObjectName(VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)oldIdxBufferEntry->_buffer->_buffer, VK_OBJECT_TYPE_BUFFER, bufferName.c_str());

                oldIdxBufferEntry->_isMemoryMappable = memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            }

            const size_t range = indices.count * params._elementSize;
            DIVIDE_ASSERT(range <= _indexBufferSize);


            if ( !oldIdxBufferEntry->_isMemoryMappable && oldIdxBufferEntry->_stagingBuffer == nullptr)
            {
                oldIdxBufferEntry->_stagingBuffer = VKUtil::createStagingBuffer(_indexBufferSize, bufferName);
            }

            Byte* mappedRange = nullptr;
            if ( !oldIdxBufferEntry->_isMemoryMappable )
            {
                mappedRange = (Byte*)oldIdxBufferEntry->_stagingBuffer->_allocInfo.pMappedData;
            }
            else
            {
                if ( oldIdxBufferEntry->_stagingBuffer )
                {
                    oldIdxBufferEntry->_stagingBuffer.reset();
                }

                mappedRange = (Byte*)oldIdxBufferEntry->_buffer->_allocInfo.pMappedData;
            }

            if (data == nullptr) {
                memset( mappedRange, 0, range);
            } else {
                memcpy( mappedRange, data, range);
            }

            if ( !oldIdxBufferEntry->_isMemoryMappable )
            {
                VKTransferQueue::TransferRequest request{};
                request.srcOffset = 0u;
                request.dstOffset = 0u;
                request.size = range;
                request.srcBuffer = oldIdxBufferEntry->_stagingBuffer->_buffer;
                request.dstBuffer = oldIdxBufferEntry->_buffer->_buffer;
                request.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
                request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                request._immediate = isNewBuffer;
                VK_API::RegisterTransferRequest(request);
            }
        }

        DIVIDE_ASSERT( oldIdxBufferEntry->_buffer.get() != nullptr );

        return BufferLock
        {
            ._range = {0u, params._elementCount},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = oldIdxBufferEntry->_buffer.get()
        };
    }

    BufferLock vkGenericVertexData::updateBuffer(U32 buffer,
                                                 U32 elementCountOffset,
                                                 U32 elementCountRange,
                                                 bufferPtr data) noexcept
    {
        GenericBufferImpl* impl = nullptr;
        for (auto& bufferImpl : _bufferObjects) {
            if (bufferImpl._bindConfig._bufferIdx == buffer) {
                impl = &bufferImpl;
                break;
            }
        }

        const BufferParams& bufferParams = impl->_buffer->_params;
        DIVIDE_ASSERT(bufferParams._flags._updateFrequency != BufferUpdateFrequency::ONCE);

        DIVIDE_ASSERT(impl != nullptr, "vkGenericVertexData error: set buffer called for invalid buffer index!");

        // Calculate the size of the data that needs updating
        const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
        // Calculate the offset in the buffer in bytes from which to start writing
        size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;
        const size_t bufferSizeInBytes = bufferParams._elementCount * bufferParams._elementSize;
        DIVIDE_ASSERT(offsetInBytes + dataCurrentSizeInBytes <= bufferSizeInBytes);

        if (impl->_ringSizeFactor > 1u) {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        Byte* mappedRange = nullptr;
        if (!impl->_isMemoryMappable)
        {
            mappedRange = (Byte*)impl->_stagingBuffer->_allocInfo.pMappedData;
        }
        else
        {
            if (!impl->_lockManager->waitForLockedRange(offsetInBytes, dataCurrentSizeInBytes)) {
                DIVIDE_UNEXPECTED_CALL();
            }

            mappedRange = (Byte*)impl->_buffer->_allocInfo.pMappedData;
        }

        if (data == nullptr) {
            memset(&mappedRange[offsetInBytes], 0, dataCurrentSizeInBytes);
        } else {
            memcpy(&mappedRange[offsetInBytes], data, dataCurrentSizeInBytes);
        }

        if ( !impl->_isMemoryMappable )
        {
            VKTransferQueue::TransferRequest request{};
            request.srcOffset = offsetInBytes;
            request.dstOffset = offsetInBytes;
            request.size = dataCurrentSizeInBytes;
            request.srcBuffer = impl->_stagingBuffer->_buffer;
            request.dstBuffer = impl->_buffer->_buffer;
            request.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            VK_API::RegisterTransferRequest(request);

            return {};
        }

        return BufferLock
        {
            ._range = {0u, dataCurrentSizeInBytes},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = impl->_buffer.get()
        };
    }
}; //namespace Divide
