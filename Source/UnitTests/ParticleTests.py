# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Regression tests for particle simulation and render preparation."""

from pathlib import Path
import unittest

if __package__:
    from .ExposureBufferTests import run_cpp
else:
    from ExposureBufferTests import run_cpp


ROOT = Path(__file__).resolve().parents[2]
PARTICLES = ROOT / "Source" / "Dynamics" / "Entities" / "Particles"


class ParticleTests(unittest.TestCase):
    def test_emission_uses_all_capacity_and_keeps_fraction_per_particle_data(self):
        source = (PARTICLES / "ParticleSource.cpp").read_text(encoding="utf-8")
        begin = source.index("void ParticleSource::emit(")
        end = source.index("\n} //namespace Divide", begin)
        emit = source[begin:end]
        run_cpp(self, f"""
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using F32 = float;
using D64 = double;
unsigned failures = 0;
bool check(bool value) {{ if (!value) ++failures; return value; }}
#define DIVIDE_ASSERT(condition, ...) check(condition)
#define TASK_NOP 0
struct Mutex {{
    bool locked = false;
    void lock() {{ assert(!locked); locked = true; }}
    void unlock() {{ assert(locked); locked = false; }}
}};
template<typename T> using LockGuard = std::lock_guard<T>;
template<typename T> U32 to_U32(T value) {{ return static_cast<U32>(value); }}
template<typename T> D64 to_D64(T value) {{ return static_cast<D64>(value); }}
namespace Time {{
    template<typename T> T MicrosecondsToSeconds(U64 value) {{ return static_cast<T>(value) / 1000000; }}
}}
struct ParticleData {{
    U32 capacity = 0, alive = 0;
    std::vector<unsigned> initialized;
    explicit ParticleData(U32 count) : capacity(count), initialized(count) {{}}
    U32 totalCount() const {{ return capacity; }}
    U32 aliveCount() const {{ return alive; }}
    void wake(U32 index) {{ assert(index == alive && index < capacity); ++alive; }}
}};
struct Task {{}};
Task* CreateTask(int) {{ static Task task; return &task; }}
struct TaskPool {{
    std::function<void()> onWait;
    void enqueue(Task&) {{}}
    void wait(Task&) {{
        auto action = std::move(onWait);
        onWait = {{}};
        if (action) action();
    }}
}};
enum class TaskPoolType {{ HIGH_PRIORITY }};
struct Context {{
    TaskPool pool;
    TaskPool& taskPool(TaskPoolType) {{ return pool; }}
}};
struct GFXDevice {{
    Context ctx;
    Context& context() {{ return ctx; }}
}};
struct ParticleGenerator {{
    unsigned calls = 0;
    void generate(Task&, TaskPool&, U64, ParticleData& data, U32 start, U32 end) {{
        ++calls;
        assert(start < end && end <= data.capacity);
        for (U32 i = start; i < end; ++i) ++data.initialized[i];
    }}
}};
struct ParticleSource {{
    F32 _emitRate = 0;
    GFXDevice& _context;
    std::vector<std::shared_ptr<ParticleGenerator>> _particleGenerators;
    Mutex _emissionLock;
    std::map<std::weak_ptr<ParticleData>, D64, std::owner_less<std::weak_ptr<ParticleData>>> _emissionRemainders;
    void emit(U64, const std::shared_ptr<ParticleData>&);
}};
{emit}
int main() {{
    GFXDevice device;
    ParticleSource source{{0.5f, device}};
    auto generator = std::make_shared<ParticleGenerator>();
    source._particleGenerators.push_back(generator);
    auto first = std::make_shared<ParticleData>(4);
    auto second = std::make_shared<ParticleData>(4);
    for (int i = 0; i < 3; ++i) {{
        source.emit(500000, first);
        source.emit(500000, second);
    }}
    assert(first->alive == 0 && second->alive == 0 && generator->calls == 0);
    source.emit(500000, first);
    assert(first->alive == 1 && second->alive == 0 && first->initialized[0] == 1);
    source.emit(500000, second);
    assert(second->alive == 1 && second->initialized[0] == 1);

    source._emitRate = 1.0f;
    auto single = std::make_shared<ParticleData>(1);
    source.emit(1000000, single);
    assert(single->alive == 1 && single->initialized[0] == 1);
    source.emit(10000000, single);
    single->alive = 0;
    source.emit(0, single);
    assert(single->alive == 0);
    source.emit(1000000, single);
    assert(single->alive == 1 && single->initialized[0] == 2);

    auto empty = std::make_shared<ParticleData>(0);
    source.emit(1000000, empty);
    assert(empty->alive == 0 && empty->initialized.empty());
    auto saturated = std::make_shared<ParticleData>(3);
    source._emitRate = std::numeric_limits<F32>::max();
    source.emit(std::numeric_limits<U64>::max(), saturated);
    assert(saturated->alive == 3 && saturated->initialized[2] == 1);

    source._emitRate = 1.0f;
    auto nested = std::make_shared<ParticleData>(2);
    auto outer = std::make_shared<ParticleData>(2);
    device.ctx.pool.onWait = [&] {{ source.emit(1000000, nested); }};
    source.emit(1000000, outer);
    assert(nested->alive == 1 && outer->alive == 1 && !source._emissionLock.locked);

    auto expired = std::make_shared<ParticleData>(1);
    source.emit(500000, expired);
    const auto oldSize = source._emissionRemainders.size();
    expired.reset();
    source.emit(0, outer);
    assert(source._emissionRemainders.size() + 1 == oldSize);

    source.emit(1000000, {{}});
    source._emitRate = -1;
    source.emit(1000000, first);
    source._emitRate = std::numeric_limits<F32>::quiet_NaN();
    source.emit(1000000, first);
    source._emitRate = 1;
    first->alive = first->capacity + 1;
    source.emit(1000000, first);
    assert(failures == 4);
}}
""")

    def test_sorted_particle_data_stays_aligned_and_clears_when_empty(self):
        source = (PARTICLES / "ParticleData.cpp").read_text(encoding="utf-8")
        begin = source.index("void ParticleData::sort()")
        end = source.index("\nvoid ParticleData::swapData(", begin)
        sort = source[begin:end]
        run_cpp(self, f"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>
using U32 = std::uint32_t;
using F32 = float;
namespace eastl {{ using std::begin; using std::end; using std::sort; }}
struct float4 {{ float value = 0, w = 0; void set(const float4& other) {{ *this = other; }} }};
struct Colour {{ int value = 0; }};
namespace Util {{ void ToByteColour(Colour input, Colour& output) {{ output = input; }} }}
struct ParticleData {{
    U32 alive = 0;
    std::vector<std::pair<U32, F32>> _indices;
    std::vector<float4> _renderingPositions, _position, _misc;
    std::vector<Colour> _renderingColours, _colour;
    U32 aliveCount() const {{ return alive; }}
    void sort();
}};
{sort}
int main() {{
    ParticleData data;
    data.alive = 3;
    data._position = {{{{10, 0}}, {{20, 0}}, {{30, 0}}, {{40, 0}}}};
    data._misc = {{{{0, 4}}, {{0, 16}}, {{0, 9}}, {{0, 99}}}};
    data._colour = {{{{1}}, {{2}}, {{3}}, {{4}}}};
    data.sort();
    assert(data._renderingPositions.size() == 3 && data._renderingColours.size() == 3);
    assert(data._renderingPositions[0].value == 20 && data._renderingColours[0].value == 2);
    assert(data._renderingPositions[1].value == 30 && data._renderingColours[1].value == 3);
    assert(data._renderingPositions[2].value == 10 && data._renderingColours[2].value == 1);
    data.alive = 0;
    data.sort();
    assert(data._indices.empty() && data._renderingPositions.empty() && data._renderingColours.empty());
}}
""")

    def test_emitter_rejects_invalid_data_and_accepts_empty_geometry(self):
        source = (PARTICLES / "ParticleEmitter.cpp").read_text(encoding="utf-8")
        begin = source.index("    [[nodiscard]] vector<U32> ParticleGeometryIndices(")
        end = source.index("\n}", begin)
        make_indices = source[begin:end]
        begin = source.index("bool ParticleEmitter::initData(")
        end = source.index("    for (U8 i = 0u; i < s_MaxPlayerBuffers; ++i)", begin)
        validate = source[begin:end] + "    return true;\n}\n"
        init = source[begin:source.index("bool ParticleEmitter::unload()", begin)]
        self.assertIn("if (!geometry.empty())", init)
        run_cpp(self, f"""
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>
using U32 = std::uint32_t;
constexpr U32 U32_MAX = std::numeric_limits<U32>::max();
template<typename T> using vector = std::vector<T>;
unsigned failures = 0;
bool check(bool value) {{ if (!value) ++failures; return value; }}
#define DIVIDE_ASSERT(condition, ...) check(condition)
U32 to_U32(std::size_t n) {{ return static_cast<U32>(n); }}
struct float3 {{ float x, y, z; }};
{make_indices}
struct ParticleData {{
    U32 count = 0;
    vector<float3> geometry;
    vector<U32> indices, _position, _acceleration, _colour, _misc;
    U32 totalCount() const {{ return count; }}
    const vector<float3>& particleGeometryVertices() const {{ return geometry; }}
    const vector<U32>& particleGeometryIndices() const {{ return indices; }}
}};
struct ParticleEmitter {{
    std::shared_ptr<ParticleData> _particles;
    U32 _geometryIndexCount = 0;
    bool initData(const std::shared_ptr<ParticleData>&);
}};
{validate}
int main() {{
    ParticleEmitter emitter;
    assert(!emitter.initData({{}}) && !emitter._particles);
    auto data = std::make_shared<ParticleData>();
    assert(!emitter.initData(data) && !emitter._particles);
    data->count = 2;
    data->_position.resize(2); data->_acceleration.resize(2); data->_misc.resize(2);
    assert(!emitter.initData(data) && !emitter._particles);
    data->_colour.resize(2);
    data->indices = {{0}};
    assert(!emitter.initData(data) && !emitter._particles);
    data->indices.clear();
    assert(emitter.initData(data) && emitter._geometryIndexCount == 0);
    data->geometry.resize(4);
    assert(emitter.initData(data) && emitter._geometryIndexCount == 4);
    data->indices = {{2, 1, 0}};
    assert(emitter.initData(data) && emitter._geometryIndexCount == 3);
    data->indices = {{0, 4}};
    assert(!emitter.initData(data) && emitter._geometryIndexCount == 3);
    data->indices = {{0, U32_MAX, 3}};
    assert(emitter.initData(data) && emitter._geometryIndexCount == 3);
    assert(failures == 5);
}}
""")

    def test_emitters_do_not_share_mutable_material_templates(self):
        source = (PARTICLES / "ParticleEmitter.cpp").read_text(encoding="utf-8")
        start = source.index("    const bool useTexture = !_particles->_textureFileName.empty();")
        end = source.index("\n    mat->setPipelineLayout", start)
        material_setup = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <map>
#include <memory>
#include <string>
struct Material {{ std::string texture; int topology = 0; }};
template<typename T> using Handle = int;
template<typename T> struct ResourceDescriptor {{
    std::string name;
    explicit ResourceDescriptor(std::string value) : name(std::move(value)) {{}}
}};
std::map<std::string, int> names;
std::map<int, Material> materials;
Handle<Material> CreateResource(const ResourceDescriptor<Material>& descriptor) {{
    auto [it, inserted] = names.emplace(descriptor.name, static_cast<int>(names.size() + 1));
    if (inserted) materials.emplace(it->second, Material{{}});
    return it->second;
}}
Material* Get(Handle<Material> handle) {{ return &materials.at(handle); }}
struct ParticleData {{ std::string _textureFileName; }};
struct ParticleEmitter {{
    std::string name;
    std::shared_ptr<ParticleData> _particles;
    std::string resourceName() const {{ return name; }}
    Material* createMaterial() {{
{material_setup}
        (void)useTexture;
        return mat;
    }}
}};
int main() {{
    ParticleEmitter fire{{"fire", std::make_shared<ParticleData>(ParticleData{{"fire.png"}})}};
    ParticleEmitter smoke{{"smoke", std::make_shared<ParticleData>(ParticleData{{"smoke.png"}})}};
    Material* first = fire.createMaterial();
    Material* second = smoke.createMaterial();
    assert(first != second);
    first->texture = "fire.png"; first->topology = 1;
    second->texture = "smoke.png"; second->topology = 2;
    assert(first->texture == "fire.png" && first->topology == 1);
    assert(second->texture == "smoke.png" && second->topology == 2);
    assert(fire.createMaterial() == first);
}}
""")

    def test_geometry_indices_and_instanced_indirection(self):
        source = (PARTICLES / "ParticleEmitter.cpp").read_text(encoding="utf-8")
        begin = source.index("    [[nodiscard]] vector<U32> ParticleGeometryIndices(")
        end = source.index("\n}", begin)
        make_indices = source[begin:end]
        begin = source.index("void ParticleEmitter::buildDrawCommands(")
        end = source.index("\nvoid ParticleEmitter::prepareRender(", begin)
        build_commands = source[begin:end]
        node_data = (ROOT / "Source" / "Rendering" / "RenderPass" / "Headers" / "NodeBufferedData.h").read_text(
            encoding="utf-8"
        )
        begin = node_data.index("    [[nodiscard]] constexpr bool RequiresInstancedIndirection(")
        end = node_data.index("\n    static_assert(", begin)
        instancing = node_data[begin:end]
        init = source[source.index("bool ParticleEmitter::initData("):
                      source.index("bool ParticleEmitter::unload()")]
        self.assertIn("mat->properties().isInstanced(true);", init)
        self.assertIn("ParticleGeometryIndices(geometry, particleData->particleGeometryIndices())", init)
        run_cpp(self, f"""
#include <cassert>
#include <cstdint>
#include <numeric>
#include <vector>
using U32 = std::uint32_t;
template<typename T> using vector = std::vector<T>;
struct float3 {{ float x, y, z; }};
{make_indices}
{instancing}
enum class CmdRenderOptions {{ RENDER_INDIRECT }};
struct GenericDrawCommand {{ struct {{ U32 indexCount = 0; }} _cmd; bool indirect = false; }};
using GenericDrawCommandContainer = vector<GenericDrawCommand>;
void toggleOption(GenericDrawCommand& cmd, CmdRenderOptions) {{ cmd.indirect = !cmd.indirect; }}
struct SceneGraphNode {{}};
struct SceneNode {{ void buildDrawCommands(SceneGraphNode*, GenericDrawCommandContainer&) {{}} }};
struct ParticleEmitter : SceneNode {{
    U32 _geometryIndexCount = 0;
    void buildDrawCommands(SceneGraphNode*, GenericDrawCommandContainer&);
}};
{build_commands}
int main() {{
    const vector<float3> quad(4);
    const auto sequential = ParticleGeometryIndices(quad, {{}});
    assert((sequential == vector<U32>{{0, 1, 2, 3}}));
    const vector<U32> explicitIndices{{3, 0, 2, 1}};
    assert(ParticleGeometryIndices(quad, explicitIndices) == explicitIndices);
    assert(ParticleGeometryIndices({{}}, {{}}).empty());
    ParticleEmitter emitter;
    GenericDrawCommandContainer commands;
    emitter._geometryIndexCount = static_cast<U32>(sequential.size());
    emitter.buildDrawCommands(nullptr, commands);
    assert(commands.size() == 1 && commands[0].indirect && commands[0]._cmd.indexCount == 4);
    for (U32 count : {{0u, 1u, 2u, 64u}}) {{
        assert(RequiresInstancedIndirection(false, true, count));
        assert(IndirectionBaseInstance(true, 37u) == 0u);
    }}
}}
""")

    def test_prepare_sorts_before_upload_and_uses_written_ring_slot(self):
        source = (PARTICLES / "ParticleEmitter.cpp").read_text(encoding="utf-8")
        begin = source.index("void ParticleEmitter::prepareRender(")
        end = source.index("\n/// Pre-process particles", begin)
        prepare = source[begin:end]
        run_cpp(self, f"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
using U8 = std::uint8_t;
using U32 = std::uint32_t;
unsigned assertionFailures = 0;
bool check(bool value) {{ if (!value) ++assertionFailures; return value; }}
#define DIVIDE_ASSERT(condition, ...) check(condition)
template<typename T> using vector = std::vector<T>;
using Mutex = std::mutex;
using SharedMutex = std::mutex;
template<typename T> using LockGuard = std::lock_guard<T>;
constexpr U32 g_particleBufferSizeFactor = 3;
constexpr U32 g_particleGeometryBuffer = 0, g_particlePositionBuffer = 1;
constexpr U32 g_particleColourBuffer = 2, g_particleIndexBuffer = 3;
U32 to_U32(std::size_t value) {{ return static_cast<U32>(value); }}
struct float3 {{
    float x = 0, y = 0, z = 0;
    float distanceSquared(const float3& p) const {{
        return (x-p.x)*(x-p.x) + (y-p.y)*(y-p.y) + (z-p.z)*(z-p.z);
    }}
}};
struct float4 {{ float3 xyz; float w = 0; }};
using UColour4 = U32;
struct BufferLock {{}};
struct GPUBuffer {{
    using Handle = U32;
    U32 current = 0, updates = 0;
    vector<float4> positions[3];
    vector<UColour4> colours[3];
    void incQueue() {{ current = (current + 1) % 3; }}
    std::size_t firstIndexOffsetCount() const {{ return 7; }}
    BufferLock updateBuffer(U32 offset, U32 count, const float4* data) {{
        assert(offset == 0); ++updates;
        positions[current].assign(data, data + count);
        return {{}};
    }}
    BufferLock updateBuffer(U32 offset, U32 count, const UColour4* data) {{
        assert(offset == 0); ++updates;
        colours[current].assign(data, data + count);
        return {{}};
    }}
}};
using GPUBuffer_ptr = GPUBuffer*;
struct ParticleData {{
    U32 alive = 0;
    vector<float4> _misc, _position, _renderingPositions;
    vector<UColour4> _renderingColours;
    void sort() {{
        vector<U32> indices(alive);
        for (U32 i = 0; i < alive; ++i) indices[i] = i;
        std::sort(indices.begin(), indices.end(), [&](U32 a, U32 b) {{ return _misc[a].w > _misc[b].w; }});
        _renderingPositions.clear(); _renderingColours.clear();
        for (U32 index : indices) {{
            _renderingPositions.push_back(_position[index]);
            _renderingColours.push_back(index + 1);
        }}
    }}
}};
struct GenericDrawCommand {{
    struct {{ U32 instanceCount = 0; }} _cmd;
    const GPUBuffer::Handle* _sourceBuffers = nullptr;
    U32 _sourceBuffersCount = 0;
}};
struct RenderingComponent {{
    struct DrawCommands {{ SharedMutex _dataLock; vector<GenericDrawCommand> _data{{1}}; }} commands;
    std::size_t offset = 0;
    void setIndexBufferElementOffset(std::size_t value) {{ offset = value; }}
    DrawCommands& drawCommands() {{ return commands; }}
}};
struct RenderPackage {{}};
namespace GFX {{ struct MemoryBarrierCommand {{ vector<BufferLock> _bufferLocks; }}; }}
enum class RenderStage {{ DISPLAY }};
enum class RenderPassType {{ MAIN_PASS, PRE_PASS }};
struct RenderStagePass {{ RenderStage _stage; RenderPassType _passType; }};
struct CameraSnapshot {{ float3 _eye; }};
struct Task {{}};
struct TaskPool {{}};
enum class TaskPoolType {{ HIGH_PRIORITY }};
struct Context {{ TaskPool pool; TaskPool& taskPool(TaskPoolType) {{ return pool; }} }};
struct SceneGraphNode {{ Context ctx; Context& context() {{ return ctx; }} }};
struct ParallelForDescriptor {{ U32 _iterCount = 0, _partitionSize = 0; }};
template<typename F> void Parallel_For(TaskPool&, ParallelForDescriptor d, F fn) {{
    if (d._iterCount) fn(nullptr, 0, d._iterCount);
}}
struct SceneNode {{
    void prepareRender(SceneGraphNode*, RenderingComponent&, RenderPackage&, GFX::MemoryBarrierCommand&,
                       RenderStagePass, const CameraSnapshot&, bool) {{}}
}};
struct ParticleEmitter : SceneNode {{
    struct ParticleBufferRet {{ GPUBuffer_ptr* _buffers; GPUBuffer::Handle* _handles; }};
    bool _enabled = true;
    U32 _geometryIndexCount = 4;
    std::shared_ptr<ParticleData> _particles;
    Mutex _particleRenderLock;
    GPUBuffer storage[4];
    GPUBuffer_ptr buffers[4]{{&storage[0], &storage[1], &storage[2], &storage[3]}};
    GPUBuffer::Handle handles[4]{{1, 2, 3, 4}};
    U32 getAliveParticleCount() const {{ return _particles->alive; }}
    ParticleBufferRet getDataBuffer(RenderStage, int) {{ return {{buffers, handles}}; }}
    void prepareRender(SceneGraphNode*, RenderingComponent&, RenderPackage&, GFX::MemoryBarrierCommand&,
                       RenderStagePass, const CameraSnapshot&, bool);
}};
{prepare}
int main() {{
    ParticleEmitter emitter;
    emitter._particles = std::make_shared<ParticleData>();
    auto& p = *emitter._particles;
    p._position = {{{{{{1, 0, 0}}}}, {{{{9, 0, 0}}}}, {{{{4, 0, 0}}}}}};
    p._misc.resize(3); p.alive = 2;
    SceneGraphNode node; RenderingComponent component; RenderPackage package; GFX::MemoryBarrierCommand barrier;
    const CameraSnapshot camera{{{{0, 0, 0}}}};
    emitter.prepareRender(&node, component, package, barrier, {{RenderStage::DISPLAY, RenderPassType::MAIN_PASS}}, camera, true);
    auto& position = emitter.storage[g_particlePositionBuffer];
    auto& colour = emitter.storage[g_particleColourBuffer];
    assert(position.current == 1 && colour.current == 1 && position.updates == 1);
    assert(position.positions[1].size() == 2 && position.positions[1][0].xyz.x == 9);
    assert((colour.colours[1] == vector<UColour4>{{2, 1}}));
    assert(component.commands._data[0]._cmd.instanceCount == 2 && component.offset == 7);
    emitter.prepareRender(&node, component, package, barrier, {{RenderStage::DISPLAY, RenderPassType::PRE_PASS}}, camera, false);
    assert(position.current == 1 && position.updates == 1);
    p.alive = 3;
    emitter.prepareRender(&node, component, package, barrier, {{RenderStage::DISPLAY, RenderPassType::MAIN_PASS}}, camera, true);
    assert(position.current == 2 && position.updates == 2);
    assert(position.positions[2].size() == 3 && position.positions[2][1].xyz.x == 4);
    assert(component.commands._data[0]._cmd.instanceCount == 3);
    emitter._geometryIndexCount = 0;
    component.commands._data.clear();
    emitter.prepareRender(&node, component, package, barrier, {{RenderStage::DISPLAY, RenderPassType::MAIN_PASS}}, camera, true);
    assert(position.current == 2 && position.updates == 2);
    emitter._geometryIndexCount = 4;
    emitter.prepareRender(&node, component, package, barrier, {{RenderStage::DISPLAY, RenderPassType::MAIN_PASS}}, camera, true);
    assert(assertionFailures == 1);
}}
""")

    def test_simulation_bounds_include_every_surviving_particle(self):
        source = (PARTICLES / "ParticleEmitter.cpp").read_text(encoding="utf-8")
        begin = source.index("void ParticleEmitter::sceneUpdate(")
        end = source.index("\nU32 ParticleEmitter::getAliveParticleCount()", begin)
        update = source[begin:end]
        run_cpp(self, f"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using F32 = float;
template<typename T> using vector = std::vector<T>;
constexpr U64 g_updateInterval = 33000;
struct float3 {{
    float x = 0, y = 0, z = 0;
    float3 operator+(const float3& v) const {{ return {{x + v.x, y + v.y, z + v.z}}; }}
    float3 operator-(const float3& v) const {{ return {{x - v.x, y - v.y, z - v.z}}; }}
}};
struct quatf {{}};
struct float4 {{ float3 xyz; float w = 0; }};
struct Misc {{ float z = 1; }};
struct Acceleration {{ void set(float) {{}} }};
struct BoundingBox {{
    float3 min{{}}, max{{}};
    void reset() {{ min = {{1e9f, 1e9f, 1e9f}}; max = {{-1e9f, -1e9f, -1e9f}}; }}
    void add(float3 p) {{
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }}
}};
struct ParticleData {{
    U32 alive = 0;
    vector<float4> _position;
    vector<Misc> _misc;
    vector<Acceleration> _acceleration;
    vector<float3> geometry{{{{0, 0, 0}}, {{3, 4, 17}}}};
    U32 aliveCount() const {{ return alive; }}
    const vector<float3>& particleGeometryVertices() const {{ return geometry; }}
}};
struct ParticleSource {{
    unsigned calls = 0;
    U64 lastDelta = 0;
    void updateTransform(const float3&, const quatf&) {{}}
    void emit(U64 delta, const std::shared_ptr<ParticleData>&) {{ ++calls; lastDelta = delta; }}
}};
struct ParticleUpdater {{
    bool killAll = false;
    U64 lastDelta = 0;
    void update(U64 delta, ParticleData& data) {{ lastDelta = delta; if (killAll) data.alive = 0; }}
}};
struct TransformComponent {{
    float3 position;
    quatf rotation;
    const float3& getWorldPosition() const {{ return position; }}
    const quatf& getWorldOrientation() const {{ return rotation; }}
}};
struct Task {{}};
struct TaskPool {{}};
enum class TaskPoolType {{ HIGH_PRIORITY }};
struct Context {{ TaskPool pool; TaskPool& taskPool(TaskPoolType) {{ return pool; }} }};
struct SceneGraphNode {{
    Context ctx;
    TransformComponent transform;
    Context& context() {{ return ctx; }}
    template<typename T> T* get() {{ return &transform; }}
}};
struct SceneState {{}};
struct ParallelForDescriptor {{ U32 _iterCount = 0, _partitionSize = 0; }};
template<typename F> void Parallel_For(TaskPool&, ParallelForDescriptor d, F fn) {{
    if (d._iterCount) fn(nullptr, 0, d._iterCount);
}}
struct RenderState {{ bool draw = false; void drawState(bool value) {{ draw = value; }} }};
struct SceneNode {{ void sceneUpdate(U64, SceneGraphNode*, SceneState&) {{}} }};
struct ParticleEmitter : SceneNode {{
    bool _enabled = true;
    std::shared_ptr<ParticleData> _particles;
    vector<std::shared_ptr<ParticleSource>> _sources;
    vector<std::shared_ptr<ParticleUpdater>> _updaters;
    RenderState state;
    BoundingBox bounds;
    unsigned boundsUpdates = 0;
    U32 getAliveParticleCount() const {{ return _particles->aliveCount(); }}
    RenderState& renderState() {{ return state; }}
    void setBounds(const BoundingBox& value) {{ bounds = value; ++boundsUpdates; }}
    void sceneUpdate(U64, SceneGraphNode*, SceneState&);
}};
{update}
int main() {{
    ParticleEmitter emitter;
    emitter._particles = std::make_shared<ParticleData>();
    auto& p = *emitter._particles;
    p._position = {{{{{{1, 2, 3}}}}, {{{{-4, 5, 6}}}}, {{{{90, -8, 7}}}}}};
    p._misc.resize(3);
    p._misc[1].z = -2;
    p._misc[2].z = 0.5f;
    p._acceleration.resize(3);
    p.alive = 3;
    SceneGraphNode node;
    SceneState scene;
    emitter.sceneUpdate(33000, &node, scene);
    assert(emitter.state.draw && emitter.boundsUpdates == 1);
    assert(emitter.bounds.min.x == -14 && emitter.bounds.max.x == 92.5f);
    assert(emitter.bounds.min.y == -10.5f && emitter.bounds.max.y == 15);
    assert(emitter.bounds.min.z == -4 && emitter.bounds.max.z == 16);
    for (unsigned i = 0; i < p.alive; ++i) assert(p._position[i].w == p._misc[i].z);

    auto source = std::make_shared<ParticleSource>();
    auto updater = std::make_shared<ParticleUpdater>();
    emitter._sources.push_back(source);
    emitter._updaters.push_back(updater);
    p.alive = 1;
    emitter.sceneUpdate(12000, &node, scene);
    assert(source->calls == 1 && emitter.state.draw && emitter.boundsUpdates == 2);
    assert(source->lastDelta == 12000 && updater->lastDelta == 12000);
    assert(emitter.bounds.min.x == -4 && emitter.bounds.max.x == 6);
    assert(emitter.bounds.min.y == -3 && emitter.bounds.max.y == 7);
    assert(emitter.bounds.min.z == -2 && emitter.bounds.max.z == 8);

    updater->killAll = true;
    emitter.sceneUpdate(33000, &node, scene);
    assert(source->calls == 2 && !emitter.state.draw && emitter.boundsUpdates == 2);
    emitter.sceneUpdate(33000, &node, scene);
    assert(source->calls == 3 && !emitter.state.draw && emitter.boundsUpdates == 2);
    updater->killAll = false;
    p.alive = 1;
    emitter.sceneUpdate(33000, &node, scene);
    assert(emitter.state.draw);
    emitter._enabled = false;
    emitter.sceneUpdate(33000, &node, scene);
    assert(!emitter.state.draw && source->calls == 4);
    emitter._enabled = true;
    emitter.sceneUpdate(33000, &node, scene);
    assert(emitter.state.draw && source->calls == 5);
    emitter._particles.reset();
    emitter.sceneUpdate(33000, &node, scene);
    assert(!emitter.state.draw && source->calls == 5);
}}
""")

    def test_world_space_bounds_survive_transforms_and_parent_aggregation(self):
        source = (ROOT / "Source" / "ECS" / "Components" / "BoundsComponent.cpp").read_text(encoding="utf-8")
        begin = source.index("void BoundsComponent::setRefBoundingBox(")
        end = source.index("\nbool Collision(", begin)
        implementation = source[begin:end]
        run_cpp(self, f"""
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>
using U32 = std::uint32_t;
using F32 = float;
#define PROFILE_SCOPE_AUTO(...)
struct float3 {{ float x = 0, y = 0, z = 0; }};
struct Matrix {{ float m[3][4]{{}}; }};
struct BoundingBox {{
    float3 min{{}}, max{{}};
    void set(const BoundingBox& b) {{ *this = b; }}
    void add(float3 p) {{
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }}
    void add(const BoundingBox& b) {{ add(b.min); add(b.max); }}
    void transform(const BoundingBox& b, const Matrix& m) {{
        min = {{1e9f, 1e9f, 1e9f}}; max = {{-1e9f, -1e9f, -1e9f}};
        for (unsigned i = 0; i < 8; ++i) {{
            const float p[] = {{i & 1 ? b.max.x : b.min.x, i & 2 ? b.max.y : b.min.y,
                               i & 4 ? b.max.z : b.min.z, 1}};
            float out[3]{{}};
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned c = 0; c < 4; ++c) out[r] += m.m[r][c] * p[c];
            add(float3{{out[0], out[1], out[2]}});
        }}
    }}
}};
struct BoundingSphere {{ BoundingBox box; void fromBoundingBox(const BoundingBox& b) {{ box = b; }} }};
struct OBB {{
    BoundingBox box;
    void fromBoundingBox(const BoundingBox& b) {{ box = b; }}
    void fromBoundingBox(const BoundingBox& b, const Matrix& m) {{ box.transform(b, m); }}
    BoundingSphere toEnclosingSphere() const {{ return {{box}}; }}
}};
enum class TransformType {{ ALL = 1 }};
constexpr U32 to_base(TransformType t) {{ return static_cast<U32>(t); }}
enum class ComponentType {{ BOUNDS }};
struct SharedMutex {{}};
template<typename T> struct SharedLock {{ explicit SharedLock(T&) {{}} }};
struct SceneNode {{ bool world = false; bool boundsInWorldSpace() const {{ return world; }} }};
struct TransformComponent {{ Matrix matrix; const Matrix& getWorldMatrix() const {{ return matrix; }} }};
struct BoundsComponent;
struct SceneGraphNode {{
    struct ChildContainer {{ mutable SharedMutex _lock; U32 _count = 0; std::vector<SceneGraphNode*> _data; }};
    SceneNode node;
    TransformComponent transform;
    BoundsComponent* bounds = nullptr;
    ChildContainer children;
    SceneNode& getNode() {{ return node; }}
    const ChildContainer& getChildren() const {{ return children; }}
    bool HasComponents(ComponentType) const {{ return bounds != nullptr; }}
    template<typename T> T* get() {{
        if constexpr (std::is_same_v<T, BoundsComponent>) return bounds;
        else return &transform;
    }}
}};
struct BoundsComponent {{
    SceneGraphNode* _parentSGN = nullptr;
    std::atomic_uint _transformUpdatedMask{{1}};
    BoundingBox _boundingBox{{}}, _refBoundingBox{{}};
    BoundingSphere _boundingSphere{{}};
    OBB _obb{{}};
    Matrix _lastTransform{{}};
    std::atomic_bool _obbDirty{{false}};
    bool _containsWorldSpaceBounds = false;
    void setRefBoundingBox(const BoundingBox&) noexcept;
    void updateBoundingBoxTransform();
    void appendChildRefBBs();
    void appendChildBBs();
    const OBB& getOBB();
}};
{implementation}
int main() {{
    const BoundingBox particle{{{{8, -4, 3}}, {{12, 6, 9}}}};
    Matrix transforms[5]{{}};
    transforms[0].m[0][0] = transforms[0].m[1][1] = transforms[0].m[2][2] = 1;
    transforms[0].m[0][3] = 100; transforms[0].m[1][3] = 200; transforms[0].m[2][3] = -50;
    transforms[1].m[0][1] = -1; transforms[1].m[1][0] = transforms[1].m[2][2] = 1;
    transforms[1].m[0][3] = 4; transforms[1].m[1][3] = 7; transforms[1].m[2][3] = 3;
    transforms[2].m[0][0] = 2; transforms[2].m[1][1] = 3; transforms[2].m[2][2] = 4;
    transforms[3] = transforms[2]; transforms[3].m[0][0] = -2;
    transforms[4].m[0][3] = 100; transforms[4].m[1][3] = 200; transforms[4].m[2][3] = -50;
    for (const Matrix& m : transforms) {{
        SceneGraphNode node;
        node.node.world = true;
        node.transform.matrix = m;
        BoundsComponent bounds;
        bounds._parentSGN = &node;
        bounds._lastTransform = m;
        node.bounds = &bounds;
        bounds.setRefBoundingBox(particle);
        bounds.updateBoundingBoxTransform();
        bounds.appendChildBBs();
        assert(bounds._boundingBox.min.x == 8 && bounds._boundingBox.max.y == 6);
        assert(bounds.getOBB().box.min.x == 8 && bounds.getOBB().box.max.z == 9);
    }}

    SceneGraphNode parent, child;
    BoundsComponent parentBounds, childBounds;
    parent.bounds = &parentBounds; parentBounds._parentSGN = &parent;
    child.bounds = &childBounds; childBounds._parentSGN = &child;
    child.node.world = true;
    parent.children._data.push_back(&child);
    parent.children._count = 1;
    const BoundingBox local{{{{-1, -1, -1}}, {{1, 1, 1}}}};
    parentBounds._lastTransform = transforms[0];
    parent.transform.matrix = transforms[0];
    parentBounds.setRefBoundingBox(local);
    childBounds.setRefBoundingBox(particle);
    parentBounds.appendChildRefBBs();
    assert(parentBounds._refBoundingBox.min.x == -1 && parentBounds._refBoundingBox.max.x == 1);
    parentBounds.updateBoundingBoxTransform();
    childBounds.updateBoundingBoxTransform();
    parentBounds.appendChildBBs();
    assert(parentBounds._boundingBox.min.x == 8 && parentBounds._boundingBox.max.x == 101);
    assert(parentBounds.getOBB().box.min.x == 8 && parentBounds.getOBB().box.max.x == 101);
}}
""")

    def test_particle_sort_and_bounds_do_not_escape_to_background_tasks(self):
        source = (PARTICLES / "ParticleEmitter.cpp").read_text(encoding="utf-8")
        header = (PARTICLES / "Headers" / "ParticleEmitter.h").read_text(encoding="utf-8")
        prepare = source[source.index("void ParticleEmitter::prepareRender("):
                         source.index("void ParticleEmitter::sceneUpdate(")]
        update = source[source.index("void ParticleEmitter::sceneUpdate("):
                        source.index("U32 ParticleEmitter::getAliveParticleCount()")]
        self.assertIn("_particles->sort();", prepare)
        for obsolete in ("_bufferUpdate", "_bbUpdate", "CreateTask("):
            self.assertNotIn(obsolete, prepare + update + header)
        self.assertNotIn("Parallel_For", prepare)
        sort = (PARTICLES / "ParticleData.cpp").read_text(encoding="utf-8")
        sort = sort[sort.index("void ParticleData::sort()"):sort.index("void ParticleData::swapData(")]
        self.assertNotIn("CreateTask(", sort)
        self.assertNotIn("pool.wait(", sort)
        self.assertNotIn("averageEmitRate", update)
        self.assertIn("i < aliveCount; ++i", update)


if __name__ == "__main__":
    unittest.main()
