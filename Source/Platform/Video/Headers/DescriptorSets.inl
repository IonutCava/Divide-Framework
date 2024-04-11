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
#ifndef DVD_DESCRIPTOR_SETS_INL_
#define DVD_DESCRIPTOR_SETS_INL_

namespace Divide
{
    inline bool ImageView::operator==( const ImageView& other ) const noexcept
    {
        return _subRange         == other._subRange &&
               _descriptor       == other._descriptor &&
               TargetType(*this) == TargetType(other) &&
               _srcTexture       == other._srcTexture;
    }

    inline bool ImageView::operator!=( const ImageView& other ) const noexcept
    {
        return _srcTexture       != other._srcTexture ||
               _subRange         != other._subRange ||
               TargetType(*this) != TargetType(other) ||
               _descriptor       != other._descriptor;
    }


    inline bool DescriptorSetBindingData::operator==( const DescriptorSetBindingData& other ) const noexcept
    {
        if ( _type != other._type )
        {
            return false;
        }

        switch ( _type )
        {
            case DescriptorSetBindingType::UNIFORM_BUFFER : 
            case DescriptorSetBindingType::SHADER_STORAGE_BUFFER : return _buffer == other._buffer;
            case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: return _sampledImage == other._sampledImage;
            case DescriptorSetBindingType::IMAGE : return _imageView == other._imageView;

            case DescriptorSetBindingType::COUNT:
            default: break;
        }

        return true;
    }

    inline bool DescriptorSetBindingData::operator!=( const DescriptorSetBindingData& other ) const noexcept
    {
        if ( _type == other._type )
        {
            switch ( _type )
            {
                case DescriptorSetBindingType::UNIFORM_BUFFER:
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER: return _buffer != other._buffer;
                case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: return _sampledImage != other._sampledImage;
                case DescriptorSetBindingType::IMAGE: return _imageView != other._imageView;

                case DescriptorSetBindingType::COUNT:
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

    inline void Set( DescriptorSetBindingData& dataInOut, const ImageView& view, const ImageUsage usage ) noexcept
    {
        Set(dataInOut, DescriptorImageView{view, usage});
    }

    inline void Set( DescriptorSetBindingData& dataInOut, const ImageView& view, const SamplerDescriptor sampler ) noexcept
    {
        dataInOut._sampledImage._image = view;
        dataInOut._sampledImage._sampler = sampler;
        dataInOut._sampledImage._samplerHash = GetHash( sampler );

        dataInOut._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
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

#endif //DVD_DESCRIPTOR_SETS_INL_
