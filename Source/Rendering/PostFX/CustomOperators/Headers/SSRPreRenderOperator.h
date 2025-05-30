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
#ifndef DVD_SSR_PRE_RENDER_OPERATOR_H_
#define DVD_SSR_PRE_RENDER_OPERATOR_H_

#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/PostFX/Headers/PreRenderOperator.h"
#include "Platform/Video/Headers/Commands.h"

namespace Divide {

class SSRPreRenderOperator final : public PreRenderOperator {
   public:
    explicit SSRPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, std::atomic_uint& taskCounter);
    ~SSRPreRenderOperator();

    [[nodiscard]] bool execute(PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) override;
    void reshape(U16 width, U16 height) override;

    void parametersChanged();

    [[nodiscard]] bool ready() const noexcept override;

   protected:
       void onToggle(const bool state) override { PreRenderOperator::onToggle(state); _stateChanged = true; }
       void prepare(PlayerIndex idx, GFX::CommandBuffer& bufferInOut) override;

   private:
     Handle<ShaderProgram> _ssrShader = INVALID_HANDLE<ShaderProgram>;
     GFX::BindPipelineCommand _pipelineCmd;
     UniformData _uniforms;
     mat4<F32> _projToPixelBasis;
     float2 _cachedZPlanes = VECTOR2_UNIT;
     bool _constantsDirty = true;
     bool _stateChanged = false;
};

}  // namespace Divide

#endif //DVD_SSR_PRE_RENDER_OPERATOR_H_
