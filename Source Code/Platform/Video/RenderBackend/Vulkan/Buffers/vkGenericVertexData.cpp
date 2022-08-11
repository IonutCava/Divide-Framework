#include "stdafx.h"

#include "Headers/vkGenericVertexData.h"

namespace Divide {
    vkGenericVertexData::vkGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
        : GenericVertexData(context, ringBufferLength, name)
    {}

    void vkGenericVertexData::reset() {

    }

    void vkGenericVertexData::draw([[maybe_unused]] const GenericDrawCommand& command) noexcept {
    }

    void vkGenericVertexData::setBuffer(const SetBufferParams& params) noexcept {
        [[maybe_unused]] constexpr BufferUsageType usageType = BufferUsageType::VERTEX_BUFFER;

        // Make sure we specify buffers in order.
        genericBufferImpl* impl = nullptr;
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
        [[maybe_unused]] const size_t dataSize = bufferSizeInBytes * ringSizeFactor;

        const size_t elementStride = params._elementStride == SetBufferParams::INVALID_ELEMENT_STRIDE
                                                            ? params._bufferParams._elementSize
                                                            : params._elementStride;
        impl->_ringSizeFactor = ringSizeFactor;
        impl->_useAutoSyncObjects = params._useAutoSyncObjects;
        impl->_bindConfig = params._bindConfig;
        impl->_elementStride = elementStride;
        if (impl->_buffer == nullptr) {
            impl->_buffer = eastl::make_unique<AllocatedBuffer>();
            impl->_buffer->_usageType = BufferUsageType::VERTEX_BUFFER;
        }
    }

    void vkGenericVertexData::setIndexBuffer([[maybe_unused]] const IndexBuffer& indices) {
    }

    void vkGenericVertexData::insertFencesIfNeeded() {
    }

    void vkGenericVertexData::updateBuffer([[maybe_unused]] U32 buffer,
                                           [[maybe_unused]] U32 elementCountOffset,
                                           [[maybe_unused]] U32 elementCountRange,
                                           [[maybe_unused]] bufferPtr data) noexcept
    {
    }
}; //namespace Divide
