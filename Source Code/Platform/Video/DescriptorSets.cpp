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

    size_t ImageView::getHash() const {
        _hash = GetHash(_textureData);
        Util::Hash_combine(_hash,
                           _mipLevels.x,
                           _mipLevels.y,
                           _layerRange.x,
                           _layerRange.y);
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
#if 0
    bool operator==(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept {
        if (lhs._resource.index() == rhs._resource.index()) {
            switch (lhs.Type()) {
                case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
                    return lhs.As<DescriptorCombinedImageSampler>() == rhs.As<DescriptorCombinedImageSampler>();
                case DescriptorSetBindingType::IMAGE:
                    return lhs.As<Image>() == rhs.As<Image>();
                case DescriptorSetBindingType::UNIFORM_BUFFER:
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER: 
                    return lhs.As<ShaderBufferEntry>() == rhs.As<ShaderBufferEntry>();
                default:
                    DIVIDE_UNEXPECTED_CALL();
                    break;
            };
        }

        return false;
    }

    bool operator!=(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept {
        if (lhs._resource.index() == rhs._resource.index()) {
            switch (lhs.Type()) {
            case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
                return lhs.As<DescriptorCombinedImageSampler>() != rhs.As<DescriptorCombinedImageSampler>();
            case DescriptorSetBindingType::IMAGE:
                return lhs.As<Image>() != rhs.As<Image>();
            case DescriptorSetBindingType::UNIFORM_BUFFER:
            case DescriptorSetBindingType::SHADER_STORAGE_BUFFER:
                return lhs.As<ShaderBufferEntry>() != rhs.As<ShaderBufferEntry>();
            default:
                DIVIDE_UNEXPECTED_CALL();
                break;
            };
        }

        return true;
    }
#else
    bool operator==(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept {
        return lhs._resource == rhs._resource;
    }
    bool operator!=(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept {
        return lhs._resource != rhs._resource;
    }
#endif
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
                const ShaderBuffer::Usage usage = As<ShaderBufferEntry>()._buffer->getUsage();
                switch (usage) {
                    case ShaderBuffer::Usage::COMMAND_BUFFER:
                    case ShaderBuffer::Usage::UNBOUND_BUFFER: return DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
                    case ShaderBuffer::Usage::CONSTANT_BUFFER: return DescriptorSetBindingType::UNIFORM_BUFFER;
                }
            } break;
            case 2: return DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
            case 3: return DescriptorSetBindingType::IMAGE;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }
        return DescriptorSetBindingType::COUNT;
    }
}; //namespace Divide