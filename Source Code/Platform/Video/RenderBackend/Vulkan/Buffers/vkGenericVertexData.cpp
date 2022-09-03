#include "stdafx.h"

#include "Headers/vkGenericVertexData.h"

#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Core/Headers/StringHelper.h"

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

        for (const auto& buffer : _bufferObjects) {
            bindBufferInternal(buffer._bindConfig, *vkData->_cmdBuffer);
        }
        const auto& idxBuffer = _idxBuffers[command._bufferFlag];
        if (idxBuffer._handle != nullptr) {
            vkCmdBindIndexBuffer(*vkData->_cmdBuffer, idxBuffer._handle->_buffer, 0u, idxBuffer._data.smallIndices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        }
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

    void vkGenericVertexData::setBuffer(const SetBufferParams& params) noexcept {
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

        const size_t ringSizeFactor = params._useRingBuffer ? queueLength() : 1;
        const size_t bufferSizeInBytes = params._bufferParams._elementCount * params._bufferParams._elementSize;
        const size_t dataSize = bufferSizeInBytes * ringSizeFactor;

        const size_t elementStride = params._elementStride == SetBufferParams::INVALID_ELEMENT_STRIDE
                                                            ? params._bufferParams._elementSize
                                                            : params._elementStride;
        impl->_ringSizeFactor = ringSizeFactor;
        impl->_useAutoSyncObjects = params._useAutoSyncObjects;
        impl->_bindConfig = params._bindConfig;
        impl->_elementStride = elementStride;
        impl->_buffer = eastl::make_unique<AllocatedBuffer>(BufferUsageType::VERTEX_BUFFER);
        impl->_buffer->_params = params._bufferParams;

        const size_t localDataSize = params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes;

        impl->_stagingBufferVtx = VKUtil::createStagingBuffer(dataSize);
        Byte* mappedRange = (Byte*)impl->_stagingBufferVtx->_allocInfo.pMappedData;
        for (U32 i = 0u; i < ringSizeFactor; ++i) {
            if (params._initialData.first == nullptr) {
                memset(&mappedRange[i * bufferSizeInBytes], 0, bufferSizeInBytes);
            } else {
                memcpy(&mappedRange[i * bufferSizeInBytes], params._initialData.first, localDataSize);
            }
        }
        // Let the VMA library know that this buffer should be readable by the GPU only
        VmaAllocationCreateInfo vmaallocInfo{};
        vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaallocInfo.flags = 0;

        // Allocate the vertex buffer (as a vb buffer and transfer destination)
        {
            VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dataSize);
            UniqueLock<Mutex> w_lock(VK_API::GetStateTracker()->_allocatorInstance._allocatorLock);
            VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()->_allocatorInstance._allocator,
                                     &bufferInfo,
                                     &vmaallocInfo,
                                     &impl->_buffer->_buffer,
                                     &impl->_buffer->_allocation,
                                     &impl->_buffer->_allocInfo));

            VkMemoryPropertyFlags memPropFlags;
            vmaGetAllocationMemoryProperties(*VK_API::GetStateTracker()->_allocatorInstance._allocator,
                                             impl->_buffer->_allocation,
                                             &memPropFlags);

            const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_VTX_BUFFER_%d", handle()._id) : (_name + "_VTX_BUFFER");
            vmaSetAllocationName(*VK_API::GetStateTracker()->_allocatorInstance._allocator, impl->_buffer->_allocation, bufferName.c_str());
        }

        // Queue a command to copy from the staging buffer to the vertex buffer
        VKTransferQueue::TransferRequest request{};
        request.srcOffset = 0u;
        request.dstOffset = 0u;
        request.size = dataSize;
        request.srcBuffer = impl->_stagingBufferVtx->_buffer;
        request.dstBuffer = impl->_buffer->_buffer;
        request.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        request._immediate = true;
        VK_API::RegisterTransferRequest(request);

        if (params._bufferParams._updateFrequency == BufferUpdateFrequency::ONCE) {
            impl->_stagingBufferVtx.reset();
        }
    }

    void vkGenericVertexData::setIndexBuffer(const IndexBuffer& indices) {
        IndexBufferEntry* oldIdxBufferEntry = nullptr;

        bool found = false;
        for (auto& idxBuffer : _idxBuffers) {
            if (idxBuffer._data.id == indices.id) {
                oldIdxBufferEntry = &idxBuffer;
                found = true;
                break;
            }
        }

        if (!found) {
            _idxBuffers.emplace_back();
            oldIdxBufferEntry = &_idxBuffers.back();
            oldIdxBufferEntry->_data = indices;
        } else if (oldIdxBufferEntry->_handle != nullptr &&
                   oldIdxBufferEntry->_handle->_buffer != VK_NULL_HANDLE &&
                  (!AreCompatible(oldIdxBufferEntry->_data, indices) || indices.count == 0u))
        {
            oldIdxBufferEntry->_handle.reset();
            oldIdxBufferEntry->_stagingBufferIdx.reset();
        }

        if (!oldIdxBufferEntry->_handle) {
            oldIdxBufferEntry->_handle = eastl::make_unique<AllocatedBuffer>(BufferUsageType::INDEX_BUFFER);
        }

        IndexBuffer& oldIdxBuffer = oldIdxBufferEntry->_data;
        oldIdxBuffer.count = std::max(oldIdxBuffer.count, indices.count);

        if (indices.count > 0u) {
            const size_t elementSize = indices.smallIndices ? sizeof(U16) : sizeof(U32);

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

            bool isNewBuffer = false;
            if (oldIdxBufferEntry->_handle->_buffer == VK_NULL_HANDLE) {
                isNewBuffer = true;

                const size_t newDataSize = indices.count * elementSize;
                _indexBufferSize = std::max(newDataSize, _indexBufferSize);

                //allocate vertex buffer
                const VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, _indexBufferSize);

                //let the VMA library know that this data should be writeable by CPU, but also readable by GPU
                VmaAllocationCreateInfo vmaallocInfo = {};
                vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                vmaallocInfo.flags = 0;

                //allocate the buffer
                UniqueLock<Mutex> w_lock(VK_API::GetStateTracker()->_allocatorInstance._allocatorLock);
                VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()->_allocatorInstance._allocator,
                                         &bufferInfo,
                                         &vmaallocInfo,
                                         &oldIdxBufferEntry->_handle->_buffer,
                                         &oldIdxBufferEntry->_handle->_allocation,
                                         &oldIdxBufferEntry->_handle->_allocInfo));

                VkMemoryPropertyFlags memPropFlags;
                vmaGetAllocationMemoryProperties(*VK_API::GetStateTracker()->_allocatorInstance._allocator,
                                                 oldIdxBufferEntry->_handle->_allocation,
                                                 &memPropFlags);

                const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_IDX_BUFFER_%d", handle()._id) : (_name + "_IDX_BUFFER");
                vmaSetAllocationName(*VK_API::GetStateTracker()->_allocatorInstance._allocator, oldIdxBufferEntry->_handle->_allocation, bufferName.c_str());
            }

            const size_t range = indices.count * elementSize;
            DIVIDE_ASSERT(range <= _indexBufferSize);

            if (oldIdxBufferEntry->_stagingBufferIdx == nullptr) {
                oldIdxBufferEntry->_stagingBufferIdx = VKUtil::createStagingBuffer(_indexBufferSize);
            }

            if (data == nullptr) {
                memset(oldIdxBufferEntry->_stagingBufferIdx->_allocInfo.pMappedData, 0, range);
            } else {
                memcpy(oldIdxBufferEntry->_stagingBufferIdx->_allocInfo.pMappedData, data, range);
            }

            VKTransferQueue::TransferRequest request{};
            request.srcOffset = 0u;
            request.dstOffset = 0u;
            request.size = range;
            request.srcBuffer = oldIdxBufferEntry->_stagingBufferIdx->_buffer;
            request.dstBuffer = oldIdxBufferEntry->_handle->_buffer;
            request.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            request._immediate = isNewBuffer;
            VK_API::RegisterTransferRequest(request);

            if (!indices.dynamic) {
                oldIdxBufferEntry->_stagingBufferIdx.reset();
            }
        }
    }

    void vkGenericVertexData::updateBuffer(U32 buffer,
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

        DIVIDE_ASSERT(impl != nullptr && impl->_stagingBufferVtx != nullptr, "vkGenericVertexData error: set buffer called for invalid buffer index!");

        const BufferParams& bufferParams = impl->_buffer->_params;
        DIVIDE_ASSERT(bufferParams._updateFrequency != BufferUpdateFrequency::ONCE);

        // Calculate the size of the data that needs updating
        const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
        // Calculate the offset in the buffer in bytes from which to start writing
        size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;
        const size_t bufferSizeInBytes = bufferParams._elementCount * bufferParams._elementSize;
        DIVIDE_ASSERT(offsetInBytes + dataCurrentSizeInBytes < bufferSizeInBytes);

        if (impl->_ringSizeFactor > 1u) {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        Byte* mappedRange = (Byte*)impl->_stagingBufferVtx->_allocInfo.pMappedData;
        if (data == nullptr) {
            memset(&mappedRange[offsetInBytes], 0, dataCurrentSizeInBytes);
        } else {
            memcpy(&mappedRange[offsetInBytes], data, dataCurrentSizeInBytes);
        }

        VKTransferQueue::TransferRequest request{};
        request.srcOffset = offsetInBytes;
        request.dstOffset = offsetInBytes;
        request.size = dataCurrentSizeInBytes;
        request.srcBuffer = impl->_stagingBufferVtx->_buffer;
        request.dstBuffer = impl->_buffer->_buffer;
        request.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        VK_API::RegisterTransferRequest(request);
    }
}; //namespace Divide
