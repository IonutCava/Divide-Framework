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
    inline  bool ImageView::isDefaultView() const noexcept {
        return _isDefaultView || (_mipLevels.max == 0u && _layerRange.max == 0u);
    }

    inline bool operator==(const ImageView::Descriptor& lhs, const ImageView::Descriptor& rhs) noexcept {
        return lhs._msaaSamples == rhs._msaaSamples &&
               lhs._dataType == rhs._dataType &&
               lhs._baseFormat == rhs._baseFormat &&
               lhs._srgb == rhs._srgb &&
               lhs._normalized == rhs._normalized;
    }

    inline bool operator!=(const ImageView::Descriptor& lhs, const ImageView::Descriptor& rhs) noexcept {
        return lhs._msaaSamples != rhs._msaaSamples ||
               lhs._dataType != rhs._dataType ||
               lhs._baseFormat != rhs._baseFormat ||
               lhs._srgb != rhs._srgb ||
               lhs._normalized != rhs._normalized;
    }

    inline bool operator==(const ImageView& lhs, const ImageView&rhs) noexcept {
        return lhs._mipLevels == rhs._mipLevels &&
               lhs._layerRange == rhs._layerRange &&
               lhs.targetType() == rhs.targetType() &&
               lhs._descriptor == rhs._descriptor &&
               lhs._srcTexture == rhs._srcTexture;
    }

    inline bool operator!=(const ImageView& lhs, const ImageView&rhs) noexcept {
        return lhs._mipLevels != rhs._mipLevels ||
               lhs._layerRange != rhs._layerRange ||
               lhs.targetType() != rhs.targetType() ||
               lhs._srcTexture != rhs._srcTexture ||
               lhs._descriptor != rhs._descriptor;
    }

    inline bool operator==(const DescriptorCombinedImageSampler& lhs, const DescriptorCombinedImageSampler& rhs) noexcept {
        return lhs._image == rhs._image &&
               lhs._samplerHash == rhs._samplerHash;
    }

    inline bool operator!=(const DescriptorCombinedImageSampler& lhs, const DescriptorCombinedImageSampler& rhs) noexcept {
        return lhs._image != rhs._image ||
               lhs._samplerHash != rhs._samplerHash;
    }

    inline bool operator==(const DescriptorSetBinding& lhs, const DescriptorSetBinding& rhs) noexcept {
        return lhs._shaderStageVisibility == rhs._shaderStageVisibility &&
               lhs._slot == rhs._slot &&
               lhs._data == rhs._data;
    }

    inline bool operator!=(const DescriptorSetBinding& lhs, const DescriptorSetBinding& rhs) noexcept {
        return lhs._shaderStageVisibility != rhs._shaderStageVisibility ||
               lhs._slot != rhs._slot ||
               lhs._data != rhs._data;
    }

    inline bool DescriptorSetBindingData::isSet() const noexcept {
        return _resource.index() != 0;
    }

    template<typename T>
    inline T& DescriptorSetBindingData::As() noexcept {
        if (_resource.index() == 0) {
            return _resource.emplace<T>();
        }
        return eastl::get<T>(_resource);
    }

    template<typename T>
    inline bool DescriptorSetBindingData::Has() const noexcept {
        return eastl::holds_alternative<T>(_resource);
    }

    template<typename T>
    inline const T& DescriptorSetBindingData::As() const noexcept {
        return eastl::get<T>(_resource);
    }
} //namespace Divide
#endif //_DESCRIPTOR_SETS_INL_
