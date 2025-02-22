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
#ifndef DVD_RENDERER_H_
#define DVD_RENDERER_H_

#include "Core/Headers/PlatformContextComponent.h"
#include "Platform/Video/Headers/Commands.h"

namespace Divide {

class LightPool;
class ShaderProgram;
class ResourceCache;
class PlatformContext;

FWD_DECLARE_MANAGED_CLASS(PostFX);
FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

/// TiledForwardShading
class Renderer final : public PlatformContextComponent {
   public:
    Renderer(PlatformContext& context);
    ~Renderer() override;

    void prepareLighting(RenderStage stage, const Rect<I32>& viewport, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut);

    void idle(const U64 deltaTimeUSGame ) const;

    void updateResolution(U16 newWidth, U16 newHeight) const;

    [[nodiscard]] PostFX& postFX() { return *_postFX; }

    [[nodiscard]] const PostFX& postFX() const { return *_postFX; }

  private:
      struct PerRenderStageData {
          struct GridBuildData
          {
              mat4<F32> _invProjectionMatrix;
              Rect<I32> _viewport;
              float2 _zPlanes;
              [[nodiscard]] bool operator!=( const GridBuildData& other ) const noexcept;
          } _gridData;

          ShaderBuffer_uptr _lightIndexBuffer{ nullptr };
          ShaderBuffer_uptr _lightGridBuffer{ nullptr };
          ShaderBuffer_uptr _globalIndexCountBuffer{ nullptr };
          ShaderBuffer_uptr _lightClusterAABBsBuffer{ nullptr };
          bool _invalidated{true};
      };
    // No shadow stage
    std::array<PerRenderStageData, to_base(RenderStage::COUNT) - 1> _lightDataPerStage;

    Handle<ShaderProgram> _lightCullComputeShader = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _lightCounterResetComputeShader = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _lightBuildClusteredAABBsComputeShader = INVALID_HANDLE<ShaderProgram>;
    PostFX_uptr _postFX;

    GFX::BindPipelineCommand _lightCullPipelineCmd;
    GFX::BindPipelineCommand _lightResetCounterPipelineCmd;
    GFX::BindPipelineCommand _lightBuildClusteredAABBsPipelineCmd;
};


FWD_DECLARE_MANAGED_CLASS(Renderer);

};  // namespace Divide

#endif //DVD_RENDERER_H_
