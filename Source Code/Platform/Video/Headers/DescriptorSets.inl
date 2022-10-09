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

    inline bool operator==(const TextureWrapper& lhs, const TextureWrapper& rhs) noexcept {
        return lhs._internalTexture == rhs._internalTexture &&
               lhs._ceguiTex == rhs._ceguiTex;
    }

    inline bool operator!=(const TextureWrapper& lhs, const TextureWrapper& rhs) noexcept {
        return lhs._internalTexture != rhs._internalTexture ||
               lhs._ceguiTex != rhs._ceguiTex;
    }

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
               lhs._srcTexture == rhs._srcTexture &&
               lhs._usage == rhs._usage;
    }

    inline bool operator!=(const ImageView& lhs, const ImageView&rhs) noexcept {
        return lhs._mipLevels != rhs._mipLevels ||
               lhs._layerRange != rhs._layerRange ||
               lhs.targetType() != rhs.targetType() ||
               lhs._srcTexture != rhs._srcTexture ||
               lhs._descriptor != rhs._descriptor ||
               lhs._usage != rhs._usage;
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

    inline bool IsSet( const DescriptorSetBindingData& data ) noexcept {
        return data.index() != 0;
    }

    template<typename T>
    inline T& As( DescriptorSetBindingData& data ) noexcept {
        if ( data.index() == 0) {
            return data.emplace<T>();
        }
        return eastl::get<T>( data );
    }

    template<typename T>
    inline bool Has( const DescriptorSetBindingData& data ) noexcept {
        return eastl::holds_alternative<T>( data );
    }

    template<typename T>
    inline const T& As( const DescriptorSetBindingData& data ) noexcept {
        return eastl::get<T>( data );
    }

    inline void Set( DescriptorSetBindingData& dataInOut, const ImageView& view )
    {
        As<ImageView>(dataInOut) = view;
    } 
    
    inline void Set( DescriptorSetBindingData& dataInOut, const DescriptorCombinedImageSampler& combinedImageSampler )
    {
        As<DescriptorCombinedImageSampler>(dataInOut) = combinedImageSampler;
    }

    inline void Set( DescriptorSetBindingData& dataInOut, const ImageView& imageView, size_t samplerHash )
    {
        Set(dataInOut, DescriptorCombinedImageSampler{imageView, samplerHash});
    }

    inline DescriptorSetBinding& AddBinding( DescriptorSet& setInOut, const U8 slot, const U16 stageVisibilityMask )
    {
        DescriptorSetBinding& newBinding = setInOut.emplace_back();
        newBinding._shaderStageVisibility = stageVisibilityMask;
        newBinding._slot = slot;
        return newBinding;
    }

    inline  DescriptorSetBinding& AddBinding( DescriptorSet& setInOut, const U8 slot, const ShaderStageVisibility stageVisibility )
    {
        return AddBinding(setInOut, slot, to_U16(stageVisibility));
    }
} //namespace Divide
#endif //_DESCRIPTOR_SETS_INL_
