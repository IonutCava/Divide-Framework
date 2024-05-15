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

/// shader from:
/// http://www.geeks3d.com/20110405/fxaa-fast-approximate-anti-aliasing-demo-glsl-opengl-test-radeon-geforce/

#pragma once
#ifndef DVD_POST_AA_PRE_RENDER_OPERATOR_H_
#define DVD_POST_AA_PRE_RENDER_OPERATOR_H_

#include "Rendering/PostFX/Headers/PreRenderOperator.h"
#include "Platform/Video/Headers/Commands.h"

namespace Divide {

class PostAAPreRenderOperator final : public PreRenderOperator {
   public:
    explicit PostAAPreRenderOperator(GFXDevice& context, PreRenderBatch& parent);
    ~PostAAPreRenderOperator();

    [[nodiscard]] bool execute(PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) override;
    void reshape(U16 width, U16 height) override;

    PROPERTY_RW(U8, postAAQualityLevel, 2u);
    PROPERTY_RW(bool, useSMAA, false);

    [[nodiscard]] bool ready() const noexcept override;

  private:
    PROPERTY_INTERNAL(U8, currentPostAAQualityLevel, 2u);
    PROPERTY_INTERNAL(bool, currentUseSMAA, false);

    Handle<ShaderProgram> _fxaa = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _smaaWeightComputation = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _smaaBlend = INVALID_HANDLE<ShaderProgram>;

    Handle<Texture> _searchTexture = INVALID_HANDLE<Texture>;
    Handle<Texture> _areaTexture = INVALID_HANDLE<Texture>;

    RenderTargetHandle _smaaWeights;

    Pipeline* _fxaaPipeline = nullptr;
    Pipeline* _smaaWeightPipeline = nullptr;
    Pipeline* _smaaBlendPipeline = nullptr;
};

}  // namespace Divide

#endif //DVD_POST_AA_PRE_RENDER_OPERATOR_H_
