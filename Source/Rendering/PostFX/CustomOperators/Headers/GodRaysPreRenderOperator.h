/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

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
#ifndef DVD_GOD_RAYS_PRE_RENDER_OPERATOR_H_
#define DVD_GOD_RAYS_PRE_RENDER_OPERATOR_H_

#include "Rendering/PostFX/Headers/PreRenderOperator.h"
#include "Platform/Video/Headers/PushConstants.h"

namespace Divide {

class GodRaysPreRenderOperator final : public PreRenderOperator {
   public:
    GodRaysPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, std::atomic_uint& taskCounter);
    ~GodRaysPreRenderOperator() override;

    [[nodiscard]] bool execute(PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) override;
    void reshape(U16 width, U16 height) override;

    void density(F32 val);
    void decay(F32 val);
    void weight(F32 val);
    void exposure(F32 val);
    void sampleCount(U8 val);

    [[nodiscard]] bool ready() const noexcept override;

    PROPERTY_R(F32, density, 0.84f);
    PROPERTY_R(F32, decay, 0.95f);
    PROPERTY_R(F32, weight, 0.4f);
    PROPERTY_R(F32, exposure, 0.2f);
    PROPERTY_R(U8, sampleCount, 64u);

   private:
    Handle<ShaderProgram> _occlusionShader{ INVALID_HANDLE<ShaderProgram> };
    Handle<ShaderProgram> _radialBlurShader{ INVALID_HANDLE<ShaderProgram> };

    Pipeline* _occlusionPipeline{ nullptr };
    Pipeline* _radialBlurPipeline{ nullptr };

    RenderTargetHandle _occlusionRT{};
};

}  // namespace Divide

#endif //DVD_GOD_RAYS_PRE_RENDER_OPERATOR_H_
