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

    bool IsEmpty(const DescriptorSet& set) noexcept {
        if (set._usage != DescriptorSetUsage::COUNT) {
            for (const auto& binding : set._bindings) {
                switch (binding._type) {
                    case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: {
                        const auto& imageSampler = binding._data.As<DescriptorCombinedImageSampler>();
                        if (imageSampler._image._textureType != TextureType::COUNT &&
                            imageSampler._image._textureHandle != 0u)
                        {
                            // We have a valid image
                            return false;
                        }
                    } break;
                    case DescriptorSetBindingType::UNIFORM_BUFFER:
                    case DescriptorSetBindingType::SHADER_STORAGE_BUFFER:
                    case DescriptorSetBindingType::ATOMIC_BUFFER: {
                        if (binding._data.As<ShaderBufferEntry>()._buffer != nullptr)
                        {
                            // We have a buffer
                            return false;
                        }
                    } break;
                    case DescriptorSetBindingType::IMAGE: {
                        if (binding._data.As<Image>()._texture != nullptr) {
                            // We have an image
                            return false;
                        }
                    } break;
                    case DescriptorSetBindingType::IMAGE_VIEW: {
                        if (binding._data.As<ImageViewEntry>().isValid()) {
                            // We have a valid image view
                            return false;
                        }
                    } break;
                    // empty slot
                    default: break;
                }
            }
        }

        return true;
    }
    
    DescriptorUpdateResult UpdateBinding(DescriptorSet& set, const DescriptorSetBinding& binding) {
        for (auto& crtBinding : set._bindings) {
            if (crtBinding._resourceSlot == binding._resourceSlot) {
                if (crtBinding._type != binding._type ||
                    crtBinding._data != binding._data ||
                    crtBinding._shaderStageVisibility != binding._shaderStageVisibility)
                {
                    crtBinding = binding;
                    return DescriptorUpdateResult::OVERWRITTEN_EXISTING;
                }
                return DescriptorUpdateResult::NO_UPDATE;
            }
        }
        set._bindings.push_back(binding);
        return DescriptorUpdateResult::NEW_ENTRY_ADDED;
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
}; //namespace Divide