# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Execute the affected shader macros and compare them with CPU references."""

import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from ShaderCompilationTests import expand_includes


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


class ShaderMathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not VALIDATOR or not RUNNER:
            raise RuntimeError("GLSLANG_VALIDATOR and SHADER_MATH_RUNNER are required")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            shader, binary = directory / "test.comp", directory / "test.spv"
            shader.write_text(expand_includes(shader_source()), encoding="utf-8")
            compile_result = subprocess.run(
                [VALIDATOR, "--target-env", "vulkan1.2", "-Os", "-o", str(binary), str(shader)],
                capture_output=True, text=True, encoding="utf-8",
            )
            if compile_result.returncode:
                raise RuntimeError(compile_result.stdout + compile_result.stderr)
            run = subprocess.run(
                [RUNNER, str(binary), str(RESULT_COUNT)], capture_output=True, text=True, encoding="utf-8",
            )
            if run.returncode == 77:
                raise unittest.SkipTest(run.stderr.strip())
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            cls.values = [float(line) for line in run.stdout.splitlines()]
            if len(cls.values) != RESULT_COUNT:
                raise RuntimeError(f"Expected {RESULT_COUNT} results, got {len(cls.values)}")

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


if __name__ == "__main__":
    result = unittest.main(exit=False).result
    if result.wasSuccessful() and result.testsRun == 0 and result.skipped:
        raise SystemExit(77)
    raise SystemExit(0 if result.wasSuccessful() else 1)
