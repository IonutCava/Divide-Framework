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

    void vkGenericVertexData::setBuffer([[maybe_unused]] const SetBufferParams& params) noexcept {
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
