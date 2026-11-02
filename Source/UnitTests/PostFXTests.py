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
