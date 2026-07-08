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
#ifndef DVD_NRI_RESOURCES_H_
#define DVD_NRI_RESOURCES_H_

#include "Platform/Video/Headers/RenderAPIWrapper.h"

#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRISwapChain.h>

// Number of buffered frames (triple-buffering)
#define NRI_BUFFERED_FRAME_COUNT 3u

// Convenience macro for NRI result checking
#define NRI_CHECK( call )                                                          \
    do {                                                                           \
        const nri::Result _nriResult = (call);                                    \
        DIVIDE_GPU_ASSERT( _nriResult == nri::Result::SUCCESS,                    \
                           "NRI call failed: " #call );                           \
    } while ( 0 )

namespace Divide {

// All NRI interfaces retrieved from the device.
// Populated once during initRenderingAPI and reused throughout the lifetime.
struct NRIInterfaces
{
    nri::CoreInterface      core{};
    nri::HelperInterface    helper{};
    nri::SwapChainInterface swapChain{};
};

// State maintained per display window (swapchain + per-frame sync objects).
struct NRIPerWindowState
{
    DisplayWindow*          _window           { nullptr };
    nri::SwapChain*         _swapChain        { nullptr };

    // Semaphores are fences created with NRI_SWAPCHAIN_SEMAPHORE initial value (required by NRI).
    nri::Fence*             _acquireSemaphore { nullptr };
    nri::Fence*             _releaseSemaphore { nullptr };

    uint32_t                _currentTextureIndex { 0u };
    bool                    _skipEndFrame     { false };
};

// Resources that need to be refreshed every buffered frame.
struct NRIFrameResources
{
    nri::CommandAllocator*  _commandAllocator { nullptr };
    nri::CommandBuffer*     _commandBuffer    { nullptr };
    nri::Fence*             _frameFence       { nullptr };
    uint64_t                _fenceValue       { 0u };
};

// Format translation helpers.
// These map Divide's (GFXImageFormat + GFXDataFormat + GFXImagePacking) triplet to the
// closest NRI equivalent.  Not all combinations are supported; unsupported ones return
// nri::Format::UNKNOWN and must be handled by the caller.
nri::Format DivideFormatToNRI( GFXImageFormat baseFormat,
                               GFXDataFormat  dataType,
                               GFXImagePacking packing ) noexcept;

nri::Format DivideDepthFormatToNRI( GFXImageFormat baseFormat, bool stencil ) noexcept;

} // namespace Divide

#endif // DVD_NRI_RESOURCES_H_
