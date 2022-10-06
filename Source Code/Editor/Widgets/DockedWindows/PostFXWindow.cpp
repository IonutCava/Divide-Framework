#include "stdafx.h"

#include "Headers/PostFXWindow.h"


#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Editor/Headers/Editor.h"
#include "Headers/Utils.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/CustomOperators/Headers/BloomPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/DoFPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/SSRPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/MotionBlurPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/PostAAPreRenderOperator.h"
#include "Rendering/PostFX/CustomOperators/Headers/SSAOPreRenderOperator.h"
#include "Rendering/PostFX/Headers/PostFX.h"

namespace Divide {
namespace {
    bool PreviewTextureButton(I32 &id, Texture* tex, const bool readOnly) {
        bool ret = false;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 15);
        ImGui::PushID(4321234 + id++);
        if (readOnly) {
            PushReadOnly();
        }
        if (ImGui::SmallButton("T")) {
            ret = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(Util::StringFormat("Preview texture : %s", tex->assetName().c_str()).c_str());
        }
        if (readOnly) {
            PopReadOnly();
        }
        ImGui::PopID();
        return ret;
    }

    constexpr I32 g_exposureRefreshFrameCount = 3;

}

    PostFXWindow::PostFXWindow(Editor& parent, PlatformContext& context, const Descriptor& descriptor)
        : DockedWindow(parent, descriptor),
          PlatformContextComponent(context),
          _postFX(context.gfx().getRenderer().postFX())
    {
    }

    void PostFXWindow::drawInternal() {
        PROFILE_SCOPE();

        PreRenderBatch& batch = _postFX.getFilterBatch();

        const auto checkBox = [this](const FilterType filter, const char* label = "Enabled", const bool overrideScene = false) {
            bool filterEnabled = _postFX.getFilterState(filter);
            ImGui::PushID(to_base(filter));
            if (ImGui::Checkbox(label, &filterEnabled)) {
                if (filterEnabled) {
                    _postFX.pushFilter(filter, overrideScene);
                } else {
                    _postFX.popFilter(filter, overrideScene);
                }
            }
            ImGui::PopID();
        };

        F32 edgeThreshold = batch.edgeDetectionThreshold();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Edge Threshold: "); ImGui::SameLine();

        ImGui::PushItemWidth(170);
        {
            if (ImGui::SliderFloat("##hidelabel", &edgeThreshold, 0.01f, 1.0f)) {
                batch.edgeDetectionThreshold(edgeThreshold);
            }
        }
        ImGui::PopItemWidth();

        if (ImGui::CollapsingHeader("Fog Settings")) {
            bool sceneChanged = false;
            SceneManager* sceneManager = context().kernel().sceneManager();
            auto& activeSceneState = sceneManager->getActiveScene().state();
            bool fogEnabled = context().config().rendering.enableFog;
            if (ImGui::Checkbox("Enabled", &fogEnabled)) {
                context().config().rendering.enableFog = fogEnabled;
                context().config().changed(true);
                sceneChanged = true;
            }
            {
                F32 fogDensity = activeSceneState->renderState().fogDetails()._colourAndDensity.a;
                if (ImGui::SliderFloat("Fog Density B", &fogDensity, 0.0001f, 0.25f, "%.6f")) {
                    FogDetails details = activeSceneState->renderState().fogDetails();
                    details._colourAndDensity.a = fogDensity;
                    activeSceneState->renderState().fogDetails(details);
                    sceneChanged = true;
                }
            }
            {
                F32 fogScatter = activeSceneState->renderState().fogDetails()._colourSunScatter.a;
                if (ImGui::SliderFloat("Fog Density C", &fogScatter, 0.0001f, 0.25f, "%.6f")) {
                    FogDetails details = activeSceneState->renderState().fogDetails();
                    details._colourSunScatter.a = fogScatter;
                    activeSceneState->renderState().fogDetails(details);
                    sceneChanged = true;
                }
            }
            {
                FColour3 fogColour = activeSceneState->renderState().fogDetails()._colourAndDensity.rgb;
                EditorComponentField tempField = {};
                tempField._name = "[Fog Colour]";
                tempField._basicType = GFX::PushConstantType::FCOLOUR3;
                tempField._type = EditorComponentFieldType::PUSH_TYPE;
                tempField._readOnly = false;
                tempField._data = &fogColour;
                tempField._format = "%.6f";
                tempField._range = { 0.0f, 1.0f };
                tempField._dataSetter = [&](const void* colour) noexcept {
                    FogDetails details = activeSceneState->renderState().fogDetails();
                    details._colourAndDensity.rgb = *static_cast<const FColour3*>(colour);
                    activeSceneState->renderState().fogDetails(details);
                };
                Util::PushNarrowLabelWidth();
                sceneChanged = Util::colourInput3(_parent, tempField) || sceneChanged;
                Util::PopNarrowLabelWidth();
            }

            if (sceneChanged) {
                Attorney::EditorGeneralWidget::registerUnsavedSceneChanges(_context.editor());
            }
        }

        if (ImGui::CollapsingHeader("SS Antialiasing")) {
            PreRenderOperator* op = batch.getOperator(FilterType::FILTER_SS_ANTIALIASING);
            PostAAPreRenderOperator& aaOp = static_cast<PostAAPreRenderOperator&>(*op);
            I32 level = to_I32(aaOp.postAAQualityLevel());

            ImGui::PushItemWidth(175);
            {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Quality level: "); ImGui::SameLine();
                ImGui::PushID("quality_level_slider");
                if (ImGui::SliderInt("##hidelabel", &level, 0, 5)) {
                    aaOp.postAAQualityLevel(to_U8(level));
                }
                ImGui::PopID();
            }
            ImGui::PopItemWidth();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Method: "); ImGui::SameLine();
            static I32 selection = _postFX.getFilterState(FilterType::FILTER_SS_ANTIALIASING) ? (aaOp.useSMAA() ? 0 : 1) : 2;
            const bool a = ImGui::RadioButton("SMAA", &selection, 0); ImGui::SameLine();
            const bool b = ImGui::RadioButton("FXAA", &selection, 1); ImGui::SameLine();
            const bool c = ImGui::RadioButton("NONE", &selection, 2);
            if (a || b|| c) {
                if (selection != 2) {
                    _postFX.pushFilter(FilterType::FILTER_SS_ANTIALIASING);
                    aaOp.useSMAA(selection == 0);
                } else {
                    _postFX.popFilter(FilterType::FILTER_SS_ANTIALIASING);
                }
            }
        }
        if (ImGui::CollapsingHeader("SS Ambient Occlusion")) {
            checkBox(FilterType::FILTER_SS_AMBIENT_OCCLUSION);
            PreRenderOperator* op = batch.getOperator(FilterType::FILTER_SS_AMBIENT_OCCLUSION);
            SSAOPreRenderOperator& ssaoOp = static_cast<SSAOPreRenderOperator&>(*op);
            F32 radius = ssaoOp.radius();
            F32 power = ssaoOp.power();
            F32 bias = ssaoOp.bias();
            F32 range = ssaoOp.maxRange() * 100.f;
            F32 fade = ssaoOp.fadeStart() * 100.f;
            bool halfRes = ssaoOp.genHalfRes();

            if (ImGui::Checkbox("Generate Half Resolution", &halfRes)) {
                ssaoOp.genHalfRes(halfRes);
            }
            if (ImGui::SliderFloat("Radius", &radius, 0.01f, 50.0f)) {
                ssaoOp.radius(radius);
            }
            if (ImGui::SliderFloat("Power", &power, 1.0f, 10.0f)) {
                ssaoOp.power(power);
            }
            if (ImGui::SliderFloat("Bias", &bias, 0.001f, 0.99f)) {
                ssaoOp.bias(bias);
            }
            if (ImGui::SliderFloat("Max Range (%)", &range, 0.001f, 100.f)) {
                ssaoOp.maxRange(range * 0.01f);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("100% - Far plane");
            }
            if (ImGui::SliderFloat("Fade Start (%)", &fade, 0.001f, 100.f)) {
                ssaoOp.fadeStart(fade * 0.01f);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Applies to the range [0 - Max Range], not to the far plane!");
            }
            bool blur = ssaoOp.blurResults();
            if (ImGui::Checkbox("Blur results", &blur)) {
                ssaoOp.blurResults(blur);
            }
            if (!blur) {
                PushReadOnly();
            }
            F32 blurThreshold = ssaoOp.blurThreshold();
            if (ImGui::SliderFloat("Blur threshold", &blurThreshold, 0.001f, 0.999f)) {
                ssaoOp.blurThreshold(blurThreshold);
            }
            F32 blurSharpness = ssaoOp.blurSharpness();
            if (ImGui::SliderFloat("Blur sharpness", &blurSharpness, 0.001f, 128.0f)) {
                ssaoOp.blurSharpness(blurSharpness);
            }
            I32 kernelSize = ssaoOp.blurKernelSize();
            if (ImGui::SliderInt("Blur kernel size", &kernelSize, 0, 16)) {
                ssaoOp.blurKernelSize(kernelSize);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0 = no blur");
            }
            if (!blur) {
                PopReadOnly();
            }
            ImGui::Text("SSAO Sample Count: %d", ssaoOp.sampleCount());
        }
        if (ImGui::CollapsingHeader("SS Reflections")) {
            checkBox(FilterType::FILTER_SS_REFLECTIONS);

            auto& params = context().config().rendering.postFX.ssr;
            F32& maxDistance = params.maxDistance;
            F32& jitterAmount = params.jitterAmount;
            F32& stride = params.stride;
            F32& zThickness = params.zThickness;
            F32& strideZCutoff = params.strideZCutoff;
            F32& screenEdgeFadeStart = params.screenEdgeFadeStart;
            F32& eyeFadeStart = params.eyeFadeStart;
            F32& eyeFadeEnd = params.eyeFadeEnd;
            U16& maxSteps = params.maxSteps;
            U8&  binarySearchIterations = params.binarySearchIterations;

            bool dirty = false;
            if (ImGui::SliderFloat("Max Distance", &maxDistance, 0.01f, 5000.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Jitter Ammount", &jitterAmount, 0.01f, 10.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Stride", &stride, 1.0f, maxSteps)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Z Thickness", &zThickness, 0.01f, 10.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Strode Z Cutoff", &strideZCutoff, 0.01f, 1000.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Screen Edge Fade Start", &screenEdgeFadeStart, 0.01f, 1.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Eye fade start", &eyeFadeStart, 0.01f, 1.0f)) {
                dirty = true;
            }
            if (ImGui::SliderFloat("Eye fade end", &eyeFadeEnd, eyeFadeStart, 1.0f)) {
                dirty = true;
            }

            constexpr U16 stepsMin = 1u; 
            constexpr U16 stepsMax = 1 << 15;

            if (ImGui::SliderScalar("Max Steps", ImGuiDataType_U16, &maxSteps, &stepsMin, &stepsMax)) {
                dirty = true;
            } 
            constexpr U8 iterMin = 1u;
            constexpr U8 iterMax = 1 << 7;
            if (ImGui::SliderScalar("Binary Search Iterations", ImGuiDataType_U8, &binarySearchIterations, &iterMin, &iterMax)) {
                dirty = true;
            }

            if (dirty) {
                PreRenderOperator* op = batch.getOperator(FilterType::FILTER_SS_REFLECTIONS);
                static_cast<SSRPreRenderOperator&>(*op).parametersChanged();
                context().config().changed(true);
            }
        }
        if (ImGui::CollapsingHeader("Depth of Field")) {
            checkBox(FilterType::FILTER_DEPTH_OF_FIELD);

            auto& params = context().config().rendering.postFX.dof;
           
            bool dirty = false;
            F32& focalLength = params.focalLength;
            if (ImGui::SliderFloat("Focal Length (mm)", &focalLength, 0.0f, 100.0f)) {
                dirty = true;
            }

            I32 crtStop = to_I32(TypeUtil::StringToFStops(params.fStop));
            const char* crtStopName = params.fStop.c_str();
            if (ImGui::SliderInt("FStop", &crtStop, 0, to_base(FStops::COUNT) - 1, crtStopName)) {
                params.fStop = TypeUtil::FStopsToString(static_cast<FStops>(crtStop));
                dirty = true;
            }

            bool& autoFocus = params.autoFocus;
            if (ImGui::Checkbox("Auto Focus", &autoFocus)) {
                dirty = true;
            }

            if (autoFocus) {
                PushReadOnly();
            }
            F32& focalDepth = params.focalDepth;
            if (ImGui::SliderFloat("Focal Depth (m)", &focalDepth, 0.0f, 100.0f)) {
                dirty = true;
            }
            vec2<F32>& focalPosition = params.focalPoint;
            if (ImGui::SliderFloat2("Focal Position", focalPosition._v, 0.0f, 1.0f)) {
                dirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Position of focused point on screen (0.0,0.0 - left lower corner, 1.0,1.0 - upper right)");
            }
            if (autoFocus) {
                PopReadOnly();
            }

            bool& manualdof = params.manualdof;
            if (ImGui::Checkbox("Manual dof calculation", &manualdof)) {
                dirty = true;
            }
            if (!manualdof) {
                PushReadOnly();
            }
            F32& ndofstart = params.ndofstart;
            if (ImGui::SliderFloat("Near dof blur start", &ndofstart, 0.0f, 100.0f)) {
                dirty = true;
            }
            F32& ndofdist = params.ndofdist;
            if (ImGui::SliderFloat("Near dof blur falloff distance", &ndofdist, 0.0f, 100.0f)) {
                dirty = true;
            }
            F32& fdofstart = params.fdofstart;
            if (ImGui::SliderFloat("Far dof blur start", &fdofstart, 0.0f, 100.0f)) {
                dirty = true;
            }
            F32& fdofdist = params.fdofdist;
            if (ImGui::SliderFloat("Far dof blur falloff distance", &fdofdist, 0.0f, 100.0f)) {
                dirty = true;
            }
            if (!manualdof) {
                PopReadOnly();
            }
            bool& vignetting = params.vignetting;
            if (ImGui::Checkbox("Use optical lens vignetting", &vignetting)) {
                dirty = true;
            }
            if (!vignetting) {
                PushReadOnly();
            }
            F32& vignout = params.vignout;
            if (ImGui::SliderFloat("Vignetting outer border", &vignout, 0.0f, 100.0f)) {
                dirty = true;
            }
            F32& vignin = params.vignin;
            if (ImGui::SliderFloat("Vignetting inner border", &vignin, 0.0f, 100.0f)) {
                dirty = true;
            }
            if (!vignetting) {
                PopReadOnly();
            }
          
            bool& debugFocus = params.debugFocus;
            if (ImGui::Checkbox("Show debug focus point and focal range", &debugFocus)) {
                dirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("red = focal point, green = focal range");
            }
 
            if (dirty) {
                PreRenderOperator* op = batch.getOperator(FilterType::FILTER_DEPTH_OF_FIELD);
                DoFPreRenderOperator& dofOp = static_cast<DoFPreRenderOperator&>(*op);
                dofOp.parametersChanged();
                context().config().changed(true);
            }
        }

        if (ImGui::CollapsingHeader("Bloom")) {
            checkBox(FilterType::FILTER_BLOOM);
            PreRenderOperator* op = batch.getOperator(FilterType::FILTER_BLOOM);
            BloomPreRenderOperator& bloomOp = static_cast<BloomPreRenderOperator&>(*op);
            F32 threshold = bloomOp.luminanceThreshold();
            if (ImGui::SliderFloat("Luminance Threshold", &threshold, 0.0f, 1.0f)) {
                bloomOp.luminanceThreshold(threshold);
            }
        }

        if (ImGui::CollapsingHeader("Motion Blur")) {
            checkBox(FilterType::FILTER_MOTION_BLUR);
            PreRenderOperator* op = batch.getOperator(FilterType::FILTER_MOTION_BLUR);
            MotionBlurPreRenderOperator& blurOP = static_cast<MotionBlurPreRenderOperator&>(*op);
            F32& velocity = _context.config().rendering.postFX.motionBlur.velocityScale;
            if (ImGui::SliderFloat("Veclocity Scale", &velocity, 0.01f, 3.0f)) {
                blurOP.parametersChanged();
                _context.config().changed(true);
            }
            U8 samples = blurOP.maxSamples(); constexpr U8 min = 1u, max = 16u;
            if (ImGui::SliderScalar("Max Samples", ImGuiDataType_U8, &samples, &min, &max)) {
                blurOP.maxSamples(samples);
            }
        }

        if (ImGui::CollapsingHeader("Tone Mapping")) {
            bool adaptiveExposure = batch.adaptiveExposureControl();
            if (ImGui::Checkbox("Adaptive Exposure", &adaptiveExposure)) {
                batch.adaptiveExposureControl(adaptiveExposure);
            }

            bool dirty = false;
            ToneMapParams params = batch.toneMapParams();
            if (adaptiveExposure) {
                if (ImGui::SliderFloat("Min Log Luminance", &params._minLogLuminance, -16.0f, 0.0f)) {
                    dirty = true;
                }
                if (ImGui::SliderFloat("Max Log Luminance", &params._maxLogLuminance, 0.0f, 16.0f)) {
                    dirty = true;
                }
                if (ImGui::SliderFloat("Tau", &params._tau, 0.1f, 2.0f)) {
                    dirty = true;
                }

                static I32 exposureRefreshFrameCount = 1;
                static F32 exposure = 1.0f;
                if (--exposureRefreshFrameCount == 0) {
                    exposure = batch.adaptiveExposureValue();
                    exposureRefreshFrameCount = g_exposureRefreshFrameCount;
                }

                ImGui::Text("Current exposure value: %5.2f", exposure);
                I32 id = 32132131;
                if (PreviewTextureButton(id, batch.luminanceTex().get(), false)) {
                    _previewTexture = batch.luminanceTex().get();
                }
            }

            if (ImGui::SliderFloat("Manual exposure", &params._manualExposureFactor, 0.01f, 100.0f)) {
                dirty = true;
            }

            I32 crtFunc = to_I32(params._function);
            const char* crtFuncName = TypeUtil::ToneMapFunctionsToString(params._function);
            if (ImGui::SliderInt("ToneMap Function", &crtFunc, 0, to_base(ToneMapParams::MapFunctions::COUNT), crtFuncName)) {
                params._function = static_cast<ToneMapParams::MapFunctions>(crtFunc);
                dirty = true;
            }

            if (dirty) {
                batch.toneMapParams(params);
            }
        }
        if (ImGui::CollapsingHeader("Test Effects")) {
            checkBox(FilterType::FILTER_NOISE, PostFX::FilterName(FilterType::FILTER_NOISE), true);
            checkBox(FilterType::FILTER_VIGNETTE, PostFX::FilterName(FilterType::FILTER_VIGNETTE), true);
            checkBox(FilterType::FILTER_UNDERWATER, PostFX::FilterName(FilterType::FILTER_UNDERWATER), true);
            checkBox(FilterType::FILTER_LUT_CORECTION, PostFX::FilterName(FilterType::FILTER_LUT_CORECTION), true);
        }

        if (_previewTexture != nullptr) {
            if (Attorney::EditorGeneralWidget::modalTextureView(_context.editor(), Util::StringFormat("Image Preview: %s", _previewTexture->resourceName().c_str()).c_str(), _previewTexture, vec2<F32>(512, 512), true, false)) {
                _previewTexture = nullptr;
            }
        }
    }
}