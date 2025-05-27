

#include "Headers/vkGenericVertexData.h"
#include "Headers/vkBufferImpl.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/Headers/LockManager.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {
    vkGenericVertexData::vkGenericVertexData(GFXDevice& context, const U16 ringBufferLength, const std::string_view name)
        : GenericVertexData(context, ringBufferLength, name)
    {
    }

    vkGenericVertexData::~vkGenericVertexData()
    {
        _context.getPerformanceMetrics()._gpuBufferCount = TotalBufferCount();
    }

    void vkGenericVertexData::reset()
    {
        _bufferObjects.clear();

        LockGuard<SharedMutex> w_lock( _idxBufferLock );
        _indexBuffer = {};

        _context.getPerformanceMetrics()._gpuBufferCount = TotalBufferCount();
    }

    void vkGenericVertexData::draw(const GenericDrawCommand& command, VDIUserData* userData) noexcept
    {
        vkUserData* vkData = static_cast<vkUserData*>(userData);

        PROFILE_VK_EVENT_AUTO_AND_CONTEX( *vkData->_cmdBuffer );

        for (const auto& buffer : _bufferObjects) 
        {
            bindBufferInternal(buffer._bindConfig, *vkData->_cmdBuffer);
        }

        SharedLock<SharedMutex> w_lock( _idxBufferLock );
        if (_indexBuffer._bufferSize > 0u )
        {
            if (_indexBuffer._buffer != nullptr)
            {
                VkDeviceSize offsetInBytes = 0u;

                if (_indexBuffer._ringSizeFactor > 1 )
                {
                    offsetInBytes += _indexBuffer._buffer->_params._elementCount * _indexBuffer._buffer->_params._elementSize * queueIndex();
                }
                VK_PROFILE(vkCmdBindIndexBuffer, *vkData->_cmdBuffer, _indexBuffer._buffer->_buffer, offsetInBytes, _indexBuffer._data.smallIndices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            }

            // Submit the draw command
            const bool indexed = _indexBuffer._buffer != nullptr;
            VK_PROFILE(VKUtil::SubmitRenderCommand, command, *vkData->_cmdBuffer, indexed );
        }
        else
        {
            PROFILE_VK_EVENT( "Submit non-indexed" );
            VKUtil::SubmitRenderCommand( command, *vkData->_cmdBuffer );
        }

    }

    void vkGenericVertexData::bindBufferInternal(const SetBufferParams::BufferBindConfig& bindConfig, VkCommandBuffer& cmdBuffer)
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

        GenericBufferImpl* impl = nullptr;
        for (auto& bufferImpl : _bufferObjects)
        {
            if (bufferImpl._bindConfig._bufferIdx == bindConfig._bufferIdx)
            {
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

        VK_PROFILE(vkCmdBindVertexBuffers, cmdBuffer, bindConfig._bindIdx, 1, &impl->_buffer->_buffer, &offsetInBytes);
    }

    BufferLock vkGenericVertexData::setBuffer(const SetBufferParams& params) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( params._bufferParams._usageType != BufferUsageType::COUNT );

        // Make sure we specify buffers in order.
        GenericBindableBufferImpl* impl = nullptr;
        for (auto& buffer : _bufferObjects)
        {
            if (buffer._bindConfig._bufferIdx == params._bindConfig._bufferIdx)
            {
                impl = &buffer;
                break;
            }
        }

        if (impl == nullptr)
        {
            impl = &_bufferObjects.emplace_back();
        }

        const size_t ringSizeFactor = params._useRingBuffer ? queueLength() : 1;

        impl->_ringSizeFactor = ringSizeFactor;
        impl->_bindConfig = params._bindConfig;
        impl->_elementStride = params._elementStride == SetBufferParams::INVALID_ELEMENT_STRIDE
                                                            ? params._bufferParams._elementSize
                                                            : params._elementStride;;

        if (_indexBuffer._buffer != nullptr &&
            _indexBuffer._buffer->_params._elementSize == params._bufferParams._elementSize &&
            _indexBuffer._buffer->_params._hostVisible == params._bufferParams._hostVisible &&
            _indexBuffer._buffer->_params._updateFrequency == params._bufferParams._updateFrequency &&
            _indexBuffer._buffer->_params._elementCount >= params._bufferParams._elementCount)
        {
            return updateBuffer( params._bindConfig._bufferIdx, 0, params._bufferParams._elementCount, params._initialData.first);
        }

        const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_VTX_BUFFER_{}", handle()._id) : string(_name.c_str()) + "_VTX_BUFFER";
        const size_t bufferSizeInBytes = params._bufferParams._elementCount * params._bufferParams._elementSize;

        impl->_buffer = std::make_unique<vkBufferImpl>(params._bufferParams,
                                                         bufferSizeInBytes,
                                                         ringSizeFactor,
                                                         params._initialData,
                                                         bufferName.c_str());

        for (U32 i = 1u; i < ringSizeFactor; ++i)
        {
            const BufferRange<> range = { i * bufferSizeInBytes , params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes };
            impl->_buffer->writeBytes(range,
                                      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                      params._initialData.first);
        }

        _context.getPerformanceMetrics()._gpuBufferCount = TotalBufferCount();

        return BufferLock
        {
            ._range = {0u, bufferSizeInBytes * ringSizeFactor},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = impl->_buffer.get()
        };
    }

    BufferLock vkGenericVertexData::setIndexBuffer(const IndexBuffer& indices)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        LockGuard<SharedMutex> w_lock( _idxBufferLock );

        if (_indexBuffer._buffer != nullptr )
        {
            DIVIDE_ASSERT(_indexBuffer._buffer->_buffer != VK_NULL_HANDLE);

            if ( indices.count == 0u || // We don't need indices anymore
                _indexBuffer._data.dynamic != indices.dynamic || // Buffer usage mode changed
                _indexBuffer._data.count < indices.count ) // Buffer not big enough
            {
                DIVIDE_EXPECTED_CALL ( _indexBuffer._buffer->waitForLockedRange( {0, U32_MAX} ) );
                _indexBuffer = {};
            }
        }

        _indexBuffer._data = indices;

        if (_indexBuffer._data.count == 0u )
        {
            // That's it. We have a buffer entry but no VK buffer associated with it, so it won't be used
            return {};
        }

        SCOPE_EXIT
        {
            _indexBuffer._data.smallIndicesTemp.clear();
        };

        bufferPtr data = _indexBuffer._data.data;
        if (data != nullptr && _indexBuffer._data.indicesNeedCast )
        {
            _indexBuffer._data.smallIndicesTemp.resize(_indexBuffer._data.count );
            const U32* const dataIn = reinterpret_cast<U32*>(data);
            for ( size_t i = 0u; i < _indexBuffer._data.count; ++i )
            {
                _indexBuffer._data.smallIndicesTemp[i] = to_U16( dataIn[i] );
            }
            data = _indexBuffer._data.smallIndicesTemp.data();
        }

        const size_t elementSize = _indexBuffer._data.smallIndices ? sizeof(U16) : sizeof(U32);
        const BufferUpdateFrequency udpateFrequency = _indexBuffer._data.dynamic ? BufferUpdateFrequency::OFTEN : BufferUpdateFrequency::OCASSIONAL;

        if (_indexBuffer._buffer != nullptr &&
            _indexBuffer._buffer->_params._elementSize == elementSize &&
            _indexBuffer._buffer->_params._updateFrequency == udpateFrequency &&
            _indexBuffer._buffer->_params._elementCount >= _indexBuffer._data.count)
        {
            return updateIndexBuffer(0u, to_U32(_indexBuffer._data.count), data);
        }

        const size_t ringSizeFactor = _indexBuffer._data.useRingBuffer ? queueLength() : 1;
        const size_t bufferSizeInBytes = _indexBuffer._data.count * elementSize;

        _indexBuffer._bufferSize = bufferSizeInBytes;
        _indexBuffer._data = _indexBuffer._data;
        _indexBuffer._ringSizeFactor = ringSizeFactor;

        BufferParams params{};
        params._hostVisible = false;
        params._updateFrequency = udpateFrequency;
        params._usageType = BufferUsageType::INDEX_BUFFER;
        params._elementCount = to_U32(_indexBuffer._data.count);
        params._elementSize = elementSize;
        
        const std::pair<bufferPtr, size_t> initialData = { data, data == nullptr ? 0u : bufferSizeInBytes };

        const string bufferName = _name.empty() ? Util::StringFormat( "DVD_GENERAL_IDX_BUFFER_{}", handle()._id ) : string(_name.c_str()) + "_IDX_BUFFER";
        _indexBuffer._buffer = std::make_unique<vkBufferImpl>( params,
                                                               bufferSizeInBytes,
                                                               ringSizeFactor,
                                                               initialData,
                                                               bufferName.c_str() );

        for (U32 i = 1u; i < ringSizeFactor; ++i)
        {
            const BufferRange<> range = { i * bufferSizeInBytes , bufferSizeInBytes };
            _indexBuffer._buffer->writeBytes(range,
                                             VK_ACCESS_INDEX_READ_BIT,
                                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                             data);
        }

        _context.getPerformanceMetrics()._gpuBufferCount = TotalBufferCount();

        return BufferLock
        {
            ._range = {0u, bufferSizeInBytes * ringSizeFactor},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = _indexBuffer._buffer.get()
        };
    }

    BufferLock vkGenericVertexData::updateBuffer(GenericBufferImpl& buffer,
                                                 const U32 elementCountOffset,
                                                 const U32 elementCountRange,
                                                 const VkAccessFlags2 dstAccessMask,
                                                 const VkPipelineStageFlags2 dstStageMask,
                                                 bufferPtr data)
    {
        const BufferParams& bufferParams = buffer._buffer->_params;
        //DIVIDE_ASSERT(bufferParams._updateFrequency != BufferUpdateFrequency::ONCE);

        // Calculate the size of the data that needs updating
        const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
        // Calculate the offset in the buffer in bytes from which to start writing
        size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;
        const size_t bufferSizeInBytes = bufferParams._elementCount * bufferParams._elementSize;
        DIVIDE_ASSERT(offsetInBytes + dataCurrentSizeInBytes <= bufferSizeInBytes);

        if (buffer._ringSizeFactor > 1u)
        {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        DIVIDE_EXPECTED_CALL( buffer._buffer->waitForLockedRange({ offsetInBytes, dataCurrentSizeInBytes }) );

        const BufferRange<> range = { offsetInBytes , dataCurrentSizeInBytes };
        return buffer._buffer->writeBytes(range, dstAccessMask, dstStageMask, data);
    }

    BufferLock vkGenericVertexData::updateBuffer(const U32 buffer,
                                                 const U32 elementCountOffset,
                                                 const U32 elementCountRange,
                                                 bufferPtr data) noexcept
    {
        GenericBufferImpl* impl = nullptr;
        for (auto& bufferImpl : _bufferObjects)
        {
            if (bufferImpl._bindConfig._bufferIdx == buffer)
            {
                impl = &bufferImpl;
                break;
            }
        }

        DIVIDE_ASSERT(impl != nullptr, "vkGenericVertexData error: set buffer called for invalid buffer index!");

        return updateBuffer(*impl, elementCountOffset, elementCountRange, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, data);
    }

    BufferLock vkGenericVertexData::updateIndexBuffer(const U32 elementCountOffset,
                                                      const U32 elementCountRange,
                                                      bufferPtr data) noexcept
    {
        return updateBuffer(_indexBuffer, elementCountOffset, elementCountRange, VK_ACCESS_INDEX_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, data);
    }

}; //namespace Divide
