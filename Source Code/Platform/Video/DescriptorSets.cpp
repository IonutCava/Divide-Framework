#include "stdafx.h"

#include "Headers/DescriptorSets.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {
    ShaderBufferEntry::ShaderBufferEntry(ShaderBuffer& buffer, const BufferRange& range)
        : _buffer(&buffer)
        , _range(range)
        , _bufferQueueReadIndex(buffer.queueReadIndex())
    {
    }

    size_t ImageView::Descriptor::getHash() const {
        Util::Hash_combine(_hash, _msaaSamples, _dataType, _baseFormat, _srgb, _normalized);
        return _hash;
    }
    
    TextureType ImageView::targetType() const noexcept {
        if (_targetType != TextureType::COUNT) {
            return _targetType;
        }

        if (_srcTexture != nullptr) {
            return _srcTexture->descriptor().texType();
        }

        return _targetType;
    }

    void ImageView::targetType(const TextureType type) noexcept {
        _targetType = type;
    }

    size_t ImageView::getHash() const {
        _hash = 1337;
        Util::Hash_combine(_hash,
                           _flag,
                           (_srcTexture != nullptr ? _srcTexture->getGUID() : 17),
                            targetType(),
                           _layout,
                           _mipLevels.min,
                           _mipLevels.max,
                           _layerRange.min,
                           _layerRange.max);
        Util::Hash_combine(_hash, _descriptor.getHash());

        return _hash;
    }

    bool operator==(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept {
        return lhs._resource == rhs._resource;
    }
    bool operator!=(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept {
        return lhs._resource != rhs._resource;
    }

    bool operator==(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept {
        return lhs._bufferQueueReadIndex == rhs._bufferQueueReadIndex &&
               lhs._range == rhs._range &&
               Compare(lhs._buffer, rhs._buffer);
    }

    bool operator!=(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept {
        return lhs._bufferQueueReadIndex != rhs._bufferQueueReadIndex ||
               lhs._range != rhs._range ||
               !Compare(lhs._buffer, rhs._buffer);
    }

    DescriptorSetBindingType DescriptorSetBindingData::Type() const noexcept {
        switch (_resource.index()) {
            case 1: {
                ShaderBuffer* buffer = As<ShaderBufferEntry>()._buffer;
                const ShaderBuffer::Usage usage = buffer->getUsage();
                switch (usage) {
                    case ShaderBuffer::Usage::COMMAND_BUFFER:
                    case ShaderBuffer::Usage::UNBOUND_BUFFER: return DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
                    case ShaderBuffer::Usage::CONSTANT_BUFFER: return DescriptorSetBindingType::UNIFORM_BUFFER;
                    default: DIVIDE_UNEXPECTED_CALL();
                }
            } break;
            case 2: return DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
            case 3: return DescriptorSetBindingType::IMAGE;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }
        return DescriptorSetBindingType::COUNT;
    }
}; //namespace Divide