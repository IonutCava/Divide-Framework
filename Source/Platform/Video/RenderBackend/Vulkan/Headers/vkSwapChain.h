/*
   Copyright (c) 2018-2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

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
#ifndef DVD_VK_SWAP_CHAIN_H_
#define DVD_VK_SWAP_CHAIN_H_

#include "vkResources.h"

namespace Divide {
    class DisplayWindow;
    class VK_API;
    class VKDevice;
    class GFXDevice;

    struct Fence
    {
        VkFence _handle{VK_NULL_HANDLE};
        U8 _tag{U8_MAX};
    };

    struct FrameData
    {
        VkSemaphore _presentSemaphore{VK_NULL_HANDLE};
        VkCommandBuffer _commandBuffer{VK_NULL_HANDLE};
        Fence _renderFence;
        // Two timestamps (begin/end) bracketing this slot's frame command buffer, followed by GPU_PASS_TIMESTAMP_CAPACITY pass timestamps.
        VkQueryPool _timestampPool{ VK_NULL_HANDLE };
        U32 _passTimestampCount{ 0u };
        U64 _passTimestampFrame{ 0u };
        // A failed reset or submission can leave the fence unsignaled indefinitely.
        VkResult _frameError{ VK_SUCCESS };
        bool _submissionPending{ false };
        bool _timestampsPending{ false };
    };

    class VKSwapChain final : NonCopyable, NonMovable {
    public:
        VKSwapChain( VK_API& context, const VKDevice& device, const DisplayWindow& window, bool smokeReadback );
        ~VKSwapChain();

        ErrorCode create(bool vSync, bool adaptiveSync, VkSurfaceKHR targetSurface);
        void destroy();

        [[nodiscard]] VkResult waitForFrameSlot( U64 frameNumber ) noexcept;
        [[nodiscard]] VkResult beginFrame();
        [[nodiscard]] VkResult endFrame( const VKSubmitSempahore::Container& semaphores );
        [[nodiscard]] VkFence activeFrameFence() const noexcept;

        [[nodiscard]] static constexpr size_t FrameSlotCount() noexcept
        {
            static_assert( Config::MAX_FRAMES_IN_FLIGHT > 0u );
            return Config::MAX_FRAMES_IN_FLIGHT;
        }

        [[nodiscard]] static constexpr size_t FrameSlotIndex( const U64 frameNumber ) noexcept
        {
            return to_size( frameNumber % FrameSlotCount() );
        }

        template<typename WaitForFence>
        [[nodiscard]] static VkResult WaitForFrameSlot( const std::span<FrameData> frames, const U64 frameNumber,
                                                         const VkDevice device, WaitForFence&& waitForFence ) noexcept
        {
            if ( frames.empty() )
            {
                return VK_SUCCESS;
            }

            for ( const FrameData& frame : frames )
            {
                if ( frame._frameError != VK_SUCCESS )
                {
                    return frame._frameError;
                }
            }

            if ( frames.size() != FrameSlotCount() )
            {
                return VK_ERROR_INITIALIZATION_FAILED;
            }

            FrameData& frame = frames[FrameSlotIndex( frameNumber )];
            if ( device == VK_NULL_HANDLE || frame._renderFence._handle == VK_NULL_HANDLE )
            {
                frame._frameError = VK_ERROR_INITIALIZATION_FAILED;
                return frame._frameError;
            }

            if ( !frame._submissionPending )
            {
                return VK_SUCCESS;
            }

            const VkResult result = waitForFence( device, frame._renderFence._handle );
            if ( result == VK_SUCCESS )
            {
                frame._submissionPending = false;
            }
            else
            {
                frame._frameError = result;
            }
            return result;
        }

        [[nodiscard]] static constexpr bool SupportsSmokeReadback( const VkFormat format ) noexcept
        {
            return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
                   format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB ||
                   format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        }

        [[nodiscard]] static bool ConvertSmokeReadback( VkFormat format, U32 width, U32 height,
                                                        std::span<const Byte> source, std::span<Byte> destination ) noexcept;

        [[nodiscard]] static constexpr VkImageLayout ScreenPassInitialLayout( const bool deferPresent,
                                                                             const bool renderedThisFrame,
                                                                             const bool canLoadPreviousContents ) noexcept
        {
            return deferPresent && renderedThisFrame ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                 : canLoadPreviousContents ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
        }

        [[nodiscard]] static VkImageMemoryBarrier2 SmokePresentBarrier( VkImage image, bool capture ) noexcept;
        [[nodiscard]] bool deferPresentTransition() const noexcept { return _smokeReadback; }

        [[nodiscard]] vkb::Swapchain& getSwapChain() noexcept;
        [[nodiscard]] VkImage         getCurrentImage() const noexcept;
        [[nodiscard]] VkImageView     getCurrentImageView() const noexcept;
        /// True if the swapchain images use an sRGB format (hardware linear -> sRGB encoding on write)
        [[nodiscard]] bool            isSRGB() const noexcept;
        [[nodiscard]] bool            currentImageWasPresented() const noexcept;
        [[nodiscard]] bool            currentImageWasRenderedThisFrame() const noexcept;
        void                          markCurrentImagePresented() noexcept;
        void                          markCurrentImageRenderedThisFrame() noexcept;
        [[nodiscard]] bool            getFrameData(FrameData*& dataOut) const noexcept;

        /// Writes pass timestamp `index` into the active frame's command buffer (no-op if timestamps are unsupported).
        void writePassTimestamp( U32 index ) noexcept;
        /// Pass timestamps resolved when the current slot was last waited on (ms relative to the first pass timestamp).
        [[nodiscard]] U32 readPassTimestamps( std::span<F32> outMS, U64& frameNumberOut ) const noexcept;

        [[nodiscard]] static constexpr F32 TimestampDeltaMS( const U64 begin, const U64 end, const U32 validBits, const F32 periodNS ) noexcept
        {
            if ( validBits == 0u )
            {
                return 0.f;
            }
            const U64 mask = validBits >= 64u ? U64_MAX : ( 1ull << validBits ) - 1ull;
            const U64 delta = ( end - begin ) & mask;
            return to_F32( to_D64( delta ) * to_D64( periodNS ) * 1e-6 );
        }

        PROPERTY_R_IW(VkExtent2D, surfaceExtent);
        /// GPU execution time of the most recently completed frame command buffer (0 if timestamps are unsupported)
        PROPERTY_R_IW(F32, lastGPUTimeMS, 0.f);
        
    private:
        void readFrameTimestamps( FrameData& frame ) noexcept;

    private:
        VK_API& _context;
        const VKDevice& _device;
        const DisplayWindow& _window;
        const bool _smokeReadback;

        FrameData* _activeFrame{ nullptr };
        vector<FrameData> _frames;
        vkb::Swapchain _swapChain{};


        std::vector<VkImage> _swapchainImages;
        std::vector<VkImageView> _swapchainImageViews;
        vector<U8> _swapchainImagePresented;
        bool _currentImageRenderedThisFrame{ false };
        vector<VkSemaphore> _renderSemaphores;
        U32 _swapchainImageIndex{ 0u };
        U32 _timestampValidBits{ 0u };
        F32 _timestampPeriodNS{ 0.f };
        U32 _lastPassTimestampCount{ 0u };
        U64 _lastPassTimestampFrame{ 0u };
        std::array<F32, GPU_PASS_TIMESTAMP_CAPACITY> _lastPassTimestampsMS{};
    };

    FWD_DECLARE_MANAGED_CLASS(VKSwapChain);
}; //namespace Divide

#endif //DVD_VK_SWAP_CHAIN_H_
