# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Execute the affected shader macros and compare them with CPU references."""

import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from ShaderCompilationTests import SHADERS, cluster_config, expand_includes, section, shadow_config


VALIDATOR = os.environ.get("GLSLANG_VALIDATOR") or shutil.which("glslangValidator")
RUNNER = os.environ.get("SHADER_MATH_RUNNER")
INPUTS = [
    0.0, 0.125, 0.25,
    1.0 / 2.75 - 0.0001, 1.0 / 2.75, 1.0 / 2.75 + 0.0001,
    0.5,
    2.0 / 2.75 - 0.0001, 2.0 / 2.75, 2.0 / 2.75 + 0.0001,
    0.75, 0.875,
    2.5 / 2.75 - 0.0001, 2.5 / 2.75, 2.5 / 2.75 + 0.0001,
    1.0,
]
EXPRESSION_INPUTS = [0.125, 0.375, 0.625]
EASING_NAMES = [
    "easeInBack", "easeOutBack", "easeInOutBack",
    "easeInElastic", "easeOutElastic", "easeInOutElastic",
    "easeInBounce", "easeOutBounce", "easeInOutBounce",
]
PACKING_OFFSET = 16 + len(EASING_NAMES) * (len(INPUTS) + len(EXPRESSION_INPUTS))
RESULT_COUNT = PACKING_OFFSET + 4


def shader_source():
    inputs = ", ".join(f"{value:.9f}f" for value in INPUTS)
    expression_inputs = ", ".join(f"{value:.9f}f" for value in EXPRESSION_INPUTS)
    calls = "\n".join(f"        values[base + {index}] = {name}(x);"
                      for index, name in enumerate(EASING_NAMES))
    expression_calls = "\n".join(f"        values[base + {index}] = {name}(x + 0.125f);"
                                 for index, name in enumerate(EASING_NAMES))
    return f"""
#version 460
#define COMPUTE_SHADER
#define float4 vec4
#define M_PI 3.14159265358979323846
#define MAX_CLIP_PLANES 4
#define PER_BATCH 2
#define DESCRIPTOR_SET_RESOURCE_LAYOUT(SET, BINDING, LAYOUT) layout(set = SET, binding = BINDING, LAYOUT)
#include "nodeDataDefinition.cmn"
#include "utility.frag"
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};

void main() {{
    TransformData t;
    t._position = vec4(3.f, -2.f, 5.f, 0.f);
    t._scale = vec4(2.f, 0.5f, -1.5f, 0.f);
    t._rotation = vec4(0.f, 0.7071067811865476f, 0.f, 0.7071067811865476f);
    mat4 terrain = mat4(vec4(1.5f, 0.f, 0.f, 0.f), vec4(0.f, 0.5f, 0.f, 0.f),
                        vec4(0.f, 0.f, 2.f, 0.f), vec4(7.f, 1.f, -4.f, 1.f));
    vec4 point = vec4(0.25f, 2.f, -1.f, 1.f);
    vec4 actual = TransformMatrix(terrain, t) * point;
    vec4 reference = Transform(terrain * point, t);
    for (int i = 0; i < 4; ++i) {{
        values[i] = actual[i];
        values[4 + i] = reference[i];
    }}

    t._position = vec4(-1.f, 3.f, 2.f, 0.f);
    t._scale = vec4(1.1f, 2.2f, 0.7f, 0.f);
    t._rotation = vec4(0.5f, 0.f, 0.f, 0.8660254037844386f);
    terrain = mat4(vec4(1.f, 0.25f, 0.f, 0.f), vec4(-0.2f, 0.8f, 0.3f, 0.f),
                   vec4(0.1f, 0.f, 1.4f, 0.f), vec4(2.f, -3.f, 4.f, 1.f));
    point = vec4(2.f, -1.f, 0.5f, 1.f);
    actual = TransformMatrix(terrain, t) * point;
    reference = Transform(terrain * point, t);
    for (int i = 0; i < 4; ++i) {{
        values[8 + i] = actual[i];
        values[12 + i] = reference[i];
    }}

    const float inputs[{len(INPUTS)}] = float[{len(INPUTS)}]({inputs});
    for (int i = 0; i < {len(INPUTS)}; ++i) {{
        float x = inputs[i];
        int base = 16 + {len(EASING_NAMES)} * i;
{calls}
    }}
    const float expressionInputs[{len(EXPRESSION_INPUTS)}] = float[{len(EXPRESSION_INPUTS)}]({expression_inputs});
    for (int i = 0; i < {len(EXPRESSION_INPUTS)}; ++i) {{
        float x = expressionInputs[i];
        int base = {16 + len(EASING_NAMES) * len(INPUTS)} + {len(EASING_NAMES)} * i;
{expression_calls}
    }}
    const vec2 packedA = unpackVec2(packVec2(0.125f + 0.125f, -0.5f * 0.5f));
    const vec2 packedB = unpackVec2(packVec2(0.f, 1.f));
    values[{PACKING_OFFSET}] = packedA.x;
    values[{PACKING_OFFSET + 1}] = packedA.y;
    values[{PACKING_OFFSET + 2}] = packedB.x;
    values[{PACKING_OFFSET + 3}] = packedB.y;
}}
"""


def bounce(x):
    n, d = 7.5625, 2.75
    if x < 1 / d:
        return n * x * x
    if x < 2 / d:
        return n * (x - 1.5 / d) ** 2 + 0.75
    if x < 2.5 / d:
        return n * (x - 2.25 / d) ** 2 + 0.9375
    return n * (x - 2.625 / d) ** 2 + 0.984375


def easing(x):
    c1 = 1.70158
    c2 = c1 * 1.525
    c3 = c1 + 1
    c4 = 2 * math.pi / 3
    c5 = 2 * math.pi / 4.5
    start, end = 2 * x, 2 * x - 2
    endpoint = x == 0 or x == 1
    return [
        c3 * x**3 - c1 * x**2,
        1 + c3 * (x - 1)**3 + c1 * (x - 1)**2,
        start**2 * ((c2 + 1) * start - c2) / 2 if x < 0.5 else (end**2 * ((c2 + 1) * end + c2) + 2) / 2,
        x if endpoint else -(2**(10*x - 10)) * math.sin((10*x - 10.75) * c4),
        x if endpoint else 2**(-10*x) * math.sin((10*x - 0.75) * c4) + 1,
        x if endpoint else (-(2**(20*x - 10)) * math.sin((20*x - 11.125) * c5) / 2
                            if x < 0.5 else 2**(-20*x + 10) * math.sin((20*x - 11.125) * c5) / 2 + 1),
        1 - bounce(1 - x),
        bounce(x),
        (1 - bounce(1 - 2*x)) / 2 if x < 0.5 else (1 + bounce(2*x - 1)) / 2,
    ]


def cross(a, b):
    return [a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0]]


def transform(columns, point, position, scale, rotation):
    terrain = [sum(columns[c][r] * point[c] for c in range(4)) for r in range(4)]
    scaled = [terrain[i] * scale[i] for i in range(3)]
    inner = cross(rotation[:3], scaled)
    inner = [inner[i] + rotation[3]*scaled[i] for i in range(3)]
    outer = cross(rotation[:3], inner)
    return [position[i] + scaled[i] + 2*outer[i] for i in range(3)] + [terrain[3]]


def execute(source, count):
    """Compile a compute shader writing `count` floats to set 0 / binding 0 and return them."""
    if not VALIDATOR or not RUNNER:
        raise RuntimeError("GLSLANG_VALIDATOR and SHADER_MATH_RUNNER are required")
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)
        shader, binary = directory / "test.comp", directory / "test.spv"
        shader.write_text(source, encoding="utf-8")
        compile_result = subprocess.run(
            [VALIDATOR, "--target-env", "vulkan1.2", "-Os", "-o", str(binary), str(shader)],
            capture_output=True, text=True, encoding="utf-8",
        )
        if compile_result.returncode:
            raise RuntimeError(compile_result.stdout + compile_result.stderr)
        run = subprocess.run(
            [RUNNER, str(binary), str(count)], capture_output=True, text=True, encoding="utf-8",
        )
        if run.returncode == 77:
            raise unittest.SkipTest(run.stderr.strip())
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        values = [float(line) for line in run.stdout.splitlines()]
        if len(values) != count:
            raise RuntimeError(f"Expected {count} results, got {len(values)}")
        return values


class ShaderMathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.values = execute(expand_includes(shader_source()), RESULT_COUNT)

    def assert_close(self, actual, expected):
        self.assertTrue(math.isfinite(actual), f"non-finite result: {actual}")
        self.assertAlmostEqual(actual, expected, delta=2e-5)

    def test_matrix_transform(self):
        cases = [
            ([[1.5, 0, 0, 0], [0, 0.5, 0, 0], [0, 0, 2, 0], [7, 1, -4, 1]],
             [0.25, 2, -1, 1], [3, -2, 5], [2, 0.5, -1.5], [0, math.sqrt(0.5), 0, math.sqrt(0.5)]),
            ([[1, 0.25, 0, 0], [-0.2, 0.8, 0.3, 0], [0.1, 0, 1.4, 0], [2, -3, 4, 1]],
             [2, -1, 0.5, 1], [-1, 3, 2], [1.1, 2.2, 0.7], [0.5, 0, 0, math.sqrt(3)/2]),
        ]
        for index, case in enumerate(cases):
            expected = transform(*case)
            for component in range(4):
                with self.subTest(case=index, component=component):
                    self.assert_close(self.values[8*index + component], expected[component])
                    self.assert_close(self.values[8*index + 4 + component], expected[component])

    def test_easing_values_and_expression_arguments(self):
        samples = INPUTS + [x + 0.125 for x in EXPRESSION_INPUTS]
        for index, x in enumerate(samples):
            for function, expected in enumerate(easing(x)):
                with self.subTest(function=EASING_NAMES[function], x=x):
                    self.assert_close(self.values[16 + len(EASING_NAMES)*index + function], expected)

    def test_packed_vectors(self):
        for index, expected in enumerate((0.25, -0.25, 0.0, 1.0)):
            with self.subTest(component=index):
                self.assert_close(self.values[PACKING_OFFSET + index], expected)


class ExposureMathTests(unittest.TestCase):
    def test_histogram_average_and_adaptation(self):
        dark = [0] * 256
        dark[0] = 100
        bright_8k = [0] * 256
        bright_8k[255] = 7680 * 4320
        mixed = [0] * 256
        mixed[0], mixed[1], mixed[128], mixed[255] = 123, 700_000, 15_000_000, 17_477_477
        distributed = [(index * 7) % 17 for index in range(256)]

        for counts, previous, coefficient in (
            (dark, 2.0, 1.0),
            (bright_8k, 2.0, 1.0),
            (bright_8k, 2.0, 0.25),
            (mixed, 1.5, 0.5),
            (distributed, 0.25, 0.75),
        ):
            with self.subTest(pixels=sum(counts), previous=previous, coefficient=coefficient):
                source = section("luminanceCalc.glsl", "Compute.Average")
                replacements = {
                    "DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW, 12, r32f) uniform ACCESS_RW image2D s_target;": "",
                    "DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW, 13, std430) coherent ACCESS_RW buffer histogramBuffer\n{\n    uint histogram[];\n};": "",
                    "uint countForThisBin = histogram[gl_LocalInvocationIndex];": "uint countForThisBin = counts[gl_LocalInvocationIndex];",
                    "histogram[gl_LocalInvocationIndex] = 0;": "",
                    "imageLoad(s_target, ivec2(0, 0)).x": f"{previous:.9f}",
                    "imageStore(s_target, ivec2(0, 0), vec4(adaptedLum, 0.0, 0.0, 0.0));": "values[0] = adaptedLum;",
                }
                for old, new in replacements.items():
                    self.assertEqual(source.count(old), 1, old)
                    source = source.replace(old, new)
                shader = f"""
#version 460
#define GROUP_SIZE 256
#define dvd_minLogLum -8.0
#define dvd_logLumRange 12.0
#define dvd_timeCoeff {coefficient:.9f}
#define dvd_numPixels {sum(counts):.1f}
const uint counts[GROUP_SIZE] = uint[GROUP_SIZE]({", ".join(f"{count}u" for count in counts)});
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
{source}
"""
                actual = execute(shader, 1)[0]
                weighted = sum(index * count for index, count in enumerate(counts))
                average = weighted / max(sum(counts) - counts[0], 1) - 1
                target = 2 ** (average / 254 * 12 - 8)
                expected = previous + (target - previous) * coefficient
                self.assertTrue(math.isfinite(actual))
                self.assertAlmostEqual(actual, expected, delta=max(1e-5, abs(expected) * 2e-5))


