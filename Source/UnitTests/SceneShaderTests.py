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
    def test_shader_buffer_reads_and_clears_the_captured_ring_slot(self):
        source = (ROOT / "Source" / "Platform" / "Video" / "Buffers" / "ShaderBuffer" /
                  "ShaderBuffer.cpp").read_text(encoding="utf-8")
        begin = source.index("BufferLock ShaderBuffer::clearData(")
        end = source.index("\n} //namespace Divide", begin)
        functions = source[begin:end]
        run_cpp(self, f"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>
#define PROFILE_SCOPE_AUTO(...)
#define DIVIDE_ASSERT(condition, ...) (condition)
using U16 = std::uint16_t;
using U32 = std::uint32_t;
using bufferPtr = void*;
std::size_t to_size(U16 value) {{ return value; }}
template<typename T = std::size_t> struct ElementRange {{ T _startOffset, _length; }};
template<typename T = std::size_t> struct BufferRange {{ T _startOffset, _length; }};
struct BufferLock {{ std::size_t offset, length; }};
enum class BufferUsageType {{ UNBOUND_BUFFER }};
enum class BufferUpdateFrequency {{ ONCE, OCCASIONAL }};
namespace Util {{
    std::size_t GetAlignmentCorrected(std::size_t value, std::size_t alignment) {{
        return (value + alignment - 1) / alignment * alignment;
    }}
}}
struct ShaderBuffer {{
    struct {{ std::size_t _elementSize = 4; bool _hostVisible = true; }} _params;
    std::size_t _alignedBufferSize = 16, _alignmentRequirement = 4;
    U16 current = 0;
    unsigned writes = 0;
    unsigned char storage[64]{{}};
    U16 queueIndex() const {{ return current; }}
    U16 queueLength() const {{ return 4; }}
    BufferUsageType getUsage() const {{ return BufferUsageType::UNBOUND_BUFFER; }}
    BufferUpdateFrequency getUpdateFrequency() const {{ return BufferUpdateFrequency::OCCASIONAL; }}
    void readBytesInternal(BufferRange<> range, std::pair<bufferPtr, std::size_t> target) {{
        std::memcpy(target.first, storage + range._startOffset, range._length);
    }}
    BufferLock writeBytesInternal(BufferRange<> range, bufferPtr data) {{
        ++writes;
        if (data) std::memcpy(storage + range._startOffset, data, range._length);
        else std::memset(storage + range._startOffset, 0, range._length);
        return {{range._startOffset, range._length}};
    }}
    BufferLock clearData(ElementRange<>);
    BufferLock clearData(ElementRange<>, U16);
    BufferLock clearBytes(BufferRange<>);
    BufferLock clearBytes(BufferRange<>, U16);
    BufferLock writeData(ElementRange<>, bufferPtr);
    BufferLock writeBytes(BufferRange<>, bufferPtr);
    BufferLock writeBytes(BufferRange<>, bufferPtr, U16);
    void readData(ElementRange<>, std::pair<bufferPtr, std::size_t>);
    void readData(ElementRange<>, std::pair<bufferPtr, std::size_t>, U16);
    void readBytes(BufferRange<>, std::pair<bufferPtr, std::size_t>);
    void readBytes(BufferRange<>, std::pair<bufferPtr, std::size_t>, U16);
}};
{functions}
int main() {{
    ShaderBuffer buffer;
    for (U16 i = 0; i < 4; ++i) {{
        buffer.current = i;
        U32 value = 100 + i;
        const auto written = buffer.writeData({{0, 1}}, &value);
        assert(written.offset == 16 * i && written.length == 4);
    }}
    U32 result = 0;
    buffer.readData({{0, 1}}, {{&result, sizeof(result)}}, 0);
    assert(result == 100 && buffer.current == 3);
    const auto cleared = buffer.clearData({{0, 1}}, 1);
    assert(cleared.offset == 16 && buffer.current == 3);
    buffer.readData({{0, 1}}, {{&result, sizeof(result)}}, 1);
    assert(result == 0);
    buffer.readData({{0, 1}}, {{&result, sizeof(result)}});
    assert(result == 103);
    buffer.clearData({{0, 1}});
    buffer.readData({{0, 1}}, {{&result, sizeof(result)}}, 2);
    assert(result == 102);
    const unsigned validWrites = buffer.writes;
    U32 value = 999;
    assert(buffer.writeBytes({{0, 4}}, &value, 4).length == 0);
    assert(buffer.writeBytes({{12, 8}}, &value, 0).length == 0);
    assert(buffer.writeBytes({{static_cast<std::size_t>(-1), 4}}, &value, 0).length == 0);
    assert(buffer.writeBytes({{1, 4}}, &value, 0).length == 0);
    assert(buffer.writeBytes({{0, 0}}, &value, 0).length == 0);
    assert(buffer.writes == validWrites);
    buffer.readData({{0, 1}}, {{&result, sizeof(result)}}, 2);
    assert(result == 102);
    const auto explicitWrite = buffer.writeBytes({{4, 4}}, &value, 2);
    assert(explicitWrite.offset == 36 && explicitWrite.length == 4 && buffer.current == 3);
    buffer.readData({{1, 1}}, {{&result, sizeof(result)}}, 2);
    assert(result == 999);
}}
""")
        host = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        begin = host.index("    void GFXDevice::occlusionCull(")
        end = host.index("\n#pragma endregion", begin)
        cull = host[begin:end]
        self.assertIn("LockGuard<Mutex> counterLock(_cullCounterLock);", cull)
        self.assertIn("readAtomicCounter._queueIndex = counterQueueIndex;", cull)
        self.assertIn("clearAtomicCounter._queueIndex = cullBuffer->queueIndex();", cull)
        self.assertIn("const bool collectStats = queryPerformanceStats() && countCulledNodes;", cull)

    def test_cull_counter_initializes_every_reused_ring_slot(self):
        source = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        begin = source.index("                ShaderBuffer& counter = *_gfxBuffers._perFrameBuffers[i]._cullCounter;")
        end = source.index("\n            }", begin)
        initialize = source[begin:end]
        run_cpp(self, f"""
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#define DIVIDE_UNUSED(value) (void)(value)
using U16 = std::uint16_t;
struct BufferLock {{}};
struct Range {{ unsigned offset, length; }};
struct ShaderBuffer {{
    std::vector<std::array<unsigned, 4>> slots;
    U16 current = 0;
    U16 queueLength() const {{ return static_cast<U16>(slots.size()); }}
    BufferLock clearData(Range range) {{
        assert(range.offset == 0 && range.length == 1);
        slots[current].fill(0);
        return {{}};
    }}
    void incQueue() {{ current = (current + 1) % queueLength(); }}
}};
struct Frame {{ std::unique_ptr<ShaderBuffer> _cullCounter; }};
struct Device {{
    struct {{ std::array<Frame, 3> _perFrameBuffers; }} _gfxBuffers;
    void initialize(unsigned i) {{
{initialize}
    }}
}};
int main() {{
    Device device;
    for (U16 length : {{1, 4, 7}}) {{
        for (auto& frame : device._gfxBuffers._perFrameBuffers) {{
            frame._cullCounter = std::make_unique<ShaderBuffer>();
            frame._cullCounter->slots.resize(length);
            for (auto& slot : frame._cullCounter->slots) slot.fill(0xdeadbeefu);
        }}
        for (unsigned i = 0; i < device._gfxBuffers._perFrameBuffers.size(); ++i)
            device.initialize(i);
        for (const auto& frame : device._gfxBuffers._perFrameBuffers) {{
            assert(frame._cullCounter->current == 0);
            for (const auto& slot : frame._cullCounter->slots)
                for (unsigned value : slot) assert(value == 0);
        }}
    }}
}}
""")

    def test_previous_view_projection_uses_both_matrices_per_player(self):
        source = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        start = source.index("    void GFXDevice::prevViewProjectionMatrix(")
        end = source.index("    bool GFXDevice::setViewport(", start)
        functions = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <cstddef>
#define PROFILE_SCOPE_AUTO(...)
using F32 = float;
using PlayerIndex = std::size_t;
template<typename T> struct mat4 {{
    T m[2][2] = {{{{1, 0}}, {{0, 1}}}};
    bool operator!=(const mat4& other) const {{
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                if (m[r][c] != other.m[r][c]) return true;
        return false;
    }}
    static void Multiply(const mat4& a, const mat4& b, mat4& out) {{
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c];
    }}
}};
struct GFXShaderData {{
    struct CamData {{ mat4<F32> dvd_PrevViewProjectionMatrix; }};
    struct PrevFrameData {{
        mat4<F32> _previousViewMatrix;
        mat4<F32> _previousProjectionMatrix;
        mat4<F32> _previousViewProjectionMatrix;
    }};
}};
struct GFXDevice {{
    struct {{ GFXShaderData::CamData _camData; GFXShaderData::PrevFrameData _prevFrameData[2]; bool _camNeedsUpload = false; }} _gpuBlock;
    void prevViewProjectionMatrix(const mat4<F32>& vpMatrix) noexcept;
    void setPreviousViewProjectionMatrix(PlayerIndex index, const mat4<F32>& view, mat4<F32> projection);
}};
{functions}
int main() {{
    GFXDevice device;
    const mat4<F32> view{{{{{{1, 3}}, {{0, 1}}}}}};
    const mat4<F32> projection{{{{{{2, 0}}, {{0, 4}}}}}};
    const mat4<F32> expected{{{{{{2, 6}}, {{0, 4}}}}}};
    const mat4<F32> identity;
    device.setPreviousViewProjectionMatrix(0, view, projection);
    assert(!(device._gpuBlock._camData.dvd_PrevViewProjectionMatrix != expected));
    assert(device._gpuBlock._camNeedsUpload);
    device._gpuBlock._camNeedsUpload = false;
    device.setPreviousViewProjectionMatrix(0, view, projection);
    assert(!device._gpuBlock._camNeedsUpload);
    device.setPreviousViewProjectionMatrix(1, identity, identity);
    assert(!(device._gpuBlock._camData.dvd_PrevViewProjectionMatrix != identity));
    assert(device._gpuBlock._camNeedsUpload);
    device._gpuBlock._camNeedsUpload = false;
    device.setPreviousViewProjectionMatrix(0, view, projection);
    assert(!(device._gpuBlock._camData.dvd_PrevViewProjectionMatrix != expected));
    assert(device._gpuBlock._camNeedsUpload);
}}
""")

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
