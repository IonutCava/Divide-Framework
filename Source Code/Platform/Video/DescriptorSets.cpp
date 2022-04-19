#include "stdafx.h"

#include "Headers/DescriptorSets.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {
    bool BufferCompare(const ShaderBuffer* const a, const ShaderBuffer* const b) noexcept {
        if (a != nullptr && b != nullptr) {
            return a->getGUID() == b->getGUID();
        }

        return a == nullptr && b == nullptr;
    }

    bool ShaderBufferBinding::set(const ShaderBufferBinding& other) noexcept {
        return set(other._binding, other._buffer, other._elementRange, other._lockType);
    }

    bool ShaderBufferBinding::set(const ShaderBufferLocation binding,
                                  ShaderBuffer* buffer,
                                  const vec2<U32>& elementRange,
                                  const ShaderBufferLockType lockType) noexcept {
        bool ret = false;
        if (_binding != binding) {
            _binding = binding;
            ret = true;
        }
        if (_buffer != buffer) {
            _buffer = buffer;
            ret = true;
        }
        if (_elementRange != elementRange) {
            _elementRange.set(elementRange);
            ret = true;
        }
        if (_lockType != lockType) {
            _lockType = lockType;
            ret = true;
        }

        return ret;
    }

    bool IsEmpty(const DescriptorSet& set) noexcept {
        return set._textureData.empty() &&
               set._buffers.empty() &&
               set._textureViews.empty() &&
               set._images.empty();
    }

    namespace {
        FORCE_INLINE const TextureEntry* FindTextureDataEntry(const TextureDataContainer& source, const U8 binding) noexcept {
            for (const TextureEntry& it : source._entries) {
                if (it._binding == binding) {
                    return &it;
                }
            }

            return nullptr;
        };

        FORCE_INLINE const TextureViewEntry* FindTextureViewEntry(const TextureViews& source, const U8 binding) noexcept {
            for (const TextureViewEntry& it : source._entries) {
                if (it._binding == binding) {
                    return &it;
                }
            }

            return nullptr;
        };

        FORCE_INLINE const Image* FindImage(const Images& source, const U8 binding) noexcept {
            for (const auto& it : source._entries) {
                if (it._binding == binding) {
                    return &it;
                }
            }

            return nullptr;
        };
    };

    bool Merge(const DescriptorSet &lhs, DescriptorSet &rhs, bool& partial) {
        for (auto* it = begin(rhs._textureData._entries); it != end(rhs._textureData._entries);) {
            if (it->_binding != INVALID_TEXTURE_BINDING) {
                ++it;
                continue;
            }

            const TextureEntry* texData = FindTextureDataEntry(lhs._textureData, it->_binding);
            if (texData != nullptr && *texData == *it) {
                rhs._textureData.remove(it->_binding);
                partial = true;
            } else {
                ++it;
            }
        }

        for (auto* it = begin(rhs._textureViews._entries); it != end(rhs._textureViews._entries);) {
            const TextureViewEntry* texViewData = FindTextureViewEntry(lhs._textureViews, it->_binding);
            if (texViewData != nullptr && texViewData->_view == it->_view) {
                it = rhs._textureViews._entries.erase(it);
                partial = true;
            } else {
                ++it;
            }
        }


        for (auto* it = begin(rhs._images._entries); it != end(rhs._images._entries);) {
            const Image* image = FindImage(lhs._images, it->_binding);
            if (image != nullptr && *image == *it) {
                it = rhs._images._entries.erase(it);
                partial = true;
            } else {
                ++it;
            }
        }

        for (U8 i = 0u; i < rhs._buffers.count(); ++i) {
            const ShaderBufferBinding& it = rhs._buffers._entries[i];
            const ShaderBufferBinding* binding = lhs._buffers.find(it._binding);
            if (binding != nullptr && *binding == it) {
                partial = rhs._buffers.remove(it._binding) || partial;
            }
        }

        return IsEmpty(rhs);
    }

    size_t TextureView::getHash() const {
        _hash = GetHash(_textureData);
        Util::Hash_combine(_hash, _targetType, _mipLevels.x, _mipLevels.y, _layerRange.x, _layerRange.y);
        return _hash;
    }

    size_t TextureViewEntry::getHash() const {
        _hash = _view.getHash();
        Util::Hash_combine(_hash, _binding);
        return _hash;
    }

    bool TextureDataContainer::remove(const U8 binding) {
        for (auto it = begin(_entries); it != end(_entries); ++it) {
            if (it->_binding == binding) {
                _entries.erase(it);
                return true;
            }
        }

        return false;
    }

    void TextureDataContainer::sortByBinding() {
        eastl::sort(_entries.begin(), _entries.begin() + count(), [](const TextureEntry& lhs, const TextureEntry& rhs) {
            return lhs._binding < rhs._binding;
        });
    }

    TextureUpdateState TextureDataContainer::add(const TextureEntry& entry) {
        OPTICK_EVENT();
        if (entry._binding != INVALID_TEXTURE_BINDING) {
            assert(IsValid(entry));

            for (TextureEntry& it : _entries) {
                if (it._binding == entry._binding) {
                    if (it._data != entry._data || it._sampler != entry._sampler) {
                        it._data = entry._data;
                        it._sampler = entry._sampler;
                        return TextureUpdateState::REPLACED;
                    }

                    return TextureUpdateState::NOTHING;
                }
            }

            _entries.push_back(entry);
             return TextureUpdateState::ADDED;
        }

        return TextureUpdateState::NOTHING;
    }

    bool operator==(const TextureDataContainer & lhs, const TextureDataContainer & rhs) noexcept {
        // Easy: different texture count
        if (lhs.count() != rhs.count()) {
            return false;
        }

        // Hard. See if texture data matches
        for (const TextureEntry& lhsEntry : lhs._entries) {
            bool found = false;
            for (const TextureEntry& rhsEntry : rhs._entries) {
                if (lhsEntry == rhsEntry) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(const TextureDataContainer & lhs, const TextureDataContainer & rhs) noexcept {
        // Easy: different texture count
        if (lhs.count() != rhs.count()) {
            return true;
        }

        // Hard. See if texture data matches
        for (const TextureEntry& lhsEntry : lhs._entries) {
            bool found = false;
            for (const TextureEntry& rhsEntry : rhs._entries) {
                if (lhsEntry == rhsEntry) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                return true;
            }
        }

        return false;
    }

    bool operator==(const DescriptorSet &lhs, const DescriptorSet &rhs) noexcept {
        return lhs._buffers == rhs._buffers &&
               lhs._textureViews == rhs._textureViews &&
               lhs._images == rhs._images &&
               lhs._textureData == rhs._textureData;
    }

    bool operator!=(const DescriptorSet &lhs, const DescriptorSet &rhs) noexcept {
        return lhs._buffers != rhs._buffers ||
               lhs._textureViews != rhs._textureViews ||
               lhs._images != rhs._images ||
               lhs._textureData != rhs._textureData;
    }

    bool operator==(const TextureView& lhs, const TextureView &rhs) noexcept {
        return lhs._samplerHash == rhs._samplerHash &&
               lhs._targetType == rhs._targetType &&
               lhs._mipLevels == rhs._mipLevels &&
               lhs._layerRange == rhs._layerRange &&
               lhs._textureData == rhs._textureData;
    }

    bool operator!=(const TextureView& lhs, const TextureView &rhs) noexcept {
        return lhs._samplerHash != rhs._samplerHash ||
               lhs._targetType != rhs._targetType ||
               lhs._mipLevels != rhs._mipLevels ||
               lhs._layerRange != rhs._layerRange ||
               lhs._textureData != rhs._textureData;
    }

    bool operator==(const TextureViewEntry& lhs, const TextureViewEntry &rhs) noexcept {
        return lhs._binding == rhs._binding &&
               lhs._view == rhs._view &&
               lhs._descriptor == rhs._descriptor;
    }

    bool operator!=(const TextureViewEntry& lhs, const TextureViewEntry &rhs) noexcept {
        return lhs._binding != rhs._binding ||
               lhs._view != rhs._view ||
               lhs._descriptor != rhs._descriptor;
    }

    bool operator==(const ShaderBufferBinding& lhs, const ShaderBufferBinding &rhs) noexcept {
        return lhs._lockType == rhs._lockType &&
               lhs._binding == rhs._binding &&
               lhs._elementRange == rhs._elementRange &&
               BufferCompare(lhs._buffer, rhs._buffer);
    }

    bool operator!=(const ShaderBufferBinding& lhs, const ShaderBufferBinding &rhs) noexcept {
        return lhs._lockType != rhs._lockType ||
               lhs._binding != rhs._binding ||
               lhs._elementRange != rhs._elementRange ||
               !BufferCompare(lhs._buffer, rhs._buffer);
    }

    bool operator==(const Image& lhs, const Image &rhs) noexcept {
        return lhs._flag == rhs._flag &&
               lhs._layer == rhs._layer &&
               lhs._level == rhs._level &&
               lhs._binding == rhs._binding &&
               lhs._texture == rhs._texture;
    }

    bool operator!=(const Image& lhs, const Image &rhs) noexcept {
        return lhs._flag != rhs._flag ||
               lhs._layer != rhs._layer ||
               lhs._level != rhs._level ||
               lhs._binding != rhs._binding ||
               lhs._texture != rhs._texture;
    }
}; //namespace Divide