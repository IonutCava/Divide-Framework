# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Regression tests for scene data shared by CPU and GPU rendering."""

from pathlib import Path
import unittest

if __package__:
    from .ExposureBufferTests import run_cpp
else:
    from ExposureBufferTests import run_cpp


ROOT = Path(__file__).resolve().parents[2]


class SceneShaderTests(unittest.TestCase):
    def test_prefiltered_probe_mips_are_not_regenerated(self):
        source = (ROOT / "Source" / "Scenes" / "SceneEnvironmentProbePool.cpp").read_text(encoding="utf-8")
        start = source.index("void SceneEnvironmentProbePool::PrefilterEnvMap(")
        end = source.index("void SceneEnvironmentProbePool::ComputeIrradianceMap(", start)
        prefilter = source[start:end]
        self.assertNotIn("ComputeMipMapsCommand", prefilter)
        self.assertIn("destinationImage._subRange._mipLevels = { to_U8(mipLevel), 1u }", prefilter)
        self.assertIn("._sourceLayout = ImageUsage::SHADER_WRITE", prefilter)
        self.assertIn("._targetLayout = ImageUsage::SHADER_READ", prefilter)
        stages = source[source.index("void SceneEnvironmentProbePool::ProcessEnvironmentMapInternal("):start]
        self.assertLess(stages.index("case ComputationStages::MIP_MAP_SOURCE:"),
                        stages.index("case ComputationStages::PREFILTER_MAP:"))
        self.assertIn("computeMipMapsCommand._texture = sourceAtt->texture()", stages)
        gl = (ROOT / "Source" / "Platform" / "Video" / "RenderBackend" / "OpenGL" / "GLWrapper.cpp").read_text(encoding="utf-8")
        vk = (ROOT / "Source" / "Platform" / "Video" / "RenderBackend" / "Vulkan" / "Textures" / "vkSamplerObject.cpp").read_text(encoding="utf-8")
        self.assertIn("gl46core::glEnable( gl46core::GL_TEXTURE_CUBE_MAP_SEAMLESS", gl)
        self.assertIn("samplerInfo.flags = 0u", vk)

    def test_cpu_underwater_detection_checks_every_positioned_body(self):
        source = (ROOT / "Source" / "Scenes" / "Scene.cpp").read_text(encoding="utf-8")
        start = source.index("    bool Scene::checkCameraUnderwater( const Camera& camera )")
        end = source.index("\n    void Scene::findHoverTarget", start)
        function = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <cmath>
#include <vector>
#define PROFILE_SCOPE_AUTO(...)
struct float3 {{ float x, y, z; }};
struct WaterBodyData {{ float3 _extents; float3 _positionW; }};
struct Snapshot {{ float3 _eye; }};
struct Camera {{ Snapshot _snapshot; const Snapshot& snapshot() const {{ return _snapshot; }} }};
struct State {{ std::vector<WaterBodyData> _waters; const auto& waterBodies() const {{ return _waters; }} }};
struct Scene {{
    State _state;
    const State* state() const {{ return &_state; }}
    bool checkCameraUnderwater(const Camera& camera) const noexcept;
}};
{function}
int main() {{
    Scene scene;
    scene._state._waters = {{
        {{{{20.f, 1.f, 40.f}}, {{100.f, 10.f, -30.f}}}},
        {{{{20.f, 10.f, 40.f}}, {{100.f, 10.f, -30.f}}}},
        {{{{8.f, 6.f, 12.f}}, {{-50.f, 5.f, 60.f}}}},
        {{{{0.f, 0.f, 0.f}}, {{0.f, 0.f, 0.f}}}}
    }};
    auto inside = [&](float x, float y, float z) {{ return scene.checkCameraUnderwater(Camera{{{{x, y, z}}}}); }};
    assert(inside(100.f, 7.f, -30.f));
    assert(inside(110.f, 7.f, -10.f));
    assert(!inside(110.01f, 7.f, -30.f));
    assert(!inside(100.f, 10.f, -30.f));
    assert(!inside(100.f, 0.f, -30.f));
    assert(inside(-50.f, 4.f, 60.f));
    assert(!inside(0.f, -0.5f, 0.f));
}}
""")

    def test_water_gpu_slots_clear_and_respect_capacity(self):
        source = (ROOT / "Source" / "Managers" / "ProjectManager.cpp").read_text(encoding="utf-8")
        anchor = source.index("activeScene->updateSceneState( deltaGameTimeUS );")
        start = source.index("        U8 index = 0u;", anchor)
        end = source.index("        _saveTimer +=", start)
        update = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <cstdint>
#include <vector>
using U8 = std::uint8_t;
constexpr U8 GLOBAL_WATER_BODIES_COUNT = 2u;
struct WaterBodyData {{ int value = 0; }};
struct State {{
    std::vector<WaterBodyData> waters;
    const auto& waterBodies() const {{ return waters; }}
}};
struct SceneData {{
    WaterBodyData slots[GLOBAL_WATER_BODIES_COUNT];
    void waterDetails(U8 i, const WaterBodyData& body) {{
        assert(i < GLOBAL_WATER_BODIES_COUNT);
        slots[i] = body;
    }}
}};
void update(const State* activeSceneState, SceneData* sceneData) {{
{update}
}}
int main() {{
    State state{{{{{{1}}, {{2}}, {{3}}}}}};
    SceneData gpu{{}};
    update(&state, &gpu);
    assert(gpu.slots[0].value == 1 && gpu.slots[1].value == 2);
    state.waters = {{{{4}}}};
    update(&state, &gpu);
    assert(gpu.slots[0].value == 4 && gpu.slots[1].value == 0);
    state.waters.clear();
    update(&state, &gpu);
    assert(gpu.slots[0].value == 0 && gpu.slots[1].value == 0);
}}
""")


if __name__ == "__main__":
    unittest.main()
