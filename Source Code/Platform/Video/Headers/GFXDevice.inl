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
    LockGuard<Mutex> w_lock(_dataLock);

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

inline Renderer& GFXDevice::getRenderer() const {
    assert(_renderer != nullptr);
    return *_renderer;
}

inline const RenderStateBlock& GFXDevice::get2DStateBlock() const noexcept {
    return _state2DRendering;
}

inline const RenderStateBlock& GFXDevice::getNoDepthTestBlock() const noexcept
{
    return _defaultStateNoDepthTest;
}

inline GFXRTPool& GFXDevice::renderTargetPool() noexcept {
    return *_rtPool;
}

inline const GFXRTPool& GFXDevice::renderTargetPool() const noexcept {
    return *_rtPool;
}

inline const ShaderProgram_ptr& GFXDevice::getRTPreviewShader(const bool depthOnly) const noexcept {
    return depthOnly ? _previewRenderTargetDepth : _previewRenderTargetColour;
}

inline void GFXDevice::registerDrawCall() noexcept {
    registerDrawCalls(1);
}

inline void GFXDevice::registerDrawCalls(const U32 count) noexcept {
    frameDrawCalls(frameDrawCalls() + count);
}

inline const DeviceInformation& GFXDevice::GetDeviceInformation() noexcept {
    return s_deviceInformation;
}

inline void GFXDevice::OverrideDeviceInformation(const DeviceInformation& info) noexcept {
    s_deviceInformation = info;
}

inline bool GFXDevice::IsSubmitCommand(const GFX::CommandType type) noexcept {
    return type == GFX::CommandType::DISPATCH_COMPUTE ||
           type == GFX::CommandType::DRAW_COMMANDS;
}

inline vec2<U16> GFXDevice::renderingResolution() const noexcept {
    return _renderingResolution;
}

inline F32 GFXDevice::renderingAspectRatio() const noexcept {
    return to_F32(_renderingResolution.width) / _renderingResolution.height;
}

inline bool GFXDevice::setViewport(const I32 x, const I32 y, const I32 width, const I32 height) {
    return setViewport({ x, y, width, height });
}

inline bool GFXDevice::setScissor(const I32 x, const I32 y, const I32 width, const I32 height) {
    return setScissor({ x, y, width, height });
}

inline PerformanceMetrics& GFXDevice::getPerformanceMetrics() noexcept {
    return _performanceMetrics;
}

inline const PerformanceMetrics& GFXDevice::getPerformanceMetrics() const noexcept {
    return _performanceMetrics;
}

inline GFXDevice::GFXDescriptorSet& GFXDevice::descriptorSet(const DescriptorSetUsage usage) noexcept {
    return _descriptorSets[to_base(usage)];
}

inline const GFXDevice::GFXDescriptorSet& GFXDevice::descriptorSet(const DescriptorSetUsage usage) const noexcept {
    return _descriptorSets[to_base(usage)];
}

inline void GFXDevice::onShaderRegisterChanged( ShaderProgram* program, const bool state ) { _api->onShaderRegisterChanged( program, state ); }


inline ShaderProgram_ptr GFXDevice::newShaderProgram( const size_t descriptorHash,
                                                      const Str256& resourceName,
                                                      const Str256& assetName,
                                                      const ResourcePath& assetLocation,
                                                      const ShaderProgramDescriptor& descriptor,
                                                      ResourceCache& parentCache )
{
    return _api->newShaderProgram( descriptorHash, resourceName, assetName, assetLocation, descriptor, parentCache );
}

inline ShaderBuffer_uptr GFXDevice::newSB( const ShaderBufferDescriptor& descriptor )
{
    return _api->newSB( descriptor );
}

inline GenericVertexData_ptr GFXDevice::newGVD( const U32 ringBufferLength, const bool renderIndirect, const Str256& name )
{
    return _api->newGVD( ringBufferLength, renderIndirect, name );
}

inline Texture_ptr GFXDevice::newTexture( const size_t descriptorHash,
                                          const Str256& resourceName,
                                          const ResourcePath& assetNames,
                                          const ResourcePath& assetLocations,
                                          const TextureDescriptor& texDescriptor,
                                          ResourceCache& parentCache )
{
    return _api->newTexture( descriptorHash, resourceName, assetNames, assetLocations, texDescriptor, parentCache );
}
};  // namespace Divide

#endif
