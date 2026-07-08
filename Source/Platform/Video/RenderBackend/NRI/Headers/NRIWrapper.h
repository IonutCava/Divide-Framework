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
#ifndef DVD_NRI_WRAPPER_H_
#define DVD_NRI_WRAPPER_H_

#include "NRIPlaceholderObjects.h"
#include "nriResources.h"

#include "Platform/Video/Headers/RenderAPIWrapper.h"

namespace Divide {

class NVIDIA_RENDER_INTERFACE_API final : public RenderAPIWrapper
{
public:
    NVIDIA_RENDER_INTERFACE_API( GFXDevice& context, RenderAPI API ) noexcept;

protected:
    void idle( bool fast ) noexcept override;

    [[nodiscard]] bool drawToWindow( DisplayWindow& window ) override;
                  void onRenderThreadLoopStart() override;
                  void onRenderThreadLoopEnd() override;
                  void prepareFlushWindow( DisplayWindow& window ) override;
                  void flushWindow( DisplayWindow& window ) override;
    [[nodiscard]] bool frameStarted() override;
    [[nodiscard]] bool frameEnded() override;

    ErrorCode initRenderingAPI( I32 argc, char** argv, Configuration& config ) noexcept override;
    void closeRenderingAPI() noexcept override;
    void preFlushCommandBuffer( Handle<GFX::CommandBuffer> commandBuffer ) override;
    void flushCommand( GFX::CommandBase* cmd ) noexcept override;
    void postFlushCommandBuffer( Handle<GFX::CommandBuffer> commandBuffer ) noexcept override;
    bool setViewportInternal( const Rect<I32>& newViewport ) noexcept override;
    bool setScissorInternal( const Rect<I32>& newScissor ) noexcept override;
    void onThreadCreated( size_t threadIndex, const std::thread::id& threadID, bool isMainRenderThread ) noexcept override;
    void initDescriptorSets() override;

    [[nodiscard]] bool bindShaderResources( const DescriptorSetEntries& descriptorSetEntries ) override;

    [[nodiscard]] RenderTarget_uptr  newRenderTarget( const RenderTargetDescriptor& descriptor ) const override;
    [[nodiscard]] GPUBuffer_uptr     newGPUBuffer( U32 ringBufferLength, std::string_view name ) const override;
    [[nodiscard]] ShaderBuffer_uptr  newShaderBuffer( const ShaderBufferDescriptor& descriptor ) const override;

private:
    // Helpers
    void initStatePerWindow( NRIPerWindowState& state );
    void destroyStatePerWindow( NRIPerWindowState& state ) noexcept;
    void recreateSwapChain( NRIPerWindowState& state );

    [[nodiscard]] NRIFrameResources& currentFrame() noexcept;
    [[nodiscard]] nri::CommandBuffer* currentCommandBuffer() noexcept;

    void advanceFrame() noexcept;

private:
    GFXDevice&     _context;

    // NRI logical device (owned; destroyed via nriDestroyDevice)
    nri::Device*   _device     { nullptr };
    nri::Queue*    _graphicsQueue { nullptr };

    // Cached interface tables retrieved from the device
    NRIInterfaces  _nri{};

    // Per-window state (keyed by window GUID)
    hashMap<I64, NRIPerWindowState> _perWindowState;

    // Buffered frame resources
    std::array<NRIFrameResources, NRI_BUFFERED_FRAME_COUNT> _frameData{};
    U32  _frameIndex { 0u };

    // Descriptor pools / sets (populated in initDescriptorSets)
    std::array<nri::DescriptorPool*, to_base( DescriptorSetUsage::COUNT )> _descriptorPools{};

    // Requested NRI graphics API
    nri::GraphicsAPI _nriAPI { nri::GraphicsAPI::NONE };
};

} // namespace Divide

#endif // DVD_NRI_WRAPPER_H_
