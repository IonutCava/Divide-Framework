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
#ifndef DVD_DESCRIPTOR_SETS_H_
#define DVD_DESCRIPTOR_SETS_H_

#include "DescriptorSetsFwd.h"
#include "Platform/Video/Buffers/Headers/BufferRange.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

namespace Divide
{
    // SubRange instead of vec2<U16> to keep things trivial
    struct SubRange
    {
        U16 _offset{0u};
        U16 _count{U16_MAX};

        bool operator==( const SubRange& ) const = default;
    };

    struct ImageSubRange
    {
        SubRange _mipLevels{};
        SubRange _layerRange{};

        bool operator==( const ImageSubRange& ) const = default;
    };

    struct ImageViewDescriptor
    {
        U8 _msaaSamples{ 0u };
        GFXDataFormat _dataType{ GFXDataFormat::COUNT };
        GFXImageFormat _baseFormat{ GFXImageFormat::COUNT };
        GFXImagePacking _packing{ GFXImagePacking::COUNT };

        bool operator==( const ImageViewDescriptor& ) const = default;
    };

    struct ImageView
    {
        static constexpr size_t INVALID_VIEW_HASH = std::numeric_limits<size_t>::max();

        ImageViewDescriptor _descriptor{};
        const Texture* _srcTexture{ nullptr };
        ImageSubRange _subRange{};

        TextureType _targetType{ TextureType::COUNT };

        bool operator==( const ImageView& other ) const noexcept;
        bool operator!=( const ImageView& other ) const noexcept;
    };

    [[nodiscard]] TextureType TargetType(const ImageView& imageView) noexcept;

    struct DescriptorCombinedImageSampler
    {
        ImageView _image{};
        SamplerDescriptor _sampler{};

        size_t _samplerHash{SamplerDescriptor::INVALID_SAMPLER_HASH};
        size_t _imageHash{ImageView::INVALID_VIEW_HASH};

        bool operator==(const DescriptorCombinedImageSampler&) const noexcept = default;
    };

    struct DescriptorImageView
    {
        ImageView _image{};
        ImageUsage _usage{ImageUsage::COUNT};

        bool operator==( const DescriptorImageView& ) const noexcept = default;
    };

    struct ShaderBufferEntry
    {
        ShaderBuffer* _buffer{ nullptr };
        BufferRange _range{};
        I32 _queueReadIndex{0u};
    };

    struct DescriptorSetBindingData
    {
        union
        {
            ShaderBufferEntry _buffer;
            DescriptorCombinedImageSampler _sampledImage;
            DescriptorImageView _imageView;
        };

        DescriptorSetBindingType _type{ DescriptorSetBindingType::COUNT };

        bool operator==( const DescriptorSetBindingData& rhs ) const noexcept;
        bool operator!=( const DescriptorSetBindingData& rhs ) const noexcept;
    };

    struct DescriptorSetBinding
    {
        DescriptorSetBindingData _data{};
        U16 _shaderStageVisibility{ to_base( ShaderStageVisibility::COUNT ) };
        U8 _slot{ 0u };

        bool operator==( const DescriptorSetBinding& ) const = default;
    };

    struct DescriptorSet
    {
        std::array<DescriptorSetBinding, MAX_BINDINGS_PER_DESCRIPTOR_SET> _bindings;
        U8 _bindingCount{ 0u };

        bool operator==( const DescriptorSet& ) const = default;
    };

    void Set( DescriptorSetBindingData& dataInOut, ShaderBuffer* buffer, BufferRange range ) noexcept;
    void Set( DescriptorSetBindingData& dataInOut, const DescriptorImageView& view ) noexcept;
    void Set( DescriptorSetBindingData& dataInOut, const ImageView& view, ImageUsage usage) noexcept;
    void Set( DescriptorSetBindingData& dataInOut, const ImageView& view, SamplerDescriptor sampler ) noexcept;

    [[nodiscard]] DescriptorSetBinding& AddBinding(DescriptorSet& setInOut, U8 slot, U16 stageVisibilityMask); 
    [[nodiscard]] DescriptorSetBinding& AddBinding(DescriptorSet& setInOut, U8 slot, ShaderStageVisibility stageVisibility);

    bool operator==( const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs ) noexcept;
    bool operator!=( const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs ) noexcept;

    [[nodiscard]] bool IsSet( const DescriptorSetBindingData& data ) noexcept;

    template<typename T>
    [[nodiscard]] T& As( DescriptorSetBindingData& data ) noexcept;
    template<typename T>
    [[nodiscard]] const T& As( const DescriptorSetBindingData& data ) noexcept;

    [[nodiscard]] size_t GetHash( const ImageViewDescriptor& descriptor ) noexcept;
    [[nodiscard]] size_t GetHash( const ImageView& imageView ) noexcept;

}; //namespace Divide

#endif //DVD_DESCRIPTOR_SETS_H_

#include "DescriptorSets.inl"
