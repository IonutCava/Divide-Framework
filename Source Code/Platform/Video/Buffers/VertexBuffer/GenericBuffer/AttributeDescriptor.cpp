#include "stdafx.h"

#include "Headers/AttributeDescriptor.h"

namespace Divide {

AttributeDescriptor::AttributeDescriptor() noexcept
  : _index(0),
    _parentBuffer(0),
    _componentsPerElement(0),
    _interleavedOffset(0),
    _wasSet(false),
    _dirty(true),
    _normalized(false),
    _strideInBytes(0),
    _type(GFXDataFormat::UNSIGNED_INT)
{
}

void AttributeDescriptor::set(const U32 bufferIndex, const U32 componentsPerElement, const GFXDataFormat dataType) noexcept {
    set(bufferIndex, componentsPerElement, dataType, false);
}

void AttributeDescriptor::set(const U32 bufferIndex,
                              const U32 componentsPerElement,
                              const GFXDataFormat dataType,
                              const bool normalized) noexcept {
    set(bufferIndex, componentsPerElement, dataType, normalized, 0);
}

void AttributeDescriptor::set(const U32 bufferIndex,
                              const U32 componentsPerElement,
                              const GFXDataFormat dataType,
                              const bool normalized,
                              const size_t strideInBytes) noexcept {

    this->bufferIndex(bufferIndex);
    this->componentsPerElement(componentsPerElement);
    this->normalized(normalized);
    this->strideInBytes(strideInBytes);
    this->dataType(dataType);
}

void AttributeDescriptor::attribIndex(const U32 index) noexcept {
    _index = index;
    _dirty = true;
    _wasSet = false;
}

void AttributeDescriptor::strideInBytes(const size_t strideInBytes) noexcept {
    _strideInBytes = strideInBytes;
    _dirty = true;
    _wasSet = false;
}

void AttributeDescriptor::bufferIndex(const U32 bufferIndex) noexcept {
    _parentBuffer = bufferIndex;
    _dirty = true;
    _wasSet = false;
}

void AttributeDescriptor::componentsPerElement(const U32 componentsPerElement) noexcept {
    _componentsPerElement = componentsPerElement;
    _dirty = true;
}

void AttributeDescriptor::normalized(const bool normalized) noexcept {
    _normalized = normalized;
    _dirty = true;
}

void AttributeDescriptor::dataType(const GFXDataFormat type) noexcept {
    _type = type;
    _dirty = true;
}

void AttributeDescriptor::interleavedOffsetInBytes(const U32 interleavedOffsetInBytes) noexcept {
    _interleavedOffset = interleavedOffsetInBytes;
    _dirty = true;
}

void AttributeDescriptor::wasSet(const bool wasSet) noexcept {
    _wasSet = wasSet;
    if (!_wasSet) {
        _dirty = true;
    }
}

void AttributeDescriptor::clean() noexcept {
    _dirty = false;
}

}; //namespace Divide