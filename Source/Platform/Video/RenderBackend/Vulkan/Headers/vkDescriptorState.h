// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "vkResources.h"

namespace Divide
{
    struct VKDescriptorSlotState
    {
        VkDescriptorBufferInfo _bufferInfo{};
        VkDescriptorImageInfo _imageInfo{};
        VkDescriptorType _type{ VK_DESCRIPTOR_TYPE_MAX_ENUM };

        [[nodiscard]] bool isImage() const noexcept
        {
            return _type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || _type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }

        [[nodiscard]] bool operator==( const VKDescriptorSlotState& other ) const noexcept
        {
            if ( _type != other._type )
            {
                return false;
            }
            if ( _type == VK_DESCRIPTOR_TYPE_MAX_ENUM )
            {
                return true;
            }
            if ( isImage() )
            {
                return _imageInfo.sampler == other._imageInfo.sampler &&
                       _imageInfo.imageView == other._imageInfo.imageView &&
                       _imageInfo.imageLayout == other._imageInfo.imageLayout;
            }

            return _bufferInfo.buffer == other._bufferInfo.buffer &&
                   _bufferInfo.offset == other._bufferInfo.offset &&
                   _bufferInfo.range == other._bufferInfo.range;
        }
    };

    using VKDescriptorSlotStates = std::array<VKDescriptorSlotState, MAX_BINDINGS_PER_DESCRIPTOR_SET>;

    class VKPushDescriptorState
    {
    public:
        [[nodiscard]] bool needsUpdate( const VkPipelineLayout layout, const U64 version ) const noexcept
        {
            return _layout != layout || _version != version;
        }

        void beginUpdate( const VkPipelineLayout layout ) noexcept
        {
            if ( _layout != layout )
            {
                _slots = {};
                _layout = layout;
            }
        }

        [[nodiscard]] bool updateSlot( const U8 slot, const VKDescriptorSlotState& resolved ) noexcept
        {
            VKDescriptorSlotState& previous = _slots[slot];
            if ( previous == resolved )
            {
                return false;
            }
            previous = resolved;
            return true;
        }

        void finishUpdate( const U64 version ) noexcept
        {
            _version = version;
        }

    private:
        VKDescriptorSlotStates _slots{};
        VkPipelineLayout _layout{ VK_NULL_HANDLE };
        U64 _version{ U64_MAX };
    };
}
