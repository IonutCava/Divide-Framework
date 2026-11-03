# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Regression tests for post-processing host and shader contracts."""

from pathlib import Path
import re
import unittest

if __package__:
    from .ExposureBufferTests import run_cpp
else:
    from ExposureBufferTests import run_cpp


ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "Assets" / "Shaders" / "GLSL"
OPERATORS = ROOT / "Source" / "Rendering" / "PostFX" / "CustomOperators"


class PostFXTests(unittest.TestCase):
    def test_ssr_uses_previous_frame_and_current_camera_projection(self):
        shader = (SHADERS / "ScreenSpaceReflections.glsl").read_text(encoding="utf-8")
        host = (OPERATORS / "SSRPreRenderOperator.cpp").read_text(encoding="utf-8")
        batch = (ROOT / "Source" / "Rendering" / "PostFX" / "PreRenderBatch.cpp").read_text(encoding="utf-8")
        self.assertIn("RenderTargetNames::SCREEN_PREV", batch)
        self.assertIn("op->execute(idx, cameraSnapshot, prevScreenHandle", batch)
        self.assertIn('_ID( "previousViewProjectionMatrix" )', host)
        self.assertIn("sendPushConstantsCmd->_fastData.data[1] = cameraSnapshot._projectionMatrix;", host)
        self.assertIn("PreviousFrameUV(prevClip, prevUV)", shader)
        self.assertIn("textureLod(texScreen, prevUV", shader)
        self.assertNotIn('_ID( "previousViewMatrix" )', host)
        self.assertIn('_ID( "previousProjectionMatrix" )', host)

    def test_oit_normal_composition_uses_independent_alpha_blending(self):
        source = (ROOT / "Source" / "Rendering" / "RenderPass" / "RenderPassExecutor.cpp").read_text(encoding="utf-8")
        start = source.index("BlendingSettings& normalState =")
        end = source.index("pipelineDescriptor._shaderProgramHandle = OITCompositionShader;", start)
        normal_state = source[start:end]
        for expression in (
            "GFXDevice::ScreenTargets::NORMALS", "normalState.enabled( true )",
            "normalState.blendOp( BlendOperation::ADD )",
            "normalState.blendSrc( BlendProperty::SRC_ALPHA )",
            "normalState.blendDest( BlendProperty::INV_SRC_ALPHA )",
            "normalState.blendOpAlpha( BlendOperation::ADD )",
            "normalState.blendSrcAlpha( BlendProperty::ZERO )",
            "normalState.blendDestAlpha( BlendProperty::ONE )",
        ):
            self.assertIn(expression, normal_state)
        self.assertIn("s_OITCompositionPipeline = context.newPipeline( pipelineDescriptor )", source[end:])
        self.assertIn("s_OITCompositionMSPipeline = context.newPipeline( pipelineDescriptor )", source[end:])

    def test_preview_defaults_and_numeric_view_configuration(self):
        header = (ROOT / "Source" / "Platform" / "Video" / "Headers" / "GFXDevice.h").read_text(encoding="utf-8")
        begin = header.index('        _shaderData.set( _ID( "lodLevel" )')
        end = header.index("\n    }", begin)
        defaults = header[begin:end]
        run_cpp(self, f"""
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
using U32 = std::uint32_t;
enum class PushConstantType {{ FLOAT, UINT }};
#define _ID(value) value
struct Uniforms {{
    std::map<std::string, double> values;
    template<typename T> void set(const char* name, PushConstantType, T value) {{ values[name] = value; }}
}};
struct DebugView {{
    Uniforms _shaderData;
    DebugView() {{
{defaults}
    }}
}};
int main() {{
    DebugView first, second;
    assert(first._shaderData.values.size() == 8u);
    assert(first._shaderData.values.at("lodLevel") == 0.0);
    assert(first._shaderData.values.at("multiplier") == 1.0);
    assert(first._shaderData.values.at("channelCount") == 4.0);
    for (const char* name : {{"startChannel", "channelsArePacked", "scaleAndBias", "normalizeOutput", "skipLinearisation"}})
        assert(first._shaderData.values.at(name) == 0.0);
    first._shaderData.set("scaleAndBias", PushConstantType::UINT, 1u);
    assert(second._shaderData.values.at("scaleAndBias") == 0.0);
}}
""")
        host = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        start = host.index("void GFXDevice::initDebugViews()")
        end = host.index("void GFXDevice::renderDebugViews", start)
        previews = host[start:end]
        self.assertIn("_previewDepthMapShader == INVALID_HANDLE<ShaderProgram>", previews)
        for name in ("HiZ", "NormalPreview", "VelocityPreview", "SSAOPreview", "BloomPreview",
                     "AlphaAccumulationHigh", "AlphaRevealageHigh", "Luminance", "Edges"):
            self.assertRegex(
                previews,
                rf'{name}->_shaderData\.set\(\s*_ID\(\s*"skipLinearisation"\s*\),\s*'
                r"PushConstantType::UINT,\s*1u\s*\);",
            )
        self.assertRegex(previews, r'NormalPreview->_shaderData\.set\(\s*_ID\(\s*"scaleAndBias"\s*\)')
        splash = (ROOT / "Source" / "GUI" / "GUISplash.cpp").read_text(encoding="utf-8")
        for name in ("channelsArePacked", "scaleAndBias", "normalizeOutput"):
            self.assertIn(f'_ID( "{name}" ), PushConstantType::UINT, 0u', splash)
        self.assertNotRegex(
            previews,
            r'_ID\(\s*"(?:channelsArePacked|scaleAndBias|normalizeOutput)"\s*\),\s*PushConstantType::BOOL',
        )

    def test_depth_of_field_defines_match_shader(self):
        host = (OPERATORS / "DoFPreRenderOperator.cpp").read_text(encoding="utf-8")
        shader = (SHADERS / "DepthOfField.glsl").read_text(encoding="utf-8")
        self.assertIn('fragModule._defines.emplace_back("USE_DEPTH_BLUR");', host)
        self.assertIn('fragModule._defines.emplace_back("USE_NOISE");', host)
        self.assertNotIn('"USER_NOISE"', host)
        self.assertEqual(host.count('"FIRST_RING_SAMPLES {}"'), 1)
        self.assertIn("#if defined(USE_NOISE)", shader)

    def test_bloom_state_source_views_and_configuration(self):
        host = (OPERATORS / "BloomPreRenderOperator.cpp").read_text(encoding="utf-8")
        shader = (SHADERS / "bloom.glsl").read_text(encoding="utf-8")
        for field in ("filterRadius", "strength", "useThreshold", "threshold", "knee"):
            self.assertIn(f"_{field} = bloomConfig.{field};", host)
        self.assertIn("ImageView inputView = screenTex->getView();", host)
        self.assertIn("inputView = bloomTex->getView({._offset = i, ._count = 1u});", host)
        self.assertIn("1.f / vec2(textureSize(srcTexture, 0))", shader)
        self.assertIn("const F32 knee = std::max(_knee, 0.f);", host)
        start = host.index("for (U16 i = mipCount() - 1u")
        end = host.index("GFX::EnqueueCommand<GFX::EndDebugScopeCommand>", start)
        upsample = host[start:end]
        self.assertNotIn("if (", upsample)
        pipeline = upsample.index("BindPipelineCommand")
        push = upsample.index("SendPushConstantsCommand")
        draw = upsample.index("DrawCommand")
        self.assertLess(pipeline, push)
        self.assertLess(push, draw)
        self.assertIn("_bloomUpscalePipeline", upsample)
        self.assertNotIn("_filterRadiusChanged", host)

    def test_antialiasing_quality_contract(self):
        header = (OPERATORS / "Headers" / "PostAAPreRenderOperator.h").read_text(encoding="utf-8")
        begin = header.index("    static constexpr U8 MAX_SMAA_QUALITY_LEVEL")
        end = header.index("    [[nodiscard]] bool ready()", begin)
        contract = header[begin:end]
        run_cpp(self, f"""
#include <cstdint>
using U8 = std::uint8_t;
struct Quality {{
{contract}
}};
static_assert(Quality::maxQualityLevel(true) == 4u);
static_assert(Quality::maxQualityLevel(false) == 5u);
int main() {{}}
""")
        host = (OPERATORS / "PostAAPreRenderOperator.cpp").read_text(encoding="utf-8")
        editor = (ROOT / "Source" / "Editor" / "Widgets" / "DockedWindows" / "PostFXWindow.cpp").read_text(encoding="utf-8")
        smaa = (SHADERS / "SMAA.glsl").read_text(encoding="utf-8")
        fxaa = (SHADERS / "FXAA.glsl").read_text(encoding="utf-8")
        self.assertIn("postAAQualityLevel(std::min(postAAQualityLevel(), maxQualityLevel(useSMAA())))", host)
        self.assertEqual(host.count("std::max(postAAQualityLevel(), U8{1}) - 1u"), 2)
        self.assertIn("PostAAPreRenderOperator::maxQualityLevel(aaOp.useSMAA())", editor)
        for name in ("SMAA_STEPS", "SMAA_MAX_SEARCH_STEPS_VALUES", "SMAA_MAX_SEARCH_STEPS_DIAG_VALUES"):
            match = re.search(rf"{name}\[\]\s*=\s*\{{([^}}]+)\}}", smaa)
            self.assertIsNotNone(match, name)
            self.assertEqual(len(match.group(1).split(",")), 4, name)
        self.assertIn("clamp(dvd_qualityMultiplier, 0, SMAA_STEPS.length() - 1)", smaa)
        self.assertIn("clamp(dvd_qualityMultiplier, 0, SMAA_MAX_SEARCH_STEPS_VALUES.length() - 1)", smaa)
        self.assertIn("clamp(dvd_qualityMultiplier, 0, 4)", fxaa)

    def test_ssao_upsample_and_box_blur_boundaries(self):
        source = (SHADERS / "SSAOPass.glsl").read_text(encoding="utf-8")
        blur = source[source.index("--Fragment.SSAOBlur"):source.index("--Fragment.SSAOBlur.Nvidia")]
        upsample = source[source.index("--Fragment.SSAOUpsample"):]
        self.assertIn("x <= blurKernelSize", blur)
        self.assertIn("y <= blurKernelSize", blur)
        self.assertRegex(upsample, r"int best_depth_idx\s*=\s*0;")
        self.assertRegex(upsample, r"float min_dist\s*=\s*abs\(d0\s*-\s*d\[0\]\);")


if __name__ == "__main__":
    unittest.main()
