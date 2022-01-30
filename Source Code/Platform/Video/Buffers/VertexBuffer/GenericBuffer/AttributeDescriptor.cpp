#include "stdafx.h"

#include "Headers/AttributeDescriptor.h"

namespace Divide {

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

    enabled(true);
    this->parentBuffer(bufferIndex);
    this->componentsPerElement(componentsPerElement);
    this->normalized(normalized);
    this->strideInBytes(strideInBytes);
    this->dataType(dataType);
}

void AttributeDescriptor::index(const U32 index) noexcept {
    _index = index;
    _dirty = true;
    _wasSet = false;
}

void AttributeDescriptor::strideInBytes(const size_t strideInBytes) noexcept {
    _strideInBytes = strideInBytes;
    _dirty = true;
    _wasSet = false;
}

void AttributeDescriptor::parentBuffer(const U32 bufferIndex) noexcept {
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
    _dataType = type;
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

void AttributeDescriptor::enabled(const bool state) noexcept {
    if (_enabled != state) {
        _enabled = state;

        _wasSet = false;
        _dirty = true;
    }
}
}; //namespace Divide