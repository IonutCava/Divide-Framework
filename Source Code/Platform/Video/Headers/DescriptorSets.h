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
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"

namespace Divide {
    class Texture;
    FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

    enum class DescriptorSetBindingType : U8 {
        COMBINED_IMAGE_SAMPLER,
        UNIFORM_BUFFER,
        SHADER_STORAGE_BUFFER,
        IMAGE,
        IMAGE_VIEW,
        COUNT
    };

    enum class DescriptorUpdateResult : U8 {
        NEW_ENTRY_ADDED = 0u,
        OVERWRITTEN_EXISTING,
        NO_UPDATE,
        COUNT
    };

    struct ImageView final : Hashable
    {
        TextureData _textureData{};
        TextureType _targetType{ TextureType::COUNT };
        size_t _samplerHash{ 0u };
        vec2<U16> _mipLevels{};
        vec2<U16> _layerRange{};

        size_t getHash() const override;
    };

    struct ImageViewEntry final : Hashable
    {
        ImageView _view{};
        TextureDescriptor _descriptor{};

        [[nodiscard]] bool isValid() const noexcept { return IsValid(_view._textureData); }

        void reset() noexcept { _view = {}; _descriptor = {}; }

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

        Texture* _texture{ nullptr };
        Flag _flag{ Flag::READ };
        U8 _layer{ 0u };
        U8 _level{ 0u };
        bool _layered{ false };
    };

    struct DescriptorCombinedImageSampler {
        TextureData _image{};
        size_t _samplerHash{ 0u };
    };

    struct ShaderBufferEntry {
        ShaderBuffer* _buffer{ nullptr };
        BufferRange _range{};
    };

    struct DescriptorSetBindingData {
        std::variant<std::monostate,
                     ShaderBufferEntry,
                     DescriptorCombinedImageSampler,
                     Image,
                     ImageViewEntry> _resource{};

        template<typename T>
        [[nodiscard]] FORCE_INLINE T& As() noexcept { 
            if (_resource.index() == 0) {
                return _resource.emplace<T>();
            }
            return std::get<T>(_resource);
        }

        template<typename T>
        [[nodiscard]] FORCE_INLINE bool Has() const noexcept {
            return std::holds_alternative<T>(_resource);
        }
        
        template<typename T>
        [[nodiscard]] FORCE_INLINE const T& As() const noexcept {
            return std::get<T>(_resource);
        }
    };

    struct DescriptorSetBinding {
        enum class ShaderStageVisibility : U16 {
            NONE = 0,
            VERTEX = toBit(1),
            GEOMETRY = toBit(2),
            TESS_CONTROL = toBit(3),
            TESS_EVAL = toBit(4),
            FRAGMENT = toBit(5),
            COMPUTE = toBit(6),
            MESH = toBit(7),
            TASK = toBit(8),
            ALL_GEOMETRY = MESH | TASK | VERTEX | GEOMETRY | TESS_CONTROL | TESS_EVAL,
            ALL_DRAW = ALL_GEOMETRY | FRAGMENT,
            COMPUTE_AND_DRAW = FRAGMENT | COMPUTE,
            COMPUTE_AND_GEOMETRY = ALL_GEOMETRY | COMPUTE,
            ALL = ALL_DRAW | COMPUTE,
            COUNT = 10
        };

        DescriptorSetBindingData _data{};
        U8 _resourceSlot{ 0u };
        ShaderStageVisibility _shaderStageVisibility{ ShaderStageVisibility::COUNT };
        DescriptorSetBindingType _type{ DescriptorSetBindingType::COUNT };
    };

    struct DescriptorSet {
        DescriptorSetUsage _usage{ DescriptorSetUsage::COUNT };
        eastl::fixed_vector<DescriptorSetBinding, 16, false> _bindings{};
    };

    bool operator==(const ImageView& lhs, const ImageView& rhs) noexcept;
    bool operator!=(const ImageView& lhs, const ImageView& rhs) noexcept;
    bool operator==(const ImageViewEntry& lhs, const ImageViewEntry& rhs) noexcept;
    bool operator!=(const ImageViewEntry& lhs, const ImageViewEntry& rhs) noexcept;
    bool operator==(const Image& lhs, const Image& rhs) noexcept;
    bool operator!=(const Image& lhs, const Image& rhs) noexcept;
    bool operator==(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept;
    bool operator!=(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept;
    bool operator==(const DescriptorCombinedImageSampler& lhs, const DescriptorCombinedImageSampler& rhs) noexcept;
    bool operator!=(const DescriptorCombinedImageSampler& lhs, const DescriptorCombinedImageSampler& rhs) noexcept;
    bool operator==(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept;
    bool operator!=(const DescriptorSetBindingData& lhs, const DescriptorSetBindingData& rhs) noexcept;
    bool operator==(const DescriptorSetBinding& lhs, const DescriptorSetBinding& rhs) noexcept;
    bool operator!=(const DescriptorSetBinding& lhs, const DescriptorSetBinding& rhs) noexcept;
    bool operator==(const DescriptorSet& lhs, const DescriptorSet& rhs) noexcept;
    bool operator!=(const DescriptorSet& lhs, const DescriptorSet& rhs) noexcept;
    /// Returns true if we don't have any bindings set
    [[nodiscard]] bool IsEmpty(const DescriptorSet& set) noexcept;
    [[nodiscard]] DescriptorUpdateResult UpdateBinding(DescriptorSet& set, const DescriptorSetBinding& binding);
}; //namespace Divide

#endif //_DESCRIPTOR_SETS_H_

#include "DescriptorSets.inl"
