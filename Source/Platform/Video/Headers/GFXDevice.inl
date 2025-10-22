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

#ifndef DVD_HARDWARE_VIDEO_GFX_DEVICE_INL_
#define DVD_HARDWARE_VIDEO_GFX_DEVICE_INL_

namespace Divide {

void DestroyIMP(IMPrimitive*& primitive);

template <typename Descriptor, size_t N>
DebugPrimitiveHandler<Descriptor, N>::DebugPrimitiveHandler()   noexcept
{
    for ( auto& primitive : _debugPrimitives )
    {
        primitive = nullptr;
    }
}

template <typename Descriptor, size_t N>
DebugPrimitiveHandler<Descriptor, N>::~DebugPrimitiveHandler()
{
    reset();
}

template <typename Descriptor, size_t N>
size_t DebugPrimitiveHandler<Descriptor, N>::size() const noexcept
{
    return _debugPrimitives.size();
}

template <typename Descriptor, size_t N>
void DebugPrimitiveHandler<Descriptor, N>::reset()
{
    LockGuard<Mutex> w_lock(_dataLock);

    for (IMPrimitive*& primitive : _debugPrimitives)
    {
        DestroyIMP(primitive);
    }

    for (auto& data : _debugData)
    {
        data = {};
    }
}

template <typename Descriptor, size_t N>
void DebugPrimitiveHandler<Descriptor, N>::add( const I64 ID, const Descriptor& data ) noexcept
{
    LockGuard<Mutex> w_lock( _dataLock );
    addLocked( ID, data );
}

template <typename Descriptor, size_t N>
void DebugPrimitiveHandler<Descriptor, N>::addLocked( const I64 ID, const Descriptor& data ) noexcept
{
    const size_t count = _debugData.size();

    for ( U32 i = 0u; i < count; ++i )
    {
        DataEntry& entry = _debugData[i];
        if ( entry._id == ID )
        {
            entry._descriptor = data;
            entry._frameLifeTime = g_maxFrameLifetime;
            return;
        }
    }

    for ( U32 i = 0u; i < count; ++i )
    {
        DataEntry& entry = _debugData[i];
        if ( entry._frameLifeTime == 0u )
        {
            entry._id = ID;
            entry._descriptor = data;
            entry._frameLifeTime = g_maxFrameLifetime;
            return;
        }
    }

    //We need a new entry. Create one and try again
    _debugPrimitives.emplace_back( nullptr );
    _debugData.emplace_back();
    addLocked( ID, data );
}

inline Renderer& GFXDevice::getRenderer() const
{
    assert(_renderer != nullptr);
    return *_renderer;
}

inline const RenderStateBlock& GFXDevice::get2DStateBlock() const noexcept
{
    return _state2DRendering;
}

inline const RenderStateBlock& GFXDevice::getNoDepthTestBlock() const noexcept
{
    return _defaultStateNoDepthTest;
}

inline GFXRTPool& GFXDevice::renderTargetPool() noexcept
{
    return *_rtPool;
}

inline const GFXRTPool& GFXDevice::renderTargetPool() const noexcept
{
    return *_rtPool;
}

inline Handle<ShaderProgram> GFXDevice::getRTPreviewShader(const bool depthOnly) const noexcept
{
    return depthOnly ? _previewRenderTargetDepth : _previewRenderTargetColour;
}

inline void GFXDevice::registerDrawCall() noexcept
{
    registerDrawCalls(1);
}

inline void GFXDevice::registerDrawCalls(const U32 count) noexcept
{
    frameDrawCalls(frameDrawCalls() + count);
}

inline const DeviceInformation& GFXDevice::GetDeviceInformation() noexcept
{
    return s_deviceInformation;
}

inline void GFXDevice::OverrideDeviceInformation(const DeviceInformation& info) noexcept
{
    s_deviceInformation = info;
}

inline bool GFXDevice::IsSubmitCommand(const GFX::CommandType type) noexcept
{
    return type == GFX::CommandType::DISPATCH_SHADER_TASK ||
           type == GFX::CommandType::DRAW_COMMANDS;
}

inline vec2<U16> GFXDevice::renderingResolution() const noexcept
{
    return _renderingResolution;
}

inline F32 GFXDevice::renderingAspectRatio() const noexcept
{
    return to_F32(_renderingResolution.width) / _renderingResolution.height;
}

inline bool GFXDevice::setViewport(const I32 x, const I32 y, const I32 width, const I32 height)
{
    return setViewport({ x, y, width, height });
}

inline bool GFXDevice::setScissor(const I32 x, const I32 y, const I32 width, const I32 height)
{
    return setScissor({ x, y, width, height });
}

inline PerformanceMetrics& GFXDevice::getPerformanceMetrics() noexcept
{
    return _performanceMetrics;
}

inline const PerformanceMetrics& GFXDevice::getPerformanceMetrics() const noexcept
{
    return _performanceMetrics;
}

inline GFXDevice::GFXDescriptorSet& GFXDevice::descriptorSet(const DescriptorSetUsage usage) noexcept
{
    return _descriptorSets[to_base(usage)];
}

inline const GFXDevice::GFXDescriptorSet& GFXDevice::descriptorSet(const DescriptorSetUsage usage) const noexcept
{
    return _descriptorSets[to_base(usage)];
}

inline ShaderBuffer_uptr GFXDevice::newShaderBuffer( const ShaderBufferDescriptor& descriptor )
{
    return _api->newShaderBuffer( descriptor );
}

inline GPUBuffer_ptr GFXDevice::newGPUBuffer( const U32 ringBufferLength, const std::string_view name )
{
    return _api->newGPUBuffer( ringBufferLength, name );
}

inline GFXDevice::GFXBuffers::PerFrameBuffers& GFXDevice::GFXBuffers::crtBuffers() noexcept
{
    return _perFrameBuffers[_perFrameBufferIndex];
}

inline const GFXDevice::GFXBuffers::PerFrameBuffers& GFXDevice::GFXBuffers::crtBuffers() const noexcept
{
    return _perFrameBuffers[_perFrameBufferIndex];
}

inline void GFXDevice::GFXBuffers::reset( const bool camBuffer, const bool cullBuffer ) noexcept
{
    for ( U8 i = 0u; i < PER_FRAME_BUFFER_COUNT; ++i )
    {
        if ( camBuffer )
        {
            _perFrameBuffers[i]._camDataBuffer.reset();
        }

        if ( cullBuffer )
        {
            _perFrameBuffers[i]._cullCounter.reset();
        }
    }
    crtBuffers()._camWritesThisFrame = 0u;
    crtBuffers()._renderWritesThisFrame = 0u;
}

inline void GFXDevice::GFXBuffers::onEndFrame() noexcept
{
    _perFrameBufferIndex = (_perFrameBufferIndex + 1u) % PER_FRAME_BUFFER_COUNT;
    crtBuffers()._camWritesThisFrame = 0u;
    crtBuffers()._renderWritesThisFrame = 0u;
}
};  // namespace Divide

#endif //DVD_HARDWARE_VIDEO_GFX_DEVICE_INL_
