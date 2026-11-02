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


class PostFXMathTests(unittest.TestCase):
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
