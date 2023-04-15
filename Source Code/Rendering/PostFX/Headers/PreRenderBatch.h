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
#ifndef _PRE_RENDER_BATCH_H_
#define _PRE_RENDER_BATCH_H_

#include "PreRenderOperator.h"
#include "Platform/Video/Headers/CommandsImpl.h"

namespace Divide {

FWD_DECLARE_MANAGED_CLASS(ShaderProgram);
FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

class PostFX;

struct ToneMapParams
{
    enum class MapFunctions : U8
    {
        REINHARD,
        REINHARD_MODIFIED,
        GT,
        ACES,
        UNREAL_ACES,
        AMD_ACES,
        GT_DIFF_PARAMETERS,
        UNCHARTED_2,
        COUNT
    };

    U16 _width{ 1u };
    U16 _height{ 1u };
    F32 _minLogLuminance{ -4.f };
    F32 _maxLogLuminance{ 3.f };
    F32 _tau{ 1.1f };

    F32 _manualExposureFactor{ 1.f };

    MapFunctions _function{ MapFunctions::UNCHARTED_2 };
};

namespace Names {
    static const char* toneMapFunctions[] = {
        "REINHARD", "REINHARD_MODIFIED", "GT", "ACES", "UNREAL_ACES", "AMD_ACES", "GT_DIFF_PARAMETERS", "UNCHARTED_2", "NONE"
    };
}

namespace TypeUtil {
    [[nodiscard]] const char* ToneMapFunctionsToString(ToneMapParams::MapFunctions stop) noexcept;
    [[nodiscard]] ToneMapParams::MapFunctions StringToToneMapFunctions(const string& name);
};

class ResourceCache;
class PreRenderBatch {
   private:
    struct HDRTargets {
        RenderTargetHandle _screenRef;
        RenderTargetHandle _screenCopy;
    };

    struct LDRTargets {
        RenderTargetHandle _temp[2];
    };

    struct ScreenTargets {
        HDRTargets _hdr;
        LDRTargets _ldr;
        bool _swappedHDR{ false };
        bool _swappedLDR{ false };
    };

    using OperatorBatch = vector<PreRenderOperator_uptr>;

   public:
       // Ordered by cost
       enum class EdgeDetectionMethod : U8 {
           Depth = 0,
           Luma,
           Colour,
           COUNT
       };

   public:
    PreRenderBatch(GFXDevice& context, PostFX& parent, ResourceCache* cache);
    ~PreRenderBatch();

    [[nodiscard]] PostFX& parent() const noexcept { return _parent; }

    void update(U64 deltaTimeUS) noexcept;

    void prePass(PlayerIndex idx, const CameraSnapshot& cameraSnapshot, U32 filterStack, GFX::CommandBuffer& bufferInOut);
    void execute(PlayerIndex idx, const CameraSnapshot& cameraSnapshot, U32 filterStack, GFX::CommandBuffer& bufferInOut);
    void reshape(U16 width, U16 height);

    void onFilterToggle(FilterType filter, bool state);

    [[nodiscard]] RenderTargetHandle getInput(bool hdr) const;
    [[nodiscard]] RenderTargetHandle getOutput(bool hdr) const;

    [[nodiscard]] RenderTargetHandle screenRT() const noexcept;
    [[nodiscard]] RenderTargetHandle edgesRT() const noexcept;
    [[nodiscard]] Texture_ptr luminanceTex() const noexcept;

    [[nodiscard]] PreRenderOperator* getOperator(FilterType type) const;

    void toneMapParams(ToneMapParams params) noexcept;

    void adaptiveExposureControl(bool state) noexcept;

    [[nodiscard]] F32 adaptiveExposureValue() const noexcept;
    [[nodiscard]] RenderTargetHandle getLinearDepthRT() const noexcept;

    PROPERTY_R(bool, adaptiveExposureControl, true);
    PROPERTY_R(ToneMapParams, toneMapParams);
    PROPERTY_RW(F32, edgeDetectionThreshold, 0.1f);
    PROPERTY_RW(EdgeDetectionMethod, edgeDetectionMethod, EdgeDetectionMethod::Luma);
    PROPERTY_R(size_t, lumaSamplingHash, 0u);

   private:

    [[nodiscard]] inline static FilterSpace GetOperatorSpace(const FilterType type) noexcept {
        // ToDo: Always keep this up-to-date with every filter we add
        switch(type) {
            case FilterType::FILTER_SS_ANTIALIASING :     return FilterSpace::FILTER_SPACE_LDR;
            case FilterType::FILTER_SS_AMBIENT_OCCLUSION:
            case FilterType::FILTER_SS_REFLECTIONS:       return FilterSpace::FILTER_SPACE_HDR;
            case FilterType::FILTER_DEPTH_OF_FIELD:
            case FilterType::FILTER_BLOOM:
            case FilterType::FILTER_MOTION_BLUR:          return FilterSpace::FILTER_SPACE_HDR_POST_SS;
            case FilterType::FILTER_LUT_CORECTION:
            case FilterType::FILTER_COUNT:
            case FilterType::FILTER_UNDERWATER: 
            case FilterType::FILTER_NOISE: 
            case FilterType::FILTER_VIGNETTE: break;
        }

        return FilterSpace::COUNT;
    }

    [[nodiscard]] bool operatorsReady() const noexcept;

    [[nodiscard]] RenderTargetHandle getTarget(bool hdr, bool swapped) const noexcept;

  private:
    GFXDevice& _context;
    PostFX&    _parent;

    std::array<OperatorBatch, to_base(FilterSpace::COUNT)> _operators;
    std::array<ShaderProgram_ptr, to_base(EdgeDetectionMethod::COUNT)> _edgeDetection{};
    std::array<Pipeline*, to_base(EdgeDetectionMethod::COUNT)> _edgeDetectionPipelines{};

    ScreenTargets _screenRTs{};
    ShaderBuffer_uptr _histogramBuffer{ nullptr };
    Texture_ptr _currentLuminance{ nullptr };
    ShaderProgram_ptr _toneMap{ nullptr };
    ShaderProgram_ptr _toneMapAdaptive{ nullptr };
    ShaderProgram_ptr _createHistogram{ nullptr };
    ShaderProgram_ptr _averageHistogram{ nullptr };
    ShaderProgram_ptr _lineariseDepthBuffer{ nullptr };
    ResourceCache* _resCache{ nullptr };
    Pipeline* _pipelineLumCalcHistogram{ nullptr };
    Pipeline* _pipelineLumCalcAverage{ nullptr };
    Pipeline* _pipelineToneMap{ nullptr };
    Pipeline* _pipelineToneMapAdaptive{ nullptr };
    U64 _lastDeltaTimeUS{ 0u };

    RenderTargetHandle _sceneEdges{};
    RenderTargetHandle _linearDepthRT{};

    F32 _adaptiveExposureValue{1.f};
    mutable bool _adaptiveExposureValueNeedsUpdate{false};
};

}  // namespace Divide
#endif //_PRE_RENDER_BATCH_H_

#include "PreRenderBatch.inl"
