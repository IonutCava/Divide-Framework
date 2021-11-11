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
#ifndef _VK_WRAPPER_H_
#define _VK_WRAPPER_H_

#include "VKPlaceholderObjects.h"

#include "Platform/Video/Headers/RenderAPIWrapper.h"
namespace Divide {

class VK_API final : public RenderAPIWrapper {
  public:
    VK_API(GFXDevice& context) noexcept;

  protected:
      void idle(bool fast) noexcept override;
      void beginFrame(DisplayWindow& window, bool global = false) noexcept override;
      void endFrame(DisplayWindow& window, bool global = false) noexcept override;

      ErrorCode initRenderingAPI(I32 argc, char** argv, Configuration& config) noexcept override;
      void closeRenderingAPI() noexcept override;
      [[nodiscard]] PerformanceMetrics getPerformanceMetrics() const noexcept override;
      void preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) noexcept override;
      void flushCommand(const GFX::CommandBuffer::CommandEntry& entry, const GFX::CommandBuffer& commandBuffer) noexcept override;
      void postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) noexcept override;
      [[nodiscard]] vec2<U16> getDrawableSize(const DisplayWindow& window) const noexcept override;
      [[nodiscard]] U32 getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const noexcept override;
      bool setViewport(const Rect<I32>& newViewport) noexcept override;
      void onThreadCreated(const std::thread::id& threadID) noexcept override;
};

};  // namespace Divide
#endif //_VK_WRAPPER_H_
