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
#ifndef _DESCRIPTOR_SETS_H_
#define _DESCRIPTOR_SETS_H_

#include "TextureData.h"
#include "Core/Headers/Hashable.h"
#include "Platform/Video/Textures/Headers/TextureDescriptor.h"

namespace Divide {
    // Can be anything really, but we can just have a series of Bind commands in a row as well

    constexpr U8 MAX_BUFFERS_PER_SET = 4u;
    constexpr U8 MAX_TEXTURE_VIEWS_PER_SET = 3u;
    constexpr U8 MAX_IMAGES_PER_SET = 2u;
    constexpr U8 MAX_TEXTURES_PER_SET = to_base(TextureUsage::COUNT) / 2;

    class ShaderBuffer;
    struct ShaderBufferBinding
    {
        vec2<U32>     _elementRange{};
        ShaderBuffer* _buffer = nullptr;
        ShaderBufferLocation _binding = ShaderBufferLocation::COUNT;
        ShaderBufferLockType _lockType = ShaderBufferLockType::COUNT;

        bool set(const ShaderBufferBinding& other) noexcept;
        bool set(ShaderBufferLocation binding, ShaderBuffer* buffer, const vec2<U32>& elementRange, ShaderBufferLockType lockType) noexcept;
    };

    class Texture;
    struct TextureView final : Hashable
    {
        TextureData _textureData = {};
        TextureType _targetType = TextureType::COUNT;
        size_t _samplerHash = 0u;
        vec2<U16> _mipLevels = {};
        vec2<U16> _layerRange = {};

        size_t getHash() const override;
    };

    struct TextureViewEntry final : Hashable
    {
        TextureView _view = {};
        U8 _binding = 0u;
        TextureDescriptor _descriptor;

        [[nodiscard]] bool isValid() const noexcept { return IsValid(_view._textureData); }

        void reset() noexcept { _view = {}; _binding = 0u; _descriptor = {}; }

        size_t getHash() const override;
    };

    struct Image
    {
        enum class Flag : U8
        {
            READ = 0,
            WRITE,
            READ_WRITE
        };

        Texture* _texture = nullptr;
        Flag _flag = Flag::READ;
        bool _layered = false;
        U8 _layer = 0u;
        U8 _level = 0u;
        U8 _binding = 0u;
    };

    template<typename Item, size_t Count, typename SearchType, bool CanExpand = false>
    struct SetContainerStorage
    {
        template<bool Dynamic>
        using vector_allocator = typename std::conditional<Dynamic, eastl::dvd_allocator, eastl::aligned_allocator>::type;

        eastl::fixed_vector<Item, Count, CanExpand, vector_allocator<CanExpand>> _entries;

        [[nodiscard]] const Item* find(SearchType search) const;

        [[nodiscard]] FORCE_INLINE bool   empty() const noexcept { return _entries.empty(); }
        [[nodiscard]] FORCE_INLINE size_t count() const noexcept { return _entries.size(); }
    };

    template<typename Item, size_t Count, typename SearchType, bool CanExpand = false>
    struct SetContainer : SetContainerStorage<Item, Count, SearchType, CanExpand>
    {
        bool add(const Item& entry);
        bool remove( SearchType search);
    };

    using ShaderBuffers = SetContainer<ShaderBufferBinding, MAX_BUFFERS_PER_SET, ShaderBufferLocation>;
    using TextureViews = SetContainer<TextureViewEntry, MAX_TEXTURE_VIEWS_PER_SET, U8>;
    using Images = SetContainer<Image, MAX_IMAGES_PER_SET, U8>;

    struct TextureDataContainer final : SetContainerStorage<TextureEntry, MAX_TEXTURES_PER_SET, U8, true>
    {
        TextureUpdateState add(const TextureEntry& entry);
        bool remove(U8 binding);

        void sortByBinding();
        PROPERTY_RW(bool, hasBindlessTextures, false);
    };

    struct DescriptorSet {
        //This needs a lot more work!
        TextureDataContainer _textureData = {};
        ShaderBuffers _buffers = {};
        TextureViews _textureViews = {};
        Images _images = {};
    };

    [[nodiscard]] bool IsEmpty(const DescriptorSet& set) noexcept;
    [[nodiscard]] bool Merge(const DescriptorSet &lhs, DescriptorSet &rhs, bool& partial);
    [[nodiscard]] bool BufferCompare(const ShaderBuffer* a, const ShaderBuffer* b) noexcept;

    template<typename Item, size_t Count, typename SearchType>
    bool operator==(const SetContainer<Item, Count, SearchType> &lhs, const SetContainer<Item, Count, SearchType> &rhs) noexcept;
    template<typename Item, size_t Count, typename SearchType>
    bool operator!=(const SetContainer<Item, Count, SearchType> &lhs, const SetContainer<Item, Count, SearchType> &rhs) noexcept;

    bool operator==(const DescriptorSet &lhs, const DescriptorSet &rhs) noexcept;
    bool operator!=(const DescriptorSet &lhs, const DescriptorSet &rhs) noexcept;

    bool operator==(const TextureView& lhs, const TextureView &rhs) noexcept;
    bool operator!=(const TextureView& lhs, const TextureView &rhs) noexcept;

    bool operator==(const TextureViewEntry& lhs, const TextureViewEntry &rhs) noexcept;
    bool operator!=(const TextureViewEntry& lhs, const TextureViewEntry &rhs) noexcept;

    bool operator==(const ShaderBufferBinding& lhs, const ShaderBufferBinding &rhs) noexcept;
    bool operator!=(const ShaderBufferBinding& lhs, const ShaderBufferBinding &rhs) noexcept;

    bool operator==(const Image& lhs, const Image &rhs) noexcept;
    bool operator!=(const Image& lhs, const Image &rhs) noexcept;

    bool operator==(const TextureDataContainer & lhs, const TextureDataContainer & rhs) noexcept;
    bool operator!=(const TextureDataContainer & lhs, const TextureDataContainer & rhs) noexcept;
}; //namespace Divide

#endif //_DESCRIPTOR_SETS_H_

#include "DescriptorSets.inl"