class WorldGeometryMathTests(unittest.TestCase):
    def test_octahedral_normal_round_trip_including_negative_pole(self):
        utility = (SHADERS / "FragmentAtoms" / "utility.frag").read_text(encoding="utf-8")
        pack = re.search(r"^vec2 packNormal\(.*?^\}", utility, re.MULTILINE | re.DOTALL)
        unpack = re.search(r"^vec3 unpackNormal\(.*?^\}", utility, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(pack)
        self.assertIsNotNone(unpack)
        normals = [
            (1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1),
            (1, 0, -1), (0, -1, -1), (1, -2, -3), (-1, 2, -3),
        ]
        inputs = ", ".join(f"normalize(vec3({x}.0, {y}.0, {z}.0))" for x, y, z in normals)
        shader = f"""
#version 460
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
{pack.group(0)}
{unpack.group(0)}
void main() {{
    const vec3 inputs[{len(normals)}] = vec3[{len(normals)}]({inputs});
    for (int i = 0; i < {len(normals)}; ++i) {{
        const vec3 actual = unpackNormal(packNormal(inputs[i]));
        values[3 * i] = actual.x;
        values[3 * i + 1] = actual.y;
        values[3 * i + 2] = actual.z;
    }}
}}
"""
        actual = execute(shader, len(normals) * 3)
        for index, normal in enumerate(normals):
            length = math.sqrt(sum(component * component for component in normal))
            for component, value in enumerate(normal):
                with self.subTest(normal=normal, component=component):
                    self.assertAlmostEqual(actual[index * 3 + component], value / length, delta=2e-6)

    def test_positioned_and_multiple_water_volumes(self):
        water = (SHADERS / "Common" / "waterData.cmn").read_text(encoding="utf-8")
        shader = f"""
#version 460
#define GLOBAL_WATER_BODIES_COUNT 2
#define M_EPSILON 0.00001
#define Saturate(X) clamp((X), 0.0, 1.0)
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
struct WaterBodyData {{ vec4 extents; vec4 positionW; }};
WaterBodyData dvd_waterEntities[GLOBAL_WATER_BODIES_COUNT];
{water}
void main() {{
    dvd_waterEntities[0] = WaterBodyData(vec4(20, 10, 40, 0), vec4(100, 10, -30, 0));
    dvd_waterEntities[1] = WaterBodyData(vec4(8, 6, 12, 0), vec4(-50, 5, 60, 0));
    const vec3 positions[8] = vec3[8](
        vec3(100, 7, -30), vec3(110, 7, -10), vec3(110.01, 7, -30),
        vec3(100, 10, -30), vec3(100, 0, -30), vec3(-50, 4, 60),
        vec3(-46, 4, 66), vec3(0, -0.5, 0));
    for (int i = 0; i < 8; ++i)
        values[i] = IsUnderWater(positions[i]) ? 1.0 : 0.0;
    const vec2 first = GetWaterDetails(positions[0], -20.0);
    const vec2 second = GetWaterDetails(positions[5], -20.0);
    const vec2 outside = GetWaterDetails(positions[7], -20.0);
    values[8] = first.x; values[9] = first.y;
    values[10] = second.x; values[11] = second.y;
    values[12] = outside.x; values[13] = outside.y;
    dvd_waterEntities[1] = WaterBodyData(vec4(0), vec4(0));
    values[14] = IsUnderWater(vec3(0, -0.5, 0)) ? 1.0 : 0.0;
    values[15] = GetWaterDetails(vec3(0, -0.5, 0), -20.0).x;
    values[16] = GetWaterDetails(positions[0], 10.0).y;
}}
"""
        actual = execute(shader, 17)
        expected = (1, 1, 0, 0, 0, 1, 1, 0, 1, 1 - 3 / 30, 1, 1 - 1 / 25, 0, 1, 0, 0, 0)
        for index, value in enumerate(expected):
            with self.subTest(index=index):
                self.assertTrue(math.isfinite(actual[index]))
                self.assertAlmostEqual(actual[index], value, delta=2e-5)

    def test_small_hiz_command_lists_are_processed(self):
        source = section("HiZOcclusionCull.glsl", "Compute")
        source = source[source.index("void CullItem("):]
        source = re.sub(r"layout\(local_size_x = WORK_GROUP_SIZE\) in;", "", source)
        source = source.replace("void main()", "void cullMain()")
        source = source.replace("gl_GlobalInvocationID.x", "fakeID")
        shader = f"""
#version 460
#define WORK_GROUP_SIZE 64
#define atomicAdd(A, B) ((A) += uint(B))
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
struct Command {{ uint count, instanceCount, firstIndex, baseVertex, baseInstance; }};
struct NodeIndirectionData {{ uint _transformIdx; }};
struct NodeTransformData {{ vec4 sphere; }};
Command dvd_drawCommands[64];
NodeIndirectionData dvd_IndirectionData[64];
NodeTransformData dvd_Transforms[64];
uint dvd_numEntities, dvd_countCulledItems = 1u, fakeID;
uint culledCount[4];
bool dvd_CullNode(NodeTransformData node) {{ return true; }}
vec4 dvd_BoundingSphere(NodeTransformData node) {{ return node.sphere; }}
bool HiZCull(vec3 position, float radius) {{ return mod(position.x, 2.0) == 0.0; }}
{source}
void main() {{
    const uint counts[5] = uint[5](0u, 1u, 3u, 63u, 64u);
    for (int test = 0; test < 5; ++test) {{
        dvd_numEntities = counts[test];
        for (int i = 0; i < 4; ++i) culledCount[i] = 0u;
        for (uint i = 0u; i < 64u; ++i) {{
            dvd_drawCommands[i] = Command(1u, 1u, 0u, 0u, i + 1u);
            dvd_IndirectionData[i]._transformIdx = i;
            dvd_Transforms[i].sphere = vec4(float(i), 0, 0, 1);
        }}
        for (fakeID = 0u; fakeID < 64u; ++fakeID) cullMain();
        uint errors = 0u;
        for (uint i = 0u; i < 64u; ++i) {{
            const uint expected = i < dvd_numEntities && (i % 2u) == 0u ? 0u : 1u;
            if (dvd_drawCommands[i].instanceCount != expected) ++errors;
        }}
        values[3 * test] = float(culledCount[0]);
        values[3 * test + 1] = float(culledCount[1]);
        values[3 * test + 2] = float(errors);
    }}
}}
"""
        actual = execute(shader, 15)
        for index, count in enumerate((0, 1, 3, 63, 64)):
            with self.subTest(count=count):
                self.assertEqual(actual[index * 3:index * 3 + 3], [(count + 1) // 2, 64 - count, 0])


class MaterialLightingMathTests(unittest.TestCase):
    @staticmethod
    def shader_function(source, name):
        match = re.search(rf"^{re.escape(name)}\(.*?^\}}", source, re.MULTILINE | re.DOTALL)
        if match is None:
            raise ValueError(f"Shader function {name} not found")
        return match.group(0)

    def test_selected_alpha_is_shared_by_colour_and_depth_paths(self):
        source = (SHADERS / "FragmentAtoms" / "materialData.frag").read_text(encoding="utf-8")
        begin = source.index("#if defined(HAS_TRANSPARENCY)")
        end = source.index("#endif //HAS_TRANSPARENCY", begin) + len("#endif //HAS_TRANSPARENCY")
        alpha = source[begin:end]
        shader = f"""
#version 460
#define HAS_TRANSPARENCY
#define USE_UNIT0_TEXTURE
#define USE_OPACITY_TEXTURE
#define TEX_NONE 0u
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
struct NodeMaterialData {{ uint opacityOp; uint unitOp; bool opacityAlpha; bool albedoAlpha; vec4 base; }};
#define dvd_TexOpOpacity(X) ((X).opacityOp)
#define dvd_TexOpUnit0(X) ((X).unitOp)
#define dvd_UseOpacityAlphaChannel(X) ((X).opacityAlpha)
#define dvd_UseAlbedoTextureAlphaChannel(X) ((X).albedoAlpha)
#define dvd_BaseColour(X) ((X).base)
const int texDiffuse0 = 0, texOpacityMap = 1;
vec4 sampleTexture(int id, vec3 uv) {{
    return id == texOpacityMap ? vec4(0.2, 0.0, 0.0, 0.4) : vec4(0.0, 0.0, 0.0, 0.6);
}}
#define texture(S, UV) sampleTexture(S, UV)
vec4 getTextureColour(NodeMaterialData data, vec3 uv) {{ return vec4(0.1, 0.2, 0.3, 0.3); }}
{alpha}
void main() {{
    NodeMaterialData data = NodeMaterialData(1u, 1u, false, false, vec4(1, 1, 1, 0.8));
    for (int i = 0; i < 5; ++i) {{
        if (i == 1) data.opacityAlpha = true;
        if (i == 2) {{ data.opacityOp = 0u; data.albedoAlpha = true; }}
        if (i == 3) data.albedoAlpha = false;
        if (i == 4) {{ data.albedoAlpha = true; data.unitOp = 0u; }}
        values[2 * i] = getAlpha(data, vec3(0));
        values[2 * i + 1] = getAlbedo(data, vec3(0)).a;
    }}
}}
"""
        actual = execute(shader, 10)
        for index, expected in enumerate((0.2, 0.4, 0.6, 0.8, 0.8)):
            with self.subTest(case=index):
                self.assertAlmostEqual(actual[2 * index], expected, delta=1e-6)
                self.assertAlmostEqual(actual[2 * index + 1], expected, delta=1e-6)

    def test_phong_roughness_decreases_with_shininess(self):
        source = (SHADERS / "FragmentAtoms" / "materialData.frag").read_text(encoding="utf-8")
        function = self.shader_function(source, "float SpecularToRoughness")
        shader = f"""
#version 460
#define MAX_SHININESS 255.0
#define Saturate(X) clamp((X), 0.0, 1.0)
#define Luminance(RGB) max(dot((RGB), vec3(0.299, 0.587, 0.114)), 0.0001)
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
{function}
void main() {{
    const float powers[5] = float[5](-1.0, 0.0, 63.75, 255.0, 300.0);
    for (int i = 0; i < 5; ++i) {{
        values[i] = SpecularToRoughness(vec3(1), powers[i]);
        values[5 + i] = SpecularToRoughness(vec3(0.5), powers[i]);
        values[10 + i] = SpecularToRoughness(vec3(0), powers[i]);
    }}
}}
"""
        actual = execute(shader, 15)
        self.assertAlmostEqual(actual[0], 1.0, delta=1e-6)
        self.assertAlmostEqual(actual[1], 1.0, delta=1e-6)
        self.assertAlmostEqual(actual[3], 0.0, delta=1e-6)
        self.assertAlmostEqual(actual[4], 0.0, delta=1e-6)
        for offset in (0, 5, 10):
            samples = actual[offset:offset + 5]
            self.assertTrue(all(math.isfinite(x) and 0 <= x <= 1 for x in samples))
            self.assertTrue(all(a >= b for a, b in zip(samples, samples[1:])))
        self.assertGreater(actual[8], actual[3])
        self.assertGreater(actual[13], actual[8])

    def test_metal_roughness_direct_fresnel_uses_f0(self):
        source = (SHADERS / "FragmentAtoms" / "pbr.frag").read_text(encoding="utf-8")
        fresnel = self.shader_function(source, "vec3 SchlickGaussianFresnel")
        brdf = self.shader_function(source, "vec3 GetBRDF")
        shader = f"""
#version 460
#define SHADING_MODE_PBR_MR
#define M_EPSILON 0.00001
#define INV_M_PI 1.0
#define Squared(X) ((X) * (X))
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
struct PBRMaterial {{ vec3 _diffuseColour; vec3 _F0; vec4 _specular; float _roughness; float _occlusion; }};
float BurleyDiffuse(float a, float b, float c, float d, float e) {{ return 0.0; }}
float GGXDistribution(float a, float b) {{ return 1.0; }}
float NeumannVisibility(float a, float b) {{ return 1.0; }}
{fresnel}
{brdf}
void main() {{
    PBRMaterial material;
    material._diffuseColour = vec3(0);
    material._F0 = vec3(0.04, 0.3, 0.8);
    material._specular = vec4(0.0);
    material._roughness = 0.5;
    material._occlusion = 1.0;
    const vec3 result = GetBRDF(vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1),
                                vec3(1), 1.0, 1.0, 1.0, material);
    values[0] = result.x; values[1] = result.y; values[2] = result.z;
}}
"""
        actual = execute(shader, 3)
        gaussian = 2 ** (-5.55473 - 6.98316)
        for index, f0 in enumerate((0.04, 0.3, 0.8)):
            self.assertAlmostEqual(actual[index], f0 + (1 - f0) * gaussian, delta=1e-6)

    def test_environment_uses_world_normals_and_preserves_diffuse_weight(self):
        source = (SHADERS / "FragmentAtoms" / "IBL.frag").read_text(encoding="utf-8")
        functions = "\n".join(self.shader_function(source, name) for name in (
            "vec3 computeFresnelSchlickRoughness", "vec3 GetSpecularDominantDir",
            "vec3 GetCubeReflectionDirection", "vec3 getIBLAmbient",
            "vec4 getReflectionLookup", "vec3 ApplyIBL",
        ))
        shader = f"""
#version 460
#define Saturate(X) clamp((X), 0.0, 1.0)
#define SKY_LIGHT_LAYER_IDX 0u
#define REFLECTION_PROBE_MIP_COUNT 7.0
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
struct ProbeData {{ vec4 _positionW; vec4 _halfExtents; }};
struct PBRMaterial {{ vec3 _diffuseColour; vec3 _F0; float _roughness; float _metallic; float _occlusion; }};
ProbeData dvd_Probes[2];
mat4 dvd_InverseViewMatrix = mat4(vec4(0, 0, -1, 0), vec4(0, 1, 0, 0),
                                  vec4(1, 0, 0, 0), vec4(0, 0, 0, 1));
const vec4 dvd_AmbientColour = vec4(0);
const int texEnvIrradiance = 0, texEnvPrefiltered = 1, texBRDFLut = 2;
vec4 irradianceDirection, reflectionDirection;
float getWorldAO() {{ return 1.0; }}
vec4 sampleTexture(int id, vec4 direction) {{ irradianceDirection = direction; return vec4(1); }}
vec4 sampleTexture(int id, vec2 uv) {{ return vec4(0); }}
vec4 sampleTextureLod(int id, vec4 direction, float lod) {{ reflectionDirection = direction; return vec4(0); }}
#define texture(S, UV) sampleTexture(S, UV)
#define textureLod(S, UV, LOD) sampleTextureLod(S, UV, LOD)
{functions}
void main() {{
    PBRMaterial material;
    material._diffuseColour = vec3(0.48);
    material._F0 = vec3(0.04);
    material._roughness = 0.5; material._metallic = 0.5; material._occlusion = 1.0;
    const vec3 normal = vec3(0, 0, 1);
    for (int i = 0; i < 2; ++i) {{
        const vec3 view = i == 0 ? normal : normalize(vec3(1, 0, 1));
        const vec3 result = ApplyIBL(material, view, normal, 1.0, vec3(0), 0u);
        values[7 * i] = irradianceDirection.x;
        values[7 * i + 1] = irradianceDirection.y;
        values[7 * i + 2] = irradianceDirection.z;
        values[7 * i + 3] = reflectionDirection.x;
        values[7 * i + 4] = reflectionDirection.y;
        values[7 * i + 5] = reflectionDirection.z;
        values[7 * i + 6] = result.x;
    }}
    dvd_Probes[1] = ProbeData(vec4(0), vec4(1));
    const vec3 boundary = GetCubeReflectionDirection(normal, normal, vec3(0, 1, 0), 1u, 0.0);
    values[14] = boundary.x; values[15] = boundary.y; values[16] = boundary.z;
}}
"""
        actual = execute(shader, 17)
        root_half = math.sqrt(0.5)
        expected = (1, 0, 0, 1, 0, 0, 0.48 * 0.96,
                    1, 0, 0, root_half, 0, root_half, 0.48 * 0.96,
                    1, 1, 0)
        for index, value in enumerate(expected):
            with self.subTest(index=index):
                self.assertTrue(math.isfinite(actual[index]))
                self.assertAlmostEqual(actual[index], value, delta=2e-6)


class PostFXMathTests(unittest.TestCase):
    def test_signed_and_color_framebuffer_previews(self):
        source = section("fbPreview.glsl", "Fragment")
        source = source.replace('#include "utility.frag"', "")
        source = re.sub(r"^uniform (?:uint|float) \w+;\s*$", "", source, flags=re.MULTILINE)
        source = source.replace("DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D texDiffuse0;", "")
        source = source.replace("layout(location = 0) out vec4 _colourOut;", "vec4 _colourOut;")
        source = source.replace("void main()\n{", "void previewPixel()\n{")
        utility = (SHADERS / "FragmentAtoms" / "utility.frag").read_text(encoding="utf-8")
        normal = re.search(r"^vec3 unpackNormal\(.*?^\}", utility, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(normal)
        shader = f"""
#version 460
#define ToLinear(SRGB) pow((SRGB).rgb, vec3(2.2))
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
struct Varyings {{ vec2 _texCoord; }} VAR;
const ivec2 dvd_ScreenDimensions = ivec2(1024, 768);
const int texDiffuse0 = 0;
uint channelCount, startChannel = 0u, channelsArePacked, scaleAndBias, normalizeOutput, skipLinearisation;
float lodLevel = 0.0, multiplier;
vec4 sampleValue;
vec4 samplePreview(int samplerId, vec2 uv, float lod) {{ return sampleValue; }}
#define textureLod(samplerId, uv, lod) samplePreview(samplerId, uv, lod)
vec2 unpackVec2(float value) {{ return unpackHalf2x16(floatBitsToUint(value)); }}
{normal.group(0)}
{source}
void storeResult(int index) {{
    values[index * 4] = _colourOut.r;
    values[index * 4 + 1] = _colourOut.g;
    values[index * 4 + 2] = _colourOut.b;
    values[index * 4 + 3] = _colourOut.a;
}}
void main() {{
    channelCount = 2u; channelsArePacked = 1u; scaleAndBias = 1u;
    normalizeOutput = 0u; skipLinearisation = 0u; multiplier = 1.0;
    sampleValue = vec4(0.0, 0.5, 0.0, 0.0); previewPixel(); storeResult(0);
    channelsArePacked = 0u; multiplier = 5.0;
    sampleValue = vec4(-0.2, 0.1, 0.0, 0.0); previewPixel(); storeResult(1);
    normalizeOutput = 1u; multiplier = 1.0;
    sampleValue = vec4(0.0); previewPixel(); storeResult(2);
    channelCount = 4u; scaleAndBias = 0u; normalizeOutput = 0u;
    sampleValue = vec4(0.5, 0.25, 1.0, 0.7); previewPixel(); storeResult(3);
    channelCount = 1u; skipLinearisation = 1u;
    sampleValue = vec4(0.25); previewPixel(); storeResult(4);
}}
"""
        actual = execute(shader, 20)
        expected = (
            (0.0, 0.5, 0.5, 1.0),
            (0.0, 0.75, 0.5, 1.0),
            (0.5, 0.5, 0.5, 1.0),
            (0.5 ** 2.2, 0.25 ** 2.2, 1.0, 0.7),
            (0.25, 0.25, 0.25, 1.0),
        )
        for index, result in enumerate(expected):
            for component, value in enumerate(result):
                with self.subTest(case=index, component=component):
                    self.assertTrue(math.isfinite(actual[index * 4 + component]))
                    self.assertAlmostEqual(actual[index * 4 + component], value, delta=1e-5)

    def test_tonemap_black_and_all_mapping_functions(self):
        source = section("toneMap.glsl", "Fragment")
        source = source.replace('#include "utility.frag"', "")
        source = re.sub(r"^DESCRIPTOR_SET_RESOURCE\(PER_DRAW, \d+\) uniform sampler2D \w+;\s*$",
                        "", source, flags=re.MULTILINE)
        source = source.replace("layout(location = 0) out vec4 _colourOut;", "vec4 _colourOut;")
        source = source.replace("void main() {", "void toneMapPixel() {")
        shader = f"""
#version 460
#define Luminance(RGB) max(dot((RGB), vec3(0.299, 0.587, 0.114)), 0.0001)
#define REINHARD 0
#define REINHARD_MODIFIED 1
#define GT 2
#define ACES 3
#define UNREAL_ACES 4
#define AMD_ACES 5
#define GT_DIFF_PARAMETERS 6
#define UNCHARTED_2 7
#define NONE 8
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
struct Varyings {{ vec2 _texCoord; }} VAR;
const int texScreen = 0, texExposure = 1, texBloom = 2;
vec3 inputColor;
uint skipToneMapping = 0u, useAdaptiveExposure;
int mappingFunction;
float manualExposureFactor = 1.0, bloomStrength = 0.0;
vec4 sampleInput(int samplerId, vec2 uv) {{
    return samplerId == texScreen ? vec4(inputColor, 1.0) :
           samplerId == texExposure ? vec4(1.0) : vec4(0.0);
}}
#define texture(samplerId, uv) sampleInput(samplerId, uv)
{source}
float expectedCurve(int mode, float x) {{
    if (mode == REINHARD) return Reinhard(x);
    if (mode == REINHARD_MODIFIED) return Reinhard2(x);
    if (mode == GT) return Tonemap_Uchimura(x);
    if (mode == ACES) return Tonemap_ACES(x);
    if (mode == UNREAL_ACES) return Tonemap_Unreal(x);
    if (mode == AMD_ACES) return Tonemap_Lottes(x);
    if (mode == GT_DIFF_PARAMETERS) return Tonemap_Uchimura(x, 1.0, 1.7, 0.1, 0.0, 1.33, 0.0);
    if (mode == UNCHARTED_2) return Uncharted2(x);
    return x;
}}
void main() {{
    const vec3 colors[3] = vec3[3](vec3(0.0), vec3(1e-8, 2e-8, 3e-8), vec3(1.0, 0.5, 0.25));
    for (int c = 0; c < 3; ++c) {{
        inputColor = colors[c];
        for (int a = 0; a < 2; ++a) {{
            useAdaptiveExposure = uint(a);
            for (int mode = 0; mode <= NONE; ++mode) {{
                mappingFunction = mode;
                toneMapPixel();
                int base = ((c * 2 + a) * 9 + mode) * 5;
                values[base] = _colourOut.r;
                values[base + 1] = _colourOut.g;
                values[base + 2] = _colourOut.b;
                float lum = dot(inputColor, vec3(0.2126729, 0.7151522, 0.0721750));
                if (a != 0 && mode != NONE) lum /= 9.6001;
                values[base + 3] = expectedCurve(mode, lum);
                values[base + 4] = dot(_colourOut.rgb, vec3(0.2126729, 0.7151522, 0.0721750));
            }}
        }}
    }}
}}
"""
        actual = execute(shader, 270)
        for color in range(3):
            for adaptive in range(2):
                for mode in range(9):
                    base = ((color * 2 + adaptive) * 9 + mode) * 5
                    with self.subTest(color=color, adaptive=adaptive, mode=mode):
                        self.assertTrue(all(math.isfinite(value) for value in actual[base:base + 5]))
                        if color == 0:
                            self.assertEqual(actual[base:base + 3], [0.0, 0.0, 0.0])
                        self.assertAlmostEqual(actual[base + 4], actual[base + 3],
                                               delta=max(1e-6, abs(actual[base + 3]) * 2e-5))

    def test_projected_depth_uses_zero_to_one_range(self):
        utility = (SHADERS / "FragmentAtoms" / "utility.frag").read_text(encoding="utf-8")
        match = re.search(r"^float computeDepth\(.*?^\}", utility, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(match)
        shader = f"""
#version 460
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
{match.group(0)}
void main() {{
    values[0] = computeDepth(vec4(0, 0, 0.0, 1), mat4(1), vec2(0, 1));
    values[1] = computeDepth(vec4(0, 0, 0.4, 2), mat4(1), vec2(0, 1));
    values[2] = computeDepth(vec4(0, 0, 1.0, 1), mat4(1), vec2(0.2, 0.8));
    values[3] = computeDepth(vec4(0, 0, 0.25, 1), mat4(1), vec2(1, 0));
}}
"""
        for actual, expected in zip(execute(shader, 4), (0.0, 0.2, 0.8, 0.75)):
            self.assertAlmostEqual(actual, expected, delta=1e-6)

    def test_depth_of_field_kernel_offsets_and_vignette(self):
        source = section("DepthOfField.glsl", "Fragment")
        depth = re.search(r"^float bdepth\(.*?^\}", source, re.MULTILINE | re.DOTALL)
        vignette = re.search(r"^float vignette\(.*?^\}", source, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(depth)
        self.assertIsNotNone(vignette)
        shader = f"""
#version 460
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
vec2 texel = vec2(0.01, 0.02);
float dbsize = 1.25, vignin, vignout, fstop = 0.0, vignfade = 22.0;
const int texLinearDepth = 0;
int tap = 0;
vec4 sampleDepth(vec2 coord) {{
    values[2 * tap] = coord.x;
    values[2 * tap + 1] = coord.y;
    ++tap;
    return vec4(1.0);
}}
#define texture(samplerId, coord) sampleDepth(coord)
{depth.group(0)}
{vignette.group(0)}
void main() {{
    values[18] = bdepth(vec2(0.0));
    vignin = 0.1; vignout = 0.5;
    values[19] = vignette(vec2(0.5, 0.5));
    values[20] = vignette(vec2(0.8, 0.5));
    values[21] = vignette(vec2(1.1, 0.5));
    vignin = 0.5; vignout = 0.1;
    values[22] = vignette(vec2(0.8, 0.5));
    vignin = 0.3; vignout = 0.3;
    values[23] = vignette(vec2(0.7, 0.5));
    values[24] = vignette(vec2(0.9, 0.5));
}}
"""
        actual = execute(shader, 25)
        offsets = [(-1, -1), (0, -1), (1, -1), (-1, 0), (0, 0),
                   (1, 0), (-1, 1), (0, 1), (1, 1)]
        for index, (x, y) in enumerate(offsets):
            self.assertAlmostEqual(actual[index * 2], x * 0.0125, delta=1e-6)
            self.assertAlmostEqual(actual[index * 2 + 1], y * 0.025, delta=1e-6)
        for value, expected in zip(actual[18:], (1.0, 1.0, 0.5, 0.0, 0.5, 1.0, 0.0)):
            self.assertAlmostEqual(value, expected, delta=1e-6)

    def test_bloom_soft_knee_and_zero_knee(self):
        source = section("bloom.glsl", "Fragment.BloomDownscale")
        match = re.search(r"^vec3 quadratic_threshold\(.*?^\}", source, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(match)
        cases = [
            (0.0, 1.0, 0.1), (0.8, 1.0, 0.1), (0.9, 1.0, 0.1),
            (1.0, 1.0, 0.1), (1.1, 1.0, 0.1), (2.0, 1.0, 0.1),
            (1.0, 1.0, 0.0), (1.5, 1.0, 0.0), (1.0, 1.0, 1e-7),
        ]
        calls = "\n".join(
            f"values[{index}] = quadratic_threshold(vec3({brightness:.9f}, {brightness * 0.5:.9f}, "
            f"{brightness * 0.25:.9f}), {threshold:.9f}, "
            f"vec2({threshold - knee:.9f}, {2 * knee:.9f})).x;"
            for index, (brightness, threshold, knee) in enumerate(cases)
        )
        shader = f"""
#version 460
#define M_EPSILON 1e-5
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
{match.group(0)}
void main() {{ {calls} }}
"""
        actual = execute(shader, len(cases))
        for index, (brightness, threshold, knee) in enumerate(cases):
            with self.subTest(brightness=brightness, knee=knee):
                part = max(0.0, min(brightness - threshold + knee, 2 * knee))
                soft = 0.25 * part * part / knee if knee > 0 else 0
                expected = brightness * max(soft, brightness - threshold) / max(brightness, 1e-5)
                self.assertTrue(math.isfinite(actual[index]))
                self.assertAlmostEqual(actual[index], expected, delta=2e-6)

    def test_bloom_uses_source_view_texel_size(self):
        source = section("bloom.glsl", "Fragment.BloomDownscale")
        match = re.search(r"const vec2 invSrcResolution\s*=\s*[^;]+;", source)
        self.assertIsNotNone(match)
        shader = f"""
#version 460
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
#define textureSize(tex, lod) (tex)
void main() {{
    const ivec2 sizes[4] = ivec2[4](ivec2(1920,1080), ivec2(960,540),
                                    ivec2(479,269), ivec2(1,1));
    for (int i = 0; i < 4; ++i) {{
        ivec2 srcTexture = sizes[i];
        {match.group(0)}
        values[2 * i] = invSrcResolution.x;
        values[2 * i + 1] = invSrcResolution.y;
    }}
}}
"""
        actual = execute(shader, 8)
        for index, size in enumerate(((1920, 1080), (960, 540), (479, 269), (1, 1))):
            self.assertAlmostEqual(actual[2 * index], 1 / size[0], delta=1e-7)
            self.assertAlmostEqual(actual[2 * index + 1], 1 / size[1], delta=1e-7)

    def test_ssao_upsample_always_selects_a_valid_sample(self):
        source = section("SSAOPass.glsl", "Fragment.SSAOUpsample")
        match = re.search(r"^float nearest_depth_ao\(\).*?^\}", source, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(match)
        shader = f"""
#version 460
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
struct Varyings {{ vec2 _texCoord; }} VAR;
const int texDepthMap = 0, texSSAONormalsDepth = 1, texSSAONearest = 2;
float centerDepth;
float depths[4];
float occlusion[4];
vec4 sampleAt(int textureId, ivec2 offset) {{
    int index = offset.x * 2 + offset.y;
    return vec4(textureId == texDepthMap ? centerDepth :
                textureId == texSSAONormalsDepth ? depths[index] : occlusion[index]);
}}
#define textureOffset(tex, uv, offset) sampleAt(tex, offset)
#define texture(tex, uv) sampleAt(tex, ivec2(0))
{match.group(0)}
void main() {{
    occlusion[0] = 0.1; occlusion[1] = 0.2; occlusion[2] = 0.3; occlusion[3] = 0.4;
    centerDepth = 1.0;
    for (int i = 0; i < 4; ++i) depths[i] = 0.0;
    values[0] = nearest_depth_ao();
    depths[3] = 0.8;
    values[1] = nearest_depth_ao();
    depths[1] = 0.8;
    values[2] = nearest_depth_ao();
    centerDepth = 0.4;
    depths[0] = 0.9; depths[1] = 0.3; depths[2] = 0.41; depths[3] = 0.8;
    values[3] = nearest_depth_ao();
}}
"""
        actual = execute(shader, 4)
        for value, expected in zip(actual, (0.1, 0.4, 0.2, 0.3)):
            self.assertAlmostEqual(value, expected, delta=1e-6)

    def test_ssao_representative_depth_with_ties(self):
        source = section("SSAOPass.glsl", "Fragment.SSAODownsample")
        match = re.search(r"^int representativeDepthIndex\(.*?^\}", source, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(match)
        samples = [
            (0.5, 0.5, 0.5, 0.5),
            (0.0, 0.0, 1.0, 1.0),
            (0.0, 0.5, 0.5, 1.0),
            (0.1, 0.2, 0.3, 0.9),
            (0.9, 0.1, 0.2, 0.3),
            (0.25, 0.25, 0.25, 0.9),
            (0.0, 0.25, 0.5, 0.75),
        ]
        inputs = ", ".join("vec4(" + ", ".join(f"{value:.8f}" for value in case) + ")" for case in samples)
        shader = f"""
#version 460
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
{match.group(0)}
void main() {{
    const vec4 cases[{len(samples)}] = vec4[{len(samples)}]({inputs});
    for (int i = 0; i < {len(samples)}; ++i) values[i] = float(representativeDepthIndex(cases[i]));
}}
"""
        actual = execute(shader, len(samples))
        for index, case in enumerate(samples):
            with self.subTest(samples=case):
                center = sum(case) / 4
                rejected = max(range(4), key=lambda i: abs(case[i] - center))
                retained = [i for i in range(4) if i != rejected]
                retained_center = sum(case[i] for i in retained) / 3
                expected_distance = min(abs(case[i] - retained_center) for i in retained)
                chosen = int(actual[index])
                self.assertIn(chosen, retained)
                self.assertAlmostEqual(abs(case[chosen] - retained_center), expected_distance, delta=1e-6)

    def test_smaa_vertex_coordinates_and_quality_bounds(self):
        source = section("SMAA.glsl", "Vertex.Weight")
        source = source.replace('#include "nodeDataInput.cmn"', "")
        source = re.sub(r"layout\(location = ATTRIB_FREE_START \+ \d+\) out ", "", source)
        source = source.replace("void main(void)", "void calculateWeight()")
        source = source.replace("gl_Position", "testPosition")
        shader = f"""
#version 460
#define Mad(a, b, c) ((a) * (b) + (c))
struct Varyings {{ vec2 _texCoord; }} VAR;
vec4 dvd_ViewPort = vec4(0.0, 0.0, 1024.0, 768.0);
int dvd_VertexIndex;
int dvd_qualityMultiplier;
vec4 testPosition;
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
{source}
void main() {{
    const int qualities[4] = int[4](-5, 0, 3, 8);
    for (int q = 0; q < 4; ++q) {{
        dvd_qualityMultiplier = qualities[q];
        for (int v = 0; v < 3; ++v) {{
            dvd_VertexIndex = v;
            VAR._texCoord = vec2(123.0);
            calculateWeight();
            int base = (q * 3 + v) * 3;
            values[base] = vPixCoord.x;
            values[base + 1] = vPixCoord.y;
            values[base + 2] = vOffset[2].x;
        }}
    }}
}}
"""
        actual = execute(shader, 36)
        for quality_index, steps in enumerate((4, 4, 32, 32)):
            for vertex in range(3):
                base = (quality_index * 3 + vertex) * 3
                uv_x = 2 if vertex & 1 else 0
                uv_y = 2 if vertex & 2 else 0
                with self.subTest(quality=quality_index, vertex=vertex):
                    self.assertAlmostEqual(actual[base], uv_x * 1024, delta=1e-5)
                    self.assertAlmostEqual(actual[base + 1], uv_y * 768, delta=1e-5)
                    self.assertAlmostEqual(actual[base + 2], uv_x - (0.25 + 2 * steps) / 1024, delta=1e-6)


PERSPECTIVE = (60.0, 16.0 / 9.0, 0.5, 200.0)  # fov (degrees), aspect, near, far
PERSPECTIVE_DISTANCES = [0.5, 1.0, 7.3, 50.0, 199.0, 200.0]
ORTHO_PLANES = (1.0, 500.0)
ORTHO_DISTANCES = [1.0, 3.5, 120.0, 500.0]
# (outer cone angle in degrees, slant height)
SPOT_CASES = [(10.0, 12.0), (30.0, 5.0), (45.0, 8.0), (60.0, 4.0), (85.0, 20.0),
              (89.99, 10000.0), (90.0, 3.0), (120.0, 7.0), (180.0, 9.0)]
SPOT_POSITION = (1.0, 2.0, 3.0)
SPOT_DIRECTION = (0.0, 0.6, -1.6)  # deliberately not normalised
SPOT_INNER = math.cos(math.radians(15.0))
SPOT_OUTER = math.cos(math.radians(35.0))
SPOT_INTENSITY_CASES = [
    (1.0, SPOT_INNER, SPOT_OUTER),
    (SPOT_INNER, SPOT_INNER, SPOT_OUTER),
    (math.cos(math.radians(25.0)), SPOT_INNER, SPOT_OUTER),
    (SPOT_OUTER, SPOT_INNER, SPOT_OUTER),
    (math.cos(math.radians(40.0)), SPOT_INNER, SPOT_OUTER),
    (0.49, 0.5, 0.5),
    (0.5, 0.5, 0.5),
    (0.51, 0.5, 0.5),
    (-1.0, -1.0, -1.0),
]
CSM_SPLIT_COUNT = shadow_config()["MAX_CSM_SPLITS_PER_LIGHT"]
CSM_SLICE_CASES = [(0, 0, 0), (CSM_SPLIT_COUNT - 2, CSM_SPLIT_COUNT - 2, CSM_SPLIT_COUNT - 2),
                   (0, CSM_SPLIT_COUNT - 1, CSM_SPLIT_COUNT),
                   (CSM_SPLIT_COUNT, CSM_SPLIT_COUNT - 1, CSM_SPLIT_COUNT)]
VIEWPORT = (100.0, 50.0, 1600.0, 900.0)
# (window x, window y, view distance)
CLUSTER_FRAGMENTS = [
    (100.5, 50.5, 0.5),
    (1699.5, 949.5, 200.0),
    (20.0, 10.0, 0.25),
    (900.0, 500.0, 7.0),
    (1450.25, 120.75, 63.0),
]
DEPTH_OFFSET = 0
ORTHO_OFFSET = DEPTH_OFFSET + 3 * len(PERSPECTIVE_DISTANCES)
SPOT_OFFSET = ORTHO_OFFSET + 2 * len(ORTHO_DISTANCES)
SPOT_INTENSITY_OFFSET = SPOT_OFFSET + 4 * len(SPOT_CASES)
CSM_SLICE_OFFSET = SPOT_INTENSITY_OFFSET + len(SPOT_INTENSITY_CASES)
CLUSTER_OFFSET = CSM_SLICE_OFFSET + len(CSM_SLICE_CASES)
CLUSTER_RESULT_COUNT = CLUSTER_OFFSET + len(CLUSTER_FRAGMENTS)


def extract(pattern, source):
    match = re.search(pattern, source, re.DOTALL | re.MULTILINE)
    if not match:
        raise ValueError(f"Pattern not found: {pattern}")
    return match.group(0)


def cluster_grid():
    config = cluster_config()
    return config["CLUSTERS_X"], config["CLUSTERS_Y"], config["CLUSTERS_Z"]


def light_scale_bias():
    near, far = PERSPECTIVE[2], PERSPECTIVE[3]
    slices = cluster_grid()[2]
    ratio = math.log(far / near)
    return slices / ratio, -(slices * math.log(near) / ratio)


def cluster_source():
    utility = (SHADERS / "Common" / "utility.cmn").read_text(encoding="utf-8")
    light_input = (SHADERS / "Common" / "lightInput.cmn").read_text(encoding="utf-8")
    lighting_calc = (SHADERS / "FragmentAtoms" / "lightingCalc.frag").read_text(encoding="utf-8")
    shadow_utils = (SHADERS / "FragmentAtoms" / "shadowUtils.frag").read_text(encoding="utf-8")
    depth_macros = "\n".join(extract(rf"^#define {name}\([^\n]*$", utility) for name in ("ToLinearDepth", "ViewDistanceFromDepth"))
    light = extract(r"^#define LIGHT_DIRECTIONAL.*?^vec4 getPositionAndRangeForLight\(.*?^\}$", light_input)
    spot_intensity = extract(r"^#define SpotConeIntensity\([^\n]*$", lighting_calc)
    resolve_csm_slice = extract(r"^#define ResolveCSMSlice\([^\n]*$", shadow_utils)
    cluster = "\n".join(extract(rf"^uint {name}\(.*?^\}}$", light_input) for name in ("GetClusterZIndex", "GetClusterIndex"))
    clusters_x, clusters_y, clusters_z = cluster_grid()
    fov, aspect, near, far = PERSPECTIVE
    scale, bias = light_scale_bias()
    cluster_size = (math.ceil(VIEWPORT[2] / clusters_x), math.ceil(VIEWPORT[3] / clusters_y))

    def floats(values):
        return ", ".join(f"{value:.9f}f" for value in values)

    spot_cos = [math.cos(math.radians(angle)) for angle, _ in SPOT_CASES]
    spot_height = [height for _, height in SPOT_CASES]
    return f"""
#version 460
#define Squared(X) ((X) * (X))
#define Saturate(X) clamp((X), 0.f, 1.f)
#define PER_PASS 1
#define ACCESS_R readonly
#define CLUSTERS_X {clusters_x}
#define CLUSTERS_Y {clusters_y}
#define CLUSTERS_Z {clusters_z}
#define MAX_CSM_SPLITS_PER_LIGHT {CSM_SPLIT_COUNT}
#define DESCRIPTOR_SET_RESOURCE_LAYOUT(SET, BINDING, LAYOUT) layout(set = SET, binding = BINDING, LAYOUT)
{depth_macros}
{light}
{spot_intensity}
{resolve_csm_slice}

mat4 dvd_ProjectionMatrix;
const vec4 dvd_ViewPort = vec4({floats(VIEWPORT)});
const vec2 dvd_ClusterSizes = vec2({floats(cluster_size)});
const float dvd_LightScale = {scale:.9f}f;
const float dvd_LightBias = {bias:.9f}f;
{cluster}

layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};

// Camera::Perspective / Camera::Ortho with zeroToOneDepth = true (column-major, m[column][row])
mat4 Perspective(in float fovy, in float aspect, in float zNear, in float zFar) {{
    const float tanHalfFovy = tan(radians(fovy) * 0.5f);
    mat4 ret = mat4(0.f);
    ret[0][0] = 1.f / (aspect * tanHalfFovy);
    ret[1][1] = 1.f / tanHalfFovy;
    ret[2][3] = -1.f;
    ret[2][2] = zFar / (zNear - zFar);
    ret[3][2] = -(zFar * zNear) / (zFar - zNear);
    return ret;
}}

mat4 Ortho(in float zNear, in float zFar) {{
    mat4 ret = mat4(0.f);
    ret[0][0] = 1.f;
    ret[1][1] = 1.f;
    ret[3][3] = 1.f;
    ret[2][2] = -1.f / (zFar - zNear);
    ret[3][2] = -zNear / (zFar - zNear);
    return ret;
}}

float DepthAt(in mat4 projection, in float viewDistance) {{
    const vec4 clip = projection * vec4(0.f, 0.f, -viewDistance, 1.f);
    return clip.z / clip.w;
}}

void main() {{
    const mat4 perspective = Perspective({fov:.9f}f, {aspect:.9f}f, {near:.9f}f, {far:.9f}f);
    const float perspectiveDistances[{len(PERSPECTIVE_DISTANCES)}] = float[]({floats(PERSPECTIVE_DISTANCES)});
    for (int i = 0; i < {len(PERSPECTIVE_DISTANCES)}; ++i) {{
        const float depth = DepthAt(perspective, perspectiveDistances[i]);
        values[{DEPTH_OFFSET} + 3 * i + 0] = depth;
        values[{DEPTH_OFFSET} + 3 * i + 1] = ViewDistanceFromDepth(depth, perspective);
        values[{DEPTH_OFFSET} + 3 * i + 2] = ToLinearDepth(depth, vec2({near:.9f}f, {far:.9f}f));
    }}

    const mat4 ortho = Ortho({ORTHO_PLANES[0]:.9f}f, {ORTHO_PLANES[1]:.9f}f);
    const float orthoDistances[{len(ORTHO_DISTANCES)}] = float[]({floats(ORTHO_DISTANCES)});
    for (int i = 0; i < {len(ORTHO_DISTANCES)}; ++i) {{
        const float depth = DepthAt(ortho, orthoDistances[i]);
        values[{ORTHO_OFFSET} + 2 * i + 0] = depth;
        values[{ORTHO_OFFSET} + 2 * i + 1] = ViewDistanceFromDepth(depth, ortho);
    }}

    const float spotCos[{len(SPOT_CASES)}] = float[]({floats(spot_cos)});
    const float spotHeight[{len(SPOT_CASES)}] = float[]({floats(spot_height)});
    for (int i = 0; i < {len(SPOT_CASES)}; ++i) {{
        Light source;
        source._colour = vec4(1.f, 1.f, 1.f, spotCos[i]);
        source._positionWV = vec4({floats(SPOT_POSITION)}, 100.f);
        source._directionWV = vec4({floats(SPOT_DIRECTION)}, 1.f);
        source._options = ivec4(LIGHT_SPOT, -1, floatBitsToInt(spotHeight[i]), 0);
        const vec4 bounds = getPositionAndRangeForLight(source);
        for (int c = 0; c < 4; ++c) {{
            values[{SPOT_OFFSET} + 4 * i + c] = bounds[c];
        }}
        const vec3 spotIntensityCases[{len(SPOT_INTENSITY_CASES)}] = vec3[](
            {", ".join(f"vec3({floats(case)})" for case in SPOT_INTENSITY_CASES)});
        for (int i = 0; i < {len(SPOT_INTENSITY_CASES)}; ++i) {{
            const vec3 sampleValue = spotIntensityCases[i];
            values[{SPOT_INTENSITY_OFFSET} + i] = SpotConeIntensity(sampleValue.x, sampleValue.y, sampleValue.z);
        }}
        const vec4 splitProperties[MAX_CSM_SPLITS_PER_LIGHT] = vec4[](
            {", ".join(f"vec4(0.f, 0.f, 0.f, {10 * (i + 1)}.f)" if i < CSM_SPLIT_COUNT - 1 else "vec4(0.f, 0.f, 0.f, -1.f)" for i in range(CSM_SPLIT_COUNT))});
        const ivec2 splitCases[{len(CSM_SLICE_CASES)}] = ivec2[](
            {", ".join(f"ivec2({split}, {quad})" for split, quad, _ in CSM_SLICE_CASES)});
        for (int i = 0; i < {len(CSM_SLICE_CASES)}; ++i) {{
            values[{CSM_SLICE_OFFSET} + i] = float(ResolveCSMSlice(splitProperties, splitCases[i].x, splitCases[i].y));
        }}
    }}

    dvd_ProjectionMatrix = perspective;
    const vec3 fragments[{len(CLUSTER_FRAGMENTS)}] = vec3[]({", ".join(f"vec3({floats(fragment)})" for fragment in CLUSTER_FRAGMENTS)});
    for (int i = 0; i < {len(CLUSTER_FRAGMENTS)}; ++i) {{
        const vec4 fragCoord = vec4(fragments[i].xy, clamp(DepthAt(perspective, fragments[i].z), 0.f, 1.f), 1.f);
        values[{CLUSTER_OFFSET} + i] = float(GetClusterIndex(fragCoord));
    }}
}}
"""


def normalised(vector):
    length = math.sqrt(sum(component * component for component in vector))
    return [component / length for component in vector]


def perpendicular(direction):
    helper = (1.0, 0.0, 0.0) if abs(direction[0]) < 0.9 else (0.0, 1.0, 0.0)
    return normalised(cross(direction, helper))


class ClusterMathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.values = execute(cluster_source(), CLUSTER_RESULT_COUNT)

    def assert_relative(self, actual, expected, tolerance):
        self.assertTrue(math.isfinite(actual), f"non-finite result: {actual}")
        self.assertLessEqual(abs(actual - expected), tolerance * max(1.0, abs(expected)), f"{actual} != {expected}")

    def test_perspective_depth_reconstruction(self):
        near, far = PERSPECTIVE[2], PERSPECTIVE[3]
        for index, distance in enumerate(PERSPECTIVE_DISTANCES):
            depth, view_distance, linear = self.values[DEPTH_OFFSET + 3 * index:DEPTH_OFFSET + 3 * index + 3]
            with self.subTest(distance=distance):
                self.assert_relative(depth, far * (distance - near) / ((far - near) * distance), 1e-5)
                self.assert_relative(view_distance, distance, 2e-3)
                self.assert_relative(linear, distance, 2e-3)

    def test_orthographic_depth_reconstruction(self):
        near, far = ORTHO_PLANES
        for index, distance in enumerate(ORTHO_DISTANCES):
            depth, view_distance = self.values[ORTHO_OFFSET + 2 * index:ORTHO_OFFSET + 2 * index + 2]
            with self.subTest(distance=distance):
                self.assert_relative(depth, (distance - near) / (far - near), 1e-5)
                self.assert_relative(view_distance, distance, 1e-4)

    def test_spot_bounding_sphere_encloses_cone(self):
        direction = normalised(SPOT_DIRECTION)
        side = perpendicular(direction)
        up = cross(direction, side)
        for index, (angle, height) in enumerate(SPOT_CASES):
            centre = self.values[SPOT_OFFSET + 4 * index:SPOT_OFFSET + 4 * index + 3]
            radius = self.values[SPOT_OFFSET + 4 * index + 3]
            theta = math.radians(angle)
            points = [list(SPOT_POSITION), [SPOT_POSITION[i] + height * direction[i] for i in range(3)]]
            for step in range(8):
                phi = 2.0 * math.pi * step / 8.0
                offset = [math.cos(phi) * side[i] + math.sin(phi) * up[i] for i in range(3)]
                points.append([SPOT_POSITION[i] + height * (math.cos(theta) * direction[i] + math.sin(theta) * offset[i])
                               for i in range(3)])
            with self.subTest(angle=angle):
                cosine = math.cos(theta)
                expected_offset = (height / (2.0 * cosine) if cosine >= math.sqrt(0.5) else
                                   height * cosine if cosine > 0.0 else 0.0)
                for component in range(3):
                    self.assert_relative(centre[component], SPOT_POSITION[component] + direction[component] * expected_offset, 2e-5)
                for point in points:
                    distance = math.dist(point, centre)
                    self.assertLessEqual(distance, radius + 1e-4 * height)
                # Tight: never larger than the trivial sphere around the apex, and minimal for the analytic cases.
                self.assertLessEqual(radius, height * (1.0 + 1e-5))
                if 0.0 < math.cos(theta) < math.sqrt(0.5) - 1e-6:
                    self.assert_relative(radius, height * math.sin(theta), 1e-5)
                elif math.cos(theta) >= math.sqrt(0.5) - 1e-6:
                    self.assert_relative(radius, height / (2.0 * math.cos(theta)), 1e-5)

    def test_spot_intensity_and_hard_cones(self):
        for index, (theta, inner, outer) in enumerate(SPOT_INTENSITY_CASES):
            expected = max(0.0, min(1.0, (theta - outer) / (inner - outer))) if inner > outer else float(theta >= outer)
            with self.subTest(theta=theta, inner=inner, outer=outer):
                self.assert_relative(self.values[SPOT_INTENSITY_OFFSET + index], expected, 2e-5)

    def test_csm_slice_fallback(self):
        for index, (_, _, expected) in enumerate(CSM_SLICE_CASES):
            with self.subTest(case=CSM_SLICE_CASES[index]):
                self.assertEqual(self.values[CSM_SLICE_OFFSET + index], expected)

    def test_cluster_index(self):
        clusters_x, clusters_y, clusters_z = cluster_grid()
        scale, bias = light_scale_bias()
        cluster_size = (math.ceil(VIEWPORT[2] / clusters_x), math.ceil(VIEWPORT[3] / clusters_y))
        near = PERSPECTIVE[2]
        for index, (x, y, distance) in enumerate(CLUSTER_FRAGMENTS):
            tile_x = min(max(math.floor((x - VIEWPORT[0]) / cluster_size[0]), 0), clusters_x - 1)
            tile_y = min(max(math.floor((y - VIEWPORT[1]) / cluster_size[1]), 0), clusters_y - 1)
            # Depth is clamped to the near plane, as gl_FragCoord.z would be.
            slice_z = min(int(max(math.log(max(distance, near)) * scale + bias, 0.0)), clusters_z - 1)
            expected = clusters_x * clusters_y * slice_z + clusters_x * tile_y + tile_x
            with self.subTest(fragment=(x, y, distance)):
                self.assertEqual(int(self.values[CLUSTER_OFFSET + index]), expected)
        self.assertEqual(int(self.values[CLUSTER_OFFSET]), 0)
        self.assertEqual(int(self.values[CLUSTER_OFFSET + 1]), clusters_x * clusters_y * clusters_z - 1)


class VegetationInstanceIndexTests(unittest.TestCase):
    def test_draw_local_instance_index_for_both_backends(self):
        cases = [(0, 0), (0, 1), (7, 0), (7, 1), (123, 5),
                 (0x7FFFFFFF, 2), (0xFFFFFFFE, 5)]
        atom = (SHADERS / "Common" / "vegetationData.cmn").read_text(encoding="utf-8")
        chunk_uniform = "uniform uint dvd_terrainChunkOffset = 0u;"
        self.assertEqual(atom.count(chunk_uniform), 1)
        atom = atom.replace(chunk_uniform, "")
        bases = ", ".join(f"{base}u" for base, _ in cases)
        indices = ", ".join(f"{index}u" for _, index in cases)

        for backend in ("OPENGL", "VULKAN"):
            with self.subTest(backend=backend):
                instance_index = "base + local" if backend == "VULKAN" else "local"
                source = f"""
#version 460
#define TARGET_{backend}
#define dvd_InstanceIndex testInstance
#define DVD_GL_BASE_INSTANCE testBase
{atom}
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
void main() {{
    const uint bases[{len(cases)}] = uint[]({bases});
    const uint indices[{len(cases)}] = uint[]({indices});
    for (int i = 0; i < {len(cases)}; ++i) {{
        const uint base = bases[i];
        const uint local = indices[i];
        const int testInstance = int({instance_index});
        const int testBase = int(base);
        values[i] = float(VEGETATION_INSTANCE_INDEX);
    }}
}}
"""
                actual = execute(source, len(cases))
                self.assertEqual(actual, [float(index) for _, index in cases])


class HiZQueryTests(unittest.TestCase):
    def test_conservative_queries_use_the_selected_mip(self):
        size = 16
        depth = [[0.2 for _ in range(size)] for _ in range(size)]
        depth[9][8] = 1.0
        base = [[max(depth[sy][sx]
                     for sy in range(max(y - 1, 0), min(y + 2, size))
                     for sx in range(max(x - 1, 0), min(x + 2, size)))
                 for x in range(size)] for y in range(size)]
        levels = [base]
        while len(levels[-1]) > 1:
            previous = levels[-1]
            dimension = len(previous) // 2
            levels.append([[max(previous[y * 2 + dy][x * 2 + dx]
                                for dy in range(2) for dx in range(2))
                            for x in range(dimension)] for y in range(dimension)])

        # (screen centre, radius, expected cull result, expected mip)
        cases = [
            (0.5, 0.03125, False, 0), (0.5, 0.0625, False, 0), (0.5, 0.125, False, 1),
            (0.5, 0.25, False, 2), (0.5, 0.375, False, 3),
            (0.5, 1.0, False, 4), (0.0625, 0.03125, True, 0),
            (0.0625, 0.0625, True, 0), (0.0625, 0.125, True, 1),
            (0.0625, 0.25, True, 2), (0.125, 0.375, True, 3),
        ]
        # At mip 0 the four corners miss the interior far depth in the mip-2 case.
        self.assertTrue(all(base[y][x] == 0.2 for x in (6, 10) for y in (6, 10)))

        atom = (SHADERS / "Common" / "HiZCullingAlgorithm.cmn").read_text(encoding="utf-8")
        replacements = {
            "DESCRIPTOR_SET_RESOURCE(PER_DRAW, 0) uniform sampler2D HiZBuffer;": "",
            "uniform vec2 dvd_viewSize;": "const vec2 dvd_viewSize = vec2(16.f);",
            "uniform vec4 dvd_frustumPlanes[6];":
                "const vec4 dvd_frustumPlanes[6] = vec4[6](vec4(0.f), vec4(0.f), vec4(0.f), "
                "vec4(0.f), vec4(0.f), vec4(0.f));",
        }
        for original, replacement in replacements.items():
            self.assertEqual(atom.count(original), 1)
            atom = atom.replace(original, replacement)

        offsets = []
        flattened = []
        for level in levels:
            offsets.append(len(flattened))
            flattened.extend(value for row in level for value in row)
        depth_values = ", ".join(f"{value:.1f}f" for value in flattened)
        offset_values = ", ".join(str(offset) for offset in offsets)
        inputs = ", ".join(f"vec2({centre:.8f}f, {radius:.8f}f)" for centre, radius, _, _ in cases)
        source = f"""
#version 460
#define M_EPSILON 1e-5f
#define PushData0 mat4(1.f)
#define PushData1 mat4(1.f)
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Results {{ float values[]; }};
const float depthValues[{len(flattened)}] = float[{len(flattened)}]({depth_values});
const int offsets[{len(offsets)}] = int[{len(offsets)}]({offset_values});
int observedMip = -1;
vec4 sampleDepth(in vec2 uv, in int requestedMip) {{
    const int mip = clamp(requestedMip, 0, {len(levels) - 1});
    observedMip = mip;
    const int dimension = max({size} >> mip, 1);
    const ivec2 texel = clamp(ivec2(floor(uv * float(dimension))), ivec2(0), ivec2(dimension - 1));
    return vec4(depthValues[offsets[mip] + texel.y * dimension + texel.x]);
}}
#define textureLod(tex, coord, lod) sampleDepth((coord), int(lod))
{atom}
void main() {{
    const vec2 cases[{len(cases)}] = vec2[{len(cases)}]({inputs});
    for (int i = 0; i < {len(cases)}; ++i) {{
        const vec2 sampleCase = cases[i];
        const float ndcCentre = sampleCase.x * 2.f - 1.f;
        observedMip = -1;
        values[2 * i] = HiZCull(vec3(ndcCentre, ndcCentre, 0.85f), sampleCase.y) ? 1.f : 0.f;
        values[2 * i + 1] = float(observedMip);
    }}
}}
"""
        actual = execute(source, len(cases) * 2)
        for index, (_, _, culled, mip) in enumerate(cases):
            with self.subTest(case=cases[index]):
                self.assertEqual(actual[2 * index:2 * index + 2], [float(culled), float(mip)])


if __name__ == "__main__":
    result = unittest.main(exit=False).result
    if result.wasSuccessful() and result.testsRun == 0 and result.skipped:
        raise SystemExit(77)
    raise SystemExit(0 if result.wasSuccessful() else 1)
