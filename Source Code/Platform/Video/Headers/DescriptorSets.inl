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

namespace Divide
{
    inline bool operator==( const ImageViewDescriptor& lhs, const ImageViewDescriptor& rhs ) noexcept
    {
        return lhs._msaaSamples == rhs._msaaSamples &&
               lhs._dataType == rhs._dataType &&
               lhs._baseFormat == rhs._baseFormat &&
               lhs._packing == rhs._packing;
    }

    inline bool operator!=( const ImageViewDescriptor& lhs, const ImageViewDescriptor& rhs ) noexcept
    {
        return lhs._msaaSamples != rhs._msaaSamples ||
               lhs._dataType != rhs._dataType ||
               lhs._baseFormat != rhs._baseFormat ||
               lhs._packing != rhs._packing;
    }

    inline bool operator==( const ImageView& lhs, const ImageView& rhs ) noexcept
    {
        return lhs._subRange == rhs._subRange &&
               lhs._descriptor == rhs._descriptor &&
               TargetType(lhs) == TargetType(rhs) &&
               lhs._srcTexture == rhs._srcTexture;
    }

    inline bool operator!=( const ImageView& lhs, const ImageView& rhs ) noexcept
    {
        return lhs._srcTexture != rhs._srcTexture ||
               lhs._subRange != rhs._subRange ||
               TargetType(lhs) != TargetType(rhs) ||
               lhs._descriptor != rhs._descriptor;
    }

    inline bool operator==( const SubRange& lhs, const SubRange& rhs ) noexcept
    {
        return lhs._offset == rhs._offset &&
               lhs._count == rhs._count;
    }

    inline bool operator!=( const SubRange& lhs, const SubRange& rhs ) noexcept
    {
        return lhs._offset != rhs._offset ||
               lhs._count != rhs._count;
    }

    inline bool operator==( const ImageSubRange& lhs, const ImageSubRange& rhs ) noexcept
    {
        return lhs._mipLevels == rhs._mipLevels &&
               lhs._layerRange == rhs._layerRange;
    }

    inline bool operator!=( const ImageSubRange& lhs, const ImageSubRange& rhs ) noexcept
    {
        return lhs._mipLevels != rhs._mipLevels ||
               lhs._layerRange != rhs._layerRange;
    }

    inline bool operator==( const DescriptorCombinedImageSampler& lhs, const DescriptorCombinedImageSampler& rhs ) noexcept
    {
        return lhs._image == rhs._image &&
               lhs._samplerHash == rhs._samplerHash;
    }

    inline bool operator!=( const DescriptorCombinedImageSampler& lhs, const DescriptorCombinedImageSampler& rhs ) noexcept
    {
        return lhs._image != rhs._image ||
               lhs._samplerHash != rhs._samplerHash;
    }

    inline bool operator==( const DescriptorSetBinding& lhs, const DescriptorSetBinding& rhs ) noexcept
    {
        return lhs._shaderStageVisibility == rhs._shaderStageVisibility &&
               lhs._slot == rhs._slot &&
               lhs._data == rhs._data;
    }

    inline bool operator!=( const DescriptorSetBinding& lhs, const DescriptorSetBinding& rhs ) noexcept
    {
        return lhs._shaderStageVisibility != rhs._shaderStageVisibility ||
               lhs._slot != rhs._slot ||
               lhs._data != rhs._data;
    }
    
    inline bool operator==( const DescriptorImageView& lhs, const DescriptorImageView& rhs ) noexcept
    {
        return lhs._image == rhs._image &&
               lhs._usage == rhs._usage;
    }

    inline bool operator!=( const DescriptorImageView& lhs, const DescriptorImageView& rhs ) noexcept
    {
        return lhs._image != rhs._image ||
               lhs._usage != rhs._usage;
    }

    inline bool operator==( const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs ) noexcept
    {
        if ( lhs._type != rhs._type )
        {
            return false;
        }

        switch ( lhs._type )
        {
            case DescriptorSetBindingType::UNIFORM_BUFFER : 
            case DescriptorSetBindingType::SHADER_STORAGE_BUFFER : return lhs._buffer == rhs._buffer;
            case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: return lhs._sampledImage == rhs._sampledImage;
            case DescriptorSetBindingType::IMAGE : return lhs._imageView == rhs._imageView;
            default: break;
        }

        return true;
    }

    inline bool operator!=( const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept
    {
        if ( lhs._type == rhs._type )
        {
            switch ( lhs._type )
            {
                case DescriptorSetBindingType::UNIFORM_BUFFER:
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER: return lhs._buffer != rhs._buffer;
                case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: return lhs._sampledImage != rhs._sampledImage;
                case DescriptorSetBindingType::IMAGE: return lhs._imageView != rhs._imageView;
                default: return false;
            }
        }

        return true;
    }

    inline bool operator==( const DescriptorSet& lhs, const DescriptorSet& rhs ) noexcept
    {
        return lhs._bindingCount == rhs._bindingCount &&
               lhs._bindings == rhs._bindings;
    }

    inline bool operator!=( const DescriptorSet& lhs, const DescriptorSet& rhs ) noexcept
    {
        return lhs._bindingCount != rhs._bindingCount ||
               lhs._bindings != rhs._bindings;
    }

    inline void Set( DescriptorSetBindingData& dataInOut, const DescriptorImageView& view ) noexcept
    {
        dataInOut._imageView = view;
        dataInOut._type = DescriptorSetBindingType::IMAGE;
    }

    inline void Set( DescriptorSetBindingData& dataInOut, const DescriptorCombinedImageSampler& combinedImageSampler ) noexcept
    {
        dataInOut._sampledImage = combinedImageSampler;
        dataInOut._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
    }

    inline void Set( DescriptorSetBindingData& dataInOut, const ImageView& view, const ImageUsage usage ) noexcept
    {
        Set(dataInOut, DescriptorImageView{view, usage});
    }

    inline void Set( DescriptorSetBindingData& dataInOut, const ImageView& view, const size_t samplerHash ) noexcept
    {
        Set(dataInOut, DescriptorCombinedImageSampler{view, samplerHash});
    }

    inline DescriptorSetBinding& AddBinding( DescriptorSet& setInOut, const U8 slot, const U16 stageVisibilityMask )
    {
        return setInOut._bindings[setInOut._bindingCount++] = DescriptorSetBinding
        {
            ._shaderStageVisibility = stageVisibilityMask,
            ._slot = slot
        };
    }

    inline  DescriptorSetBinding& AddBinding( DescriptorSet& setInOut, const U8 slot, const ShaderStageVisibility stageVisibility )
    {
        return AddBinding( setInOut, slot, to_U16( stageVisibility ) );
    }
} //namespace Divide
#endif //_DESCRIPTOR_SETS_INL_
