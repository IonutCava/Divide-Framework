#include "stdafx.h"

#include "Headers/vkGenericVertexData.h"
#include "Headers/vkBufferImpl.h"

#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/Headers/LockManager.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {
    vkGenericVertexData::vkGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const bool renderIndirect, const Str256& name)
        : GenericVertexData(context, ringBufferLength, renderIndirect, name)
    {
    }

    void vkGenericVertexData::reset()
    {
        _bufferObjects.clear();

        LockGuard<SharedMutex> w_lock( _idxBufferLock );
        _idxBuffers.clear();
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
        if ( !_idxBuffers.empty() )
        {
            DIVIDE_ASSERT( command._bufferFlag < _idxBuffers.size() );

            const auto& idxBuffer = _idxBuffers[command._bufferFlag];
            if (idxBuffer._buffer != nullptr)
            {
                VkDeviceSize offsetInBytes = 0u;

                if ( idxBuffer._ringSizeFactor > 1 )
                {
                    offsetInBytes += idxBuffer._buffer->_params._elementCount * idxBuffer._buffer->_params._elementSize * queueIndex();
                }
                vkCmdBindIndexBuffer(*vkData->_cmdBuffer, idxBuffer._buffer->_buffer, offsetInBytes, idxBuffer._data.smallIndices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            }

            {
                // Submit the draw command
                PROFILE_VK_EVENT( "Submit indexed" );
                VKUtil::SubmitRenderCommand( command, *vkData->_cmdBuffer, idxBuffer._buffer != nullptr, renderIndirect() );
            }
        }
        else
        {
            DIVIDE_ASSERT( command._bufferFlag == 0u );
            PROFILE_VK_EVENT( "Submit non-indexed" );
            VKUtil::SubmitRenderCommand( command, *vkData->_cmdBuffer, false, renderIndirect() );
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

        vkCmdBindVertexBuffers(cmdBuffer, bindConfig._bindIdx, 1, &impl->_buffer->_buffer, &offsetInBytes);
    }

    BufferLock vkGenericVertexData::setBuffer(const SetBufferParams& params) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( params._bufferParams._flags._usageType != BufferUsageType::COUNT );

        // Make sure we specify buffers in order.
        GenericBufferImpl* impl = nullptr;
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

        const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_VTX_BUFFER_%d", handle()._id) : string(_name.c_str()) + "_VTX_BUFFER";
        impl->_buffer = eastl::make_unique<vkBufferImpl>(params._bufferParams,
                                                         bufferSizeInBytes,
                                                         ringSizeFactor,
                                                         params._initialData,
                                                         bufferName.c_str());
        return BufferLock
        {
            ._range = {0u, dataSize},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = impl->_buffer.get()
        };
    }

    BufferLock vkGenericVertexData::setIndexBuffer(const IndexBuffer& indices)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        IndexBufferEntry* impl = nullptr;

        LockGuard<SharedMutex> w_lock( _idxBufferLock );
        bool found = false;
        for (auto& idxBuffer : _idxBuffers) {
            if (idxBuffer._data.id == indices.id)
            {
                impl = &idxBuffer;
                found = true;
                break;
            }
        }

        if (!found)
        {
            impl = &_idxBuffers.emplace_back();
        }
        else if ( impl->_buffer != nullptr )
        {
            DIVIDE_ASSERT(impl->_buffer->_buffer != VK_NULL_HANDLE);

            if ( indices.count == 0u || // We don't need indices anymore
                 impl->_data.dynamic != indices.dynamic || // Buffer usage mode changed
                 impl->_data.count < indices.count ) // Buffer not big enough
            {
                if ( !impl->_buffer->waitForLockedRange( {0, U32_MAX} ) )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
                impl->_buffer.reset();
            }
        }

        if ( indices.count == 0u )
        {
            // That's it. We have a buffer entry but no VK buffer associated with it, so it won't be used
            return {};
        }

        SCOPE_EXIT {
            impl->_data._smallIndicesTemp.clear();
        };

        bufferPtr data = indices.data;
        if ( indices.indicesNeedCast )
        {
            impl->_data._smallIndicesTemp.resize( indices.count );
            const U32* const dataIn = reinterpret_cast<U32*>(data);
            for ( size_t i = 0u; i < indices.count; ++i )
            {
                impl->_data._smallIndicesTemp[i] = to_U16( dataIn[i] );
            }
            data = impl->_data._smallIndicesTemp.data();
        }

        const size_t elementSize = indices.smallIndices ? sizeof( U16 ) : sizeof( U32 );
        const size_t range = indices.count * elementSize;

        if ( impl->_buffer != nullptr )
        {
            size_t offsetInBytes = 0u;
            if ( impl->_ringSizeFactor > 1u )
            {
                offsetInBytes += impl->_data.count * elementSize * queueIndex();
            }

            DIVIDE_ASSERT( range <= impl->_bufferSize );
            return impl->_buffer->writeBytes( { offsetInBytes, range }, VK_ACCESS_INDEX_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, data );
        }
        
        impl->_bufferSize = range;
        impl->_data = indices;
        impl->_ringSizeFactor = queueLength();

        BufferParams params{};
        params._flags._updateFrequency = indices.dynamic ? BufferUpdateFrequency::OFTEN : BufferUpdateFrequency::OCASSIONAL;
        params._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;
        params._flags._usageType = BufferUsageType::INDEX_BUFFER;
        params._elementSize = elementSize;
        params._elementCount = to_U32(indices.count);

        const std::pair<bufferPtr, size_t> initialData = { data, range };

        const string bufferName = _name.empty() ? Util::StringFormat( "DVD_GENERAL_IDX_BUFFER_%d", handle()._id ) : string(_name.c_str()) + "_IDX_BUFFER";
        impl->_buffer = eastl::make_unique<vkBufferImpl>( params,
                                                            impl->_bufferSize,
                                                            impl->_ringSizeFactor,
                                                            initialData,
                                                            bufferName.c_str() );
        return BufferLock
        {
            ._range = {0u, indices.count},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = impl->_buffer.get()
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

        if (impl->_ringSizeFactor > 1u)
        {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        if (!impl->_buffer->waitForLockedRange({offsetInBytes, dataCurrentSizeInBytes}))
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        const BufferRange range = { offsetInBytes , dataCurrentSizeInBytes};
        return impl->_buffer->writeBytes(range, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, data);
    }
}; //namespace Divide
