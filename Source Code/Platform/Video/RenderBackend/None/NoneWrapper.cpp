#include "stdafx.h"

#include "Headers/NoneWrapper.h"

namespace Divide {
    NONE_API::NONE_API([[maybe_unused]] GFXDevice& context)
    {
    }

    void NONE_API::idle([[maybe_unused]] const bool fast) {
    }

    void NONE_API::beginFrame([[maybe_unused]] DisplayWindow& window, [[maybe_unused]] bool global) {
    }

    void NONE_API::endFrame([[maybe_unused]] DisplayWindow& window, [[maybe_unused]] bool global) {
    }

    ErrorCode NONE_API::initRenderingAPI([[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, [[maybe_unused]] Configuration& config) {
        return ErrorCode::NO_ERR;
    }

    void NONE_API::closeRenderingAPI() {
    }

    PerformanceMetrics NONE_API::getPerformanceMetrics() const noexcept {
        return {};
    }

    void NONE_API::flushCommand([[maybe_unused]] const GFX::CommandBuffer::CommandEntry& entry, [[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    void NONE_API::preFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    void NONE_API::postFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    vec2<U16> NONE_API::getDrawableSize([[maybe_unused]] const DisplayWindow& window) const {
        return vec2<U16>(1);
    }

    U32 NONE_API::getHandleFromCEGUITexture([[maybe_unused]] const CEGUI::Texture& textureIn) const {
        return 0u;
    }

    bool NONE_API::setViewport([[maybe_unused]] const Rect<I32>& newViewport) {
        return true;
    }

    void NONE_API::onThreadCreated([[maybe_unused]] const std::thread::id& threadID) {
    }

}; //namespace Divide