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
#ifndef DVD_BLOOM_PRE_RENDER_OPERATOR_H_
#define DVD_BLOOM_PRE_RENDER_OPERATOR_H_

#include "Rendering/PostFX/Headers/PreRenderOperator.h"
#include "Platform/Video/Headers/PushConstants.h"

namespace Divide {

class RenderTarget;
class ShaderProgram;

class BloomPreRenderOperator final : public PreRenderOperator {
   public:
    BloomPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, std::atomic_uint& taskCounter);
    ~BloomPreRenderOperator() override;

    [[nodiscard]] bool execute(PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) override;
    void reshape(U16 width, U16 height) override;

    void filterRadius(F32 val);
    void strength(F32 val);

    [[nodiscard]] bool ready() const noexcept override;

    PROPERTY_R(F32, filterRadius, 0.005f);
    PROPERTY_R(F32, strength, 0.04f);

   private:
    Handle<ShaderProgram> _bloomDownscale{ INVALID_HANDLE<ShaderProgram> };
    Handle<ShaderProgram> _bloomUpscale{ INVALID_HANDLE<ShaderProgram> };

    Pipeline* _bloomDownscalePipeline{ nullptr };
    Pipeline* _bloomUpscalePipeline{ nullptr };

    PROPERTY_INTERNAL(U16, mipCount, 6u);
    vector<vec2<U16>> _mipSizes;
    bool _filterRadiusChanged{true};
    bool _mipSizesChanged{true};
};

}  // namespace Divide

#endif //DVD_BLOOM_PRE_RENDER_OPERATOR_H_
