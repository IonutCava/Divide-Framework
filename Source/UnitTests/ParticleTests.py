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
    def test_simulation_bounds_include_every_surviving_particle(self):
        source = (PARTICLES / "ParticleEmitter.cpp").read_text(encoding="utf-8")
        begin = source.index("void ParticleEmitter::sceneUpdate(")
        end = source.index("\nU32 ParticleEmitter::getAliveParticleCount()", begin)
        update = source[begin:end]
        run_cpp(self, f"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using F32 = float;
template<typename T> using vector = std::vector<T>;
constexpr U64 g_updateInterval = 33000;
struct float3 {{ float x = 0, y = 0, z = 0; }};
struct quatf {{}};
struct Position {{ float3 xyz; float w = 0; }};
struct Misc {{ float z = 1; }};
struct Acceleration {{ void set(float) {{}} }};
struct BoundingBox {{
    float3 min{{1e9f, 1e9f, 1e9f}}, max{{-1e9f, -1e9f, -1e9f}};
    void add(float3 p) {{
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }}
}};
struct ParticleData {{
    U32 alive = 0;
    vector<Position> _position;
    vector<Misc> _misc;
    vector<Acceleration> _acceleration;
    U32 aliveCount() const {{ return alive; }}
}};
struct ParticleSource {{
    unsigned calls = 0;
    void updateTransform(const float3&, const quatf&) {{}}
    void emit(U64, const std::shared_ptr<ParticleData>&) {{ ++calls; }}
}};
struct ParticleUpdater {{
    bool killAll = false;
    void update(U64, ParticleData& data) {{ if (killAll) data.alive = 0; }}
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
    p._acceleration.resize(3);
    p.alive = 3;
    SceneGraphNode node;
    SceneState scene;
    emitter.sceneUpdate(33000, &node, scene);
    assert(emitter.state.draw && emitter.boundsUpdates == 1);
    assert(emitter.bounds.min.x == -4 && emitter.bounds.max.x == 90);
    assert(emitter.bounds.min.y == -8 && emitter.bounds.max.y == 5);
    for (const auto& position : p._position) assert(position.w == 1);

    auto source = std::make_shared<ParticleSource>();
    auto updater = std::make_shared<ParticleUpdater>();
    emitter._sources.push_back(source);
    emitter._updaters.push_back(updater);
    p.alive = 1;
    emitter.sceneUpdate(33000, &node, scene);
    assert(source->calls == 1 && emitter.state.draw && emitter.boundsUpdates == 2);
    assert(emitter.bounds.min.x == 1 && emitter.bounds.max.x == 1);

    updater->killAll = true;
    emitter.sceneUpdate(33000, &node, scene);
    assert(source->calls == 2 && !emitter.state.draw && emitter.boundsUpdates == 2);
    emitter.sceneUpdate(33000, &node, scene);
    assert(source->calls == 3 && !emitter.state.draw && emitter.boundsUpdates == 2);
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
        self.assertNotIn("averageEmitRate", update)
        self.assertIn("i < aliveCount; ++i", update)


if __name__ == "__main__":
    unittest.main()
