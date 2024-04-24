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
#ifndef DVD_NONE_WRAPPER_H_
#define DVD_NONE_WRAPPER_H_

#include "NonePlaceholderObjects.h"

#include "Platform/Video/Headers/RenderAPIWrapper.h"

namespace Divide {

class NONE_API final : public RenderAPIWrapper {
  public:
    NONE_API(GFXDevice& context) noexcept;

  protected:
      void idle(bool fast) noexcept override;

      [[nodiscard]] bool drawToWindow( DisplayWindow& window ) override;
                    void onRenderThreadLoopStart() override;
                    void onRenderThreadLoopEnd() override;
                    void prepareFlushWindow( DisplayWindow& window ) override;
                    void flushWindow( DisplayWindow& window ) override;
      [[nodiscard]] bool frameStarted() override;
      [[nodiscard]] bool frameEnded() override;

      ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) noexcept override;
      void closeRenderingAPI() noexcept override;
      void preFlushCommandBuffer( Handle<GFX::CommandBuffer> commandBuffer) override;
      void flushCommand( GFX::CommandBase* cmd ) noexcept override;
      void postFlushCommandBuffer( Handle<GFX::CommandBuffer> commandBuffer) noexcept override;
      bool setViewportInternal(const Rect<I32>& newViewport) noexcept override;
      bool setScissorInternal(const Rect<I32>& newScissor) noexcept override;
      void onThreadCreated(const std::thread::id& threadID, bool isMainRenderThread ) noexcept override;
      void initDescriptorSets() override;

      [[nodiscard]] bool bindShaderResources( const DescriptorSetEntries& descriptorSetEntries ) override;

      [[nodiscard]] RenderTarget_uptr     newRT( const RenderTargetDescriptor& descriptor ) const override;
      [[nodiscard]] GenericVertexData_ptr newGVD( U32 ringBufferLength, bool renderIndirect, std::string_view name ) const override;
      [[nodiscard]] Texture_ptr           newTexture( size_t descriptorHash, std::string_view resourceName, std::string_view assetNames, const ResourcePath& assetLocations, const TextureDescriptor& texDescriptor, ResourceCache& parentCache ) const override;
      [[nodiscard]] ShaderProgram_ptr     newShaderProgram( size_t descriptorHash, std::string_view resourceName, std::string_view assetName, const ResourcePath& assetLocation, const ShaderProgramDescriptor& descriptor, ResourceCache& parentCache ) const override;
      [[nodiscard]] ShaderBuffer_uptr     newSB( const ShaderBufferDescriptor& descriptor ) const override;

private:
    GFXDevice& _context;
    SDL_Renderer* _renderer{ nullptr };
    SDL_Surface* _background{ nullptr };
    SDL_Texture* _texture{ nullptr };
};

};  // namespace Divide

#endif //DVD_NONE_WRAPPER_H_
