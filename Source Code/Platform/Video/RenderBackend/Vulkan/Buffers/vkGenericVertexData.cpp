#include "stdafx.h"

#include "Headers/vkGenericVertexData.h"

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

    void vkGenericVertexData::draw([[maybe_unused]] const GenericDrawCommand& command) noexcept {
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
        impl->_buffer = eastl::make_unique<AllocatedBuffer>();
        impl->_buffer->_params = params._bufferParams;
        impl->_buffer->_usageType = BufferUsageType::VERTEX_BUFFER;

        // allocate vertex buffer
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        //this is the total size, in bytes, of the buffer we are allocating
        bufferInfo.size = dataSize;
        //this buffer is going to be used as a Vertex Buffer
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        //let the VMA library know that this data should be writeable by CPU, but also readable by GPU
        VmaAllocationCreateInfo vmaallocInfo = {};
        vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        //allocate the buffer
        VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()->_allocator,
                                 &bufferInfo,
                                 &vmaallocInfo,
                                 &impl->_buffer->_buffer,
                                 &impl->_buffer->_allocation,
                                 &impl->_buffer->_allocInfo));

        VkMemoryPropertyFlags memPropFlags;
        vmaGetAllocationMemoryProperties(*VK_API::GetStateTracker()->_allocator,
                                         impl->_buffer->_allocation,
                                         &memPropFlags);

        const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_VTX_BUFFER_%d", handle()._id) : (_name + "_VTX_BUFFER");
        vmaSetAllocationName(*VK_API::GetStateTracker()->_allocator, impl->_buffer->_allocation, bufferName.c_str());

        const size_t localDataSize = params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes;
        void* mappedData;
        vmaMapMemory(*VK_API::GetStateTracker()->_allocator, impl->_buffer->_allocation, &mappedData);
        Byte* mappedRange = (Byte*)mappedData;
        for (U32 i = 0u; i < ringSizeFactor; ++i) {
            if (params._initialData.first == nullptr) {
                memset(&mappedRange[i * bufferSizeInBytes], 0, bufferSizeInBytes);
            } else {
                memcpy(&mappedRange[i * bufferSizeInBytes], params._initialData.first, localDataSize);
            }
        }
        vmaUnmapMemory(*VK_API::GetStateTracker()->_allocator, impl->_buffer->_allocation);
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
            oldIdxBufferEntry->_handle.reset(nullptr);
        }

        if (!oldIdxBufferEntry->_handle) {
            oldIdxBufferEntry->_handle = eastl::make_unique<AllocatedBuffer>();
        }

        oldIdxBufferEntry->_handle->_usageType = BufferUsageType::INDEX_BUFFER;

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

            if (oldIdxBufferEntry->_handle->_buffer == VK_NULL_HANDLE) {
                const size_t newDataSize = indices.count * elementSize;
                _indexBufferSize = std::max(newDataSize, _indexBufferSize);

                //allocate vertex buffer
                VkBufferCreateInfo bufferInfo = {};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                //this is the total size, in bytes, of the buffer we are allocating
                bufferInfo.size = _indexBufferSize;
                //this buffer is going to be used as a Vertex Buffer
                bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

                //let the VMA library know that this data should be writeable by CPU, but also readable by GPU
                VmaAllocationCreateInfo vmaallocInfo = {};
                vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

                //allocate the buffer
                VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()->_allocator,
                                         &bufferInfo,
                                         &vmaallocInfo,
                                         &oldIdxBufferEntry->_handle->_buffer,
                                         &oldIdxBufferEntry->_handle->_allocation,
                                         &oldIdxBufferEntry->_handle->_allocInfo));

                VkMemoryPropertyFlags memPropFlags;
                vmaGetAllocationMemoryProperties(*VK_API::GetStateTracker()->_allocator,
                                                 oldIdxBufferEntry->_handle->_allocation,
                                                 &memPropFlags);

                const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_IDX_BUFFER_%d", handle()._id) : (_name + "_IDX_BUFFER");
                vmaSetAllocationName(*VK_API::GetStateTracker()->_allocator, oldIdxBufferEntry->_handle->_allocation, bufferName.c_str());
            }

            const size_t range = indices.count * elementSize;
            DIVIDE_ASSERT(range <= _indexBufferSize);

            void* mappedData;
            vmaMapMemory(*VK_API::GetStateTracker()->_allocator, oldIdxBufferEntry->_handle->_allocation, &mappedData);
            if (data == nullptr) {
                memset(mappedData, 0, range);
            } else {
                memcpy(mappedData, data, range);
            }
            vmaUnmapMemory(*VK_API::GetStateTracker()->_allocator, oldIdxBufferEntry->_handle->_allocation);
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

        DIVIDE_ASSERT(impl != nullptr && "glGenericVertexData error: set buffer called for invalid buffer index!");

        const BufferParams& bufferParams = impl->_buffer->_params;

        // Calculate the size of the data that needs updating
        const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
        // Calculate the offset in the buffer in bytes from which to start writing
        size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;

        if (impl->_ringSizeFactor > 1u) {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        void* mappedData;
        vmaMapMemory(*VK_API::GetStateTracker()->_allocator, impl->_buffer->_allocation, &mappedData);
        Byte* mappedRange = (Byte*)mappedData;
        memcpy(&mappedRange[offsetInBytes], data, dataCurrentSizeInBytes);
        vmaUnmapMemory(*VK_API::GetStateTracker()->_allocator, impl->_buffer->_allocation);
    }
}; //namespace Divide
