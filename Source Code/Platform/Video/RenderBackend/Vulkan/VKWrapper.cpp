#include "stdafx.h"

#include "Headers/VKWrapper.h"

namespace Divide {
    VK_API::VK_API([[maybe_unused]] GFXDevice& context)
    {
    }

    void VK_API::idle([[maybe_unused]] const bool fast) {
    }

    void VK_API::beginFrame([[maybe_unused]] DisplayWindow& window, [[maybe_unused]] bool global) {
    }

    void VK_API::endFrame([[maybe_unused]] DisplayWindow& window, [[maybe_unused]] bool global) {
    }

    ErrorCode VK_API::initRenderingAPI([[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, [[maybe_unused]] Configuration& config) {
        return ErrorCode::NO_ERR;
    }

    void VK_API::closeRenderingAPI() {
    }

    PerformanceMetrics VK_API::getPerformanceMetrics() const noexcept {
        return {};
    }

    void VK_API::flushCommand([[maybe_unused]] const GFX::CommandBuffer::CommandEntry& entry, [[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    void VK_API::preFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    void VK_API::postFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    vec2<U16> VK_API::getDrawableSize([[maybe_unused]] const DisplayWindow& window) const {
        return vec2<U16>(1);
    }

    U32 VK_API::getHandleFromCEGUITexture([[maybe_unused]] const CEGUI::Texture& textureIn) const {
        return 0u;
    }

    bool VK_API::setViewport([[maybe_unused]] const Rect<I32>& newViewport) {
        return true;
    }

    void VK_API::onThreadCreated([[maybe_unused]] const std::thread::id& threadID) {
    }

}; //namespace Divide