/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _DESCRIPTOR_SETS_INL_
#define _DESCRIPTOR_SETS_INL_

namespace Divide {
    FORCE_INLINE bool operator==(const ImageView& lhs, const ImageView&rhs) noexcept {
        return lhs._samplerHash == rhs._samplerHash &&
               lhs._targetType == rhs._targetType &&
               lhs._mipLevels == rhs._mipLevels &&
               lhs._layerRange == rhs._layerRange &&
               lhs._textureData == rhs._textureData;
    }

    FORCE_INLINE bool operator!=(const ImageView& lhs, const ImageView&rhs) noexcept {
        return lhs._samplerHash != rhs._samplerHash ||
               lhs._targetType != rhs._targetType ||
               lhs._mipLevels != rhs._mipLevels ||
               lhs._layerRange != rhs._layerRange ||
               lhs._textureData != rhs._textureData;
    }

    inline bool operator==(const ImageViewEntry& lhs, const ImageViewEntry&rhs) noexcept {
        return lhs._view == rhs._view &&
               lhs._descriptor == rhs._descriptor;
    }

    inline bool operator!=(const ImageViewEntry& lhs, const ImageViewEntry&rhs) noexcept {
        return lhs._view != rhs._view ||
               lhs._descriptor != rhs._descriptor;
    }

    FORCE_INLINE bool operator==(const DescriptorCombinedImageSampler& lhs, const DescriptorCombinedImageSampler& rhs) noexcept {
        return lhs._image == rhs._image &&
               lhs._samplerHash == rhs._samplerHash;
    }

    FORCE_INLINE bool operator!=(const DescriptorCombinedImageSampler& lhs, const DescriptorCombinedImageSampler& rhs) noexcept {
        return lhs._image != rhs._image ||
               lhs._samplerHash != rhs._samplerHash;
    }

    inline bool operator==(const DescriptorSetBinding& lhs, const DescriptorSetBinding& rhs) noexcept {
        return lhs._shaderStageVisibility == rhs._shaderStageVisibility &&
               lhs._type == rhs._type &&
               lhs._resource == rhs._resource;
    }

    inline bool operator!=(const DescriptorSetBinding& lhs, const DescriptorSetBinding& rhs) noexcept {
        return lhs._shaderStageVisibility != rhs._shaderStageVisibility ||
               lhs._type != rhs._type ||
               lhs._resource != rhs._resource;
    }

    inline bool operator==(const DescriptorBindingEntry& lhs, const DescriptorBindingEntry& rhs) noexcept {
        return lhs._slot == rhs._slot &&
               lhs._data == rhs._data;
    }

    inline bool operator!=(const DescriptorBindingEntry& lhs, const DescriptorBindingEntry& rhs) noexcept {
        return lhs._slot != rhs._slot ||
               lhs._data != rhs._data;
    }

    template<typename T>
    FORCE_INLINE T& DescriptorSetBindingData::As() noexcept {
        if (_resource.index() == 0) {
            return _resource.emplace<T>();
        }
        return std::get<T>(_resource);
    }

    template<typename T>
    FORCE_INLINE bool DescriptorSetBindingData::Has() const noexcept {
        return std::holds_alternative<T>(_resource);
    }

    template<typename T>
    FORCE_INLINE const T& DescriptorSetBindingData::As() const noexcept {
        return std::get<T>(_resource);
    }
} //namespace Divide
#endif //_DESCRIPTOR_SETS_INL_
