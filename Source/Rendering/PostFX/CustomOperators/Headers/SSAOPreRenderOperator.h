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
#ifndef DVD_SSAO_PRE_RENDER_OPERATOR_H_
#define DVD_SSAO_PRE_RENDER_OPERATOR_H_

#include "Rendering/PostFX/Headers/PreRenderOperator.h"
#include "Platform/Video/Headers/Commands.h"

namespace Divide {

class SSAOPreRenderOperator final : public PreRenderOperator {
   public:
    SSAOPreRenderOperator(GFXDevice& context, PreRenderBatch& parent, std::atomic_uint& taskCounter);
    ~SSAOPreRenderOperator() override;

    [[nodiscard]] bool execute(PlayerIndex idx, const CameraSnapshot& cameraSnapshot, const RenderTargetHandle& input, const RenderTargetHandle& output, GFX::CommandBuffer& bufferInOut) override;
    void reshape(U16 width, U16 height) override;

    [[nodiscard]] F32 radius() const noexcept { return _radius[_genHalfRes ? 1 : 0]; }
    void radius(F32 val);

    [[nodiscard]] F32 power() const noexcept { return _power[_genHalfRes ? 1 : 0]; }
    void power(F32 val);

    [[nodiscard]] F32 bias() const noexcept { return _bias[_genHalfRes ? 1 : 0]; }
    void bias(F32 val);

    [[nodiscard]] bool genHalfRes() const noexcept { return _genHalfRes; }
    void genHalfRes(bool state);

    [[nodiscard]] bool blurResults() const noexcept { return _blur[_genHalfRes ? 1 : 0]; }
    void blurResults(bool state) noexcept;

    [[nodiscard]] F32 blurThreshold() const noexcept { return _blurThreshold[_genHalfRes ? 1 : 0]; }
    void blurThreshold(F32 val); 
    
    [[nodiscard]] F32 blurSharpness() const noexcept { return _blurSharpness[_genHalfRes ? 1 : 0]; }
    void blurSharpness(F32 val);
    
    [[nodiscard]] I32 blurKernelSize() const noexcept { return _kernelSize[_genHalfRes ? 1 : 0]; }
    void blurKernelSize(I32 val);

    [[nodiscard]] F32 maxRange() const noexcept { return _maxRange[_genHalfRes ? 1 : 0]; }
    void maxRange(F32 val);

    [[nodiscard]] F32 fadeStart() const noexcept { return _fadeStart[_genHalfRes ? 1 : 0]; }
    void fadeStart(F32 val);

    [[nodiscard]] U8 sampleCount() const noexcept;

    [[nodiscard]] bool ready() const noexcept override;

   protected:
       void onToggle(const bool state) override { PreRenderOperator::onToggle(state); _stateChanged = true; }
       void prepare(PlayerIndex idx, GFX::CommandBuffer& bufferInOut) override;

   private:
    UniformData _ssaoGenerateConstants;
    UniformData _ssaoBlurConstants;
    Handle<ShaderProgram> _ssaoGenerateShader = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _ssaoGenerateHalfResShader = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _ssaoBlurShaderHorizontal = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _ssaoBlurShaderVertical = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _ssaoPassThroughShader = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _ssaoDownSampleShader = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _ssaoUpSampleShader = INVALID_HANDLE<ShaderProgram>;
    Pipeline* _downsamplePipeline = nullptr;
    Pipeline* _generateHalfResPipeline = nullptr;
    Pipeline* _upsamplePipeline = nullptr;
    Pipeline* _generateFullResPipeline = nullptr;
    Pipeline* _blurHorizontalPipeline = nullptr;
    Pipeline* _blurVerticalPipeline = nullptr;
    Pipeline* _passThroughPipeline = nullptr;

    Handle<Texture> _noiseTexture = INVALID_HANDLE<Texture>;
    SamplerDescriptor _noiseSampler{};
    RenderTargetHandle _ssaoOutput;
    RenderTargetHandle _ssaoHalfResOutput;
    RenderTargetHandle _halfDepthAndNormals;
    RenderTargetHandle _ssaoBlurBuffer;
    F32 _radius[2] = { 0.0f, 0.0f };
    F32 _bias[2] = { 0.0f, 0.0f };
    F32 _power[2] = { 0.0f, 0.0f };
    F32 _maxRange[2] = { 1.f, 1.f };
    F32 _fadeStart[2] = { 1.f, 1.f };
    F32 _blurThreshold[2] = { 0.05f, 0.05f };
    F32 _blurSharpness[2] = { 40.f, 40.f };
    I32 _kernelSize[2] = { 3, 3 };
    U8  _kernelSampleCount[2] = { 0u, 0u };
    bool _blur[2] = { false, false };
    bool _stateChanged = false;
    bool _genHalfRes = false;
};

}  // namespace Divide

#endif //DVD_SSAO_PRE_RENDER_OPERATOR_H_
