#include "stdafx.h"

#include "Headers/vkFramebuffer.h"

namespace Divide {
    vkRenderTarget::vkRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
        : RenderTarget(context, descriptor)
    {
    }

    void vkRenderTarget::clear([[maybe_unused]] const RTClearDescriptor& descriptor) noexcept {
    }

    void vkRenderTarget::setDefaultState([[maybe_unused]] const RTDrawDescriptor& drawPolicy) noexcept {
    }

    void vkRenderTarget::readData([[maybe_unused]] const vec4<U16>& rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept {
    }

    void vkRenderTarget::blitFrom([[maybe_unused]] const RTBlitParams& params) noexcept {
    }
}; //namespace Divide