#include "stdafx.h"

#include "Headers/VKWrapper.h"

namespace Divide {
    VK_API::VK_API([[maybe_unused]] GFXDevice& context) noexcept
    {
    }

    void VK_API::idle([[maybe_unused]] const bool fast) noexcept {
    }

    void VK_API::beginFrame([[maybe_unused]] DisplayWindow& window, [[maybe_unused]] bool global) noexcept {
    }

    void VK_API::endFrame([[maybe_unused]] DisplayWindow& window, [[maybe_unused]] bool global) noexcept {
    }

    ErrorCode VK_API::initRenderingAPI([[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, [[maybe_unused]] Configuration& config) noexcept {
        return ErrorCode::NO_ERR;
    }

    void VK_API::closeRenderingAPI() noexcept {
    }

    const PerformanceMetrics& VK_API::getPerformanceMetrics() const noexcept {
        static PerformanceMetrics perf;
        return perf;
    }

    void VK_API::flushCommand([[maybe_unused]] const GFX::CommandBuffer::CommandEntry& entry, [[maybe_unused]] const GFX::CommandBuffer& commandBuffer) noexcept {
    }

    void VK_API::preFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    void VK_API::postFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) noexcept {
    }

    vec2<U16> VK_API::getDrawableSize([[maybe_unused]] const DisplayWindow& window) const noexcept {
        return vec2<U16>(1);
    }

    U32 VK_API::getHandleFromCEGUITexture([[maybe_unused]] const CEGUI::Texture& textureIn) const noexcept {
        return 0u;
    }

    bool VK_API::setViewport([[maybe_unused]] const Rect<I32>& newViewport) noexcept {
        return true;
    }

    void VK_API::onThreadCreated([[maybe_unused]] const std::thread::id& threadID) noexcept {
    }

}; //namespace Divide