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

#ifndef _HARDWARE_VIDEO_GFX_DEVICE_INL_H_
#define _HARDWARE_VIDEO_GFX_DEVICE_INL_H_

namespace Divide {

template <typename Data, size_t N>
void DebugPrimitiveHandler<Data, N>::reset() {
    ScopedLock<Mutex> w_lock(_dataLock);

    for (IMPrimitive*& primitive : _debugPrimitives) {
        if (primitive != nullptr) {
            primitive->context().destroyIMP(primitive);
        }
        primitive = nullptr;
    }
    for (auto& data : _debugData) {
        data = {};
    }
}

inline Renderer& 
GFXDevice::getRenderer() const {
    assert(_renderer != nullptr);
    return *_renderer;
}

inline const GPUState&
GFXDevice::gpuState() const noexcept {
    return _state;
}

inline GPUState&
GFXDevice::gpuState() noexcept {
    return _state;
}

inline size_t
GFXDevice::get2DStateBlock() const noexcept {
    return _state2DRenderingHash;
}

inline GFXRTPool&
GFXDevice::renderTargetPool() noexcept {
    return *_rtPool;
}

inline const GFXRTPool&
GFXDevice::renderTargetPool() const noexcept {
    return *_rtPool;
}

inline const ShaderProgram_ptr&
GFXDevice::getRTPreviewShader(const bool depthOnly) const noexcept {
    return depthOnly ? _previewRenderTargetDepth : _previewRenderTargetColour;
}

inline Arena::Statistics
GFXDevice::getObjectAllocStats() const noexcept {
    return _gpuObjectArena.statistics_;
}

inline void
GFXDevice::registerDrawCall() noexcept {
    registerDrawCalls(1);
}

inline void
GFXDevice::registerDrawCalls(const U32 count) noexcept {
    frameDrawCalls(frameDrawCalls() + count);
}

inline const Rect<I32>&
GFXDevice::getCurrentViewport() const noexcept {
    return _viewport;
}

inline const PerformanceMetrics&
GFXDevice::getPerformanceMetrics() const noexcept {
    return _api->getPerformanceMetrics();
}

inline const DeviceInformation&
GFXDevice::GetDeviceInformation() noexcept {
    return s_deviceInformation;
}

inline void
GFXDevice::OverrideDeviceInformation(const DeviceInformation& info) noexcept {
    s_deviceInformation = info;
}

inline vec2<U16>
GFXDevice::getDrawableSize(const DisplayWindow& window) const {
    return _api->getDrawableSize(window);
}

inline U32
GFXDevice::getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const {
    return _api->getHandleFromCEGUITexture(textureIn);
}

inline void
GFXDevice::onThreadCreated(const std::thread::id& threadID) const {
    _api->onThreadCreated(threadID);
    if (!ShaderProgram::OnThreadCreated(*this, threadID)) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

inline const vec2<U16>& 
GFXDevice::renderingResolution() const noexcept {
    return _renderingResolution;
}

inline F32
GFXDevice::renderingAspectRatio() const noexcept {
    return to_F32(_renderingResolution.width) / _renderingResolution.height;
}

inline bool
GFXDevice::setViewport(const I32 x, const I32 y, const I32 width, const I32 height) {
    return setViewport({ x, y, width, height });
}

inline const Rect<I32>&
GFXDevice::getViewport() const noexcept {
    return _viewport;
}

};  // namespace Divide

#endif
