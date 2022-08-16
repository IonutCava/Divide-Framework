#include "stdafx.h"

#include "Headers/DescriptorSets.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

    size_t ImageView::getHash() const {
        _hash = GetHash(_textureData);
        Util::Hash_combine(_hash, _targetType, _mipLevels.x, _mipLevels.y, _layerRange.x, _layerRange.y);
        return _hash;
    }

    size_t ImageViewEntry::getHash() const {
        _hash = _view.getHash();
        Util::Hash_combine(_hash, _descriptor.getHash());
        return _hash;
    }

    bool operator==(const Image& lhs, const Image& rhs) noexcept {
        return lhs._flag == rhs._flag &&
               lhs._layered == rhs._layered &&
               lhs._layer == rhs._layer &&
               lhs._level == rhs._level &&
               Compare(lhs._texture, rhs._texture);
    }

    bool operator!=(const Image& lhs, const Image& rhs) noexcept {
        return lhs._flag != rhs._flag ||
               lhs._layered != rhs._layered ||
               lhs._layer != rhs._layer ||
               lhs._level != rhs._level ||
               !Compare(lhs._texture, rhs._texture);
    }

    bool operator==(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept {
        return lhs._resource == rhs._resource;
    }

    bool operator!=(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept {
        return lhs._resource != rhs._resource;
    }

    bool operator==(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept {
        return lhs._range == rhs._range &&
               Compare(lhs._buffer, rhs._buffer);
    }

    bool operator!=(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept {
        return lhs._range != rhs._range ||
               !Compare(lhs._buffer, rhs._buffer);
    }

    DescriptorSetBindingType DescriptorSetBindingData::Type() const noexcept {
        switch (_resource.index()) {
        case 1: {
            const ShaderBuffer::Usage usage = As<ShaderBufferEntry>()._buffer->getUsage();
            switch (usage) {
            case ShaderBuffer::Usage::COMMAND_BUFFER:
            case ShaderBuffer::Usage::UNBOUND_BUFFER: return DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
            case ShaderBuffer::Usage::CONSTANT_BUFFER: return DescriptorSetBindingType::UNIFORM_BUFFER;
            }
        } break;
        case 2: return DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        case 3: return DescriptorSetBindingType::IMAGE;
        case 4: return DescriptorSetBindingType::IMAGE_VIEW;
        default: DIVIDE_UNEXPECTED_CALL(); break;
        }
        return DescriptorSetBindingType::COUNT;
    }
}; //namespace Divide