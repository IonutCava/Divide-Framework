# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Compile affected GLSW sections with representative engine-generated headers."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

if __package__:
    from .ExposureBufferTests import ExposureBufferTests
else:
    from ExposureBufferTests import ExposureBufferTests


ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "Assets" / "Shaders" / "GLSL"
VALIDATOR = os.environ.get("GLSLANG_VALIDATOR") or shutil.which("glslangValidator")
SPIRV_VALIDATOR = os.environ.get("SPIRV_VALIDATOR") or shutil.which("spirv-val")
ATOM_DIRECTORIES = {
    ".cmn": "Common",
    ".frag": "FragmentAtoms",
    ".vert": "VertexAtoms",
    ".comp": "ComputeAtoms",
}


def section(filename, key):
    source = (SHADERS / filename).read_text(encoding="utf-8")
    entries = list(re.finditer(r"^--\s*([A-Za-z0-9_.]+).*$", source, re.MULTILINE))
    matches = [
        (entry, entries[index + 1].start() if index + 1 < len(entries) else len(source))
        for index, entry in enumerate(entries)
        if key == entry.group(1) or key.startswith(entry.group(1) + ".")
    ]
    if not matches:
        raise ValueError(f"No GLSW section for {filename}:{key}")
    entry, end = max(matches, key=lambda match: len(match[0].group(1)))
    return source[entry.end():end]


def expand_includes(source):
    def replace(match):
        name = match.group(1)
        directory = ATOM_DIRECTORIES[Path(name).suffix]
        return expand_includes((SHADERS / directory / name).read_text(encoding="utf-8"))

    return re.sub(r'^\s*#\s*include\s+"([^"]+)"\s*$', replace, source, flags=re.MULTILINE)


def opengl_bindings():
    source = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
    registrations = re.findall(
        r"RegisterSetLayoutBinding\(\s*DescriptorSetUsage::(PER_\w+),\s*(\d+),\s*"
        r"DescriptorSetBindingType::(\w+),\s*ShaderStageVisibility::(\w+)\s*\)",
        source,
    )
    if not registrations:
        raise ValueError("No descriptor layout registrations found")
    counters = {"texture": 0, "image": 0, "buffer": 0, "compute_buffer": 0}
    bindings = {}
    for usage, slot, kind, visibility in registrations:
        slot = int(slot)
        if usage == "PER_BATCH" and slot == 0:
            bindings[(usage, slot)] = 239
            continue
        if kind == "COMBINED_IMAGE_SAMPLER":
            counter = "texture"
        elif kind == "IMAGE":
            counter = "image"
        elif kind.startswith(("SHADER_STORAGE_BUFFER_", "UNIFORM_BUFFER_")):
            counter = "compute_buffer" if visibility == "COMPUTE" else "buffer"
        else:
            raise ValueError(f"Unknown descriptor type: {kind}")
        bindings[(usage, slot)] = counters[counter]
        counters[counter] += 1
    return bindings


def descriptor_set_usages():
    source = (ROOT / "Source" / "Platform" / "Video" / "Headers" / "RenderAPIEnums.h").read_text(encoding="utf-8")
    body = re.search(r"enum class DescriptorSetUsage\s*:\s*\w+\s*\{([^}]*)\}", source)
    if not body:
        raise ValueError("DescriptorSetUsage enum not found")
    names = re.findall(r"\b(PER_\w+)\b", body.group(1))
    return {name: index for index, name in enumerate(names)}


def cluster_config():
    source = (ROOT / "Source" / "EngineConfig.h").read_text(encoding="utf-8")
    values = {name: int(value) for name, value in re.findall(r"constexpr U8 (CLUSTERS_[XYZ]) = (\d+)u;", source)}
    divisors = dict(re.findall(r"constexpr U8 (CLUSTERS_[XYZ])_THREADS = CLUSTERS_[XYZ](?: / (\d+)u)?;", source))
    if set(values) != {"CLUSTERS_X", "CLUSTERS_Y", "CLUSTERS_Z"} or set(divisors) != set(values):
        raise ValueError("Cluster grid configuration not found")
    values.update({f"{name}_THREADS": values[name] // int(divisor or 1) for name, divisor in divisors.items()})
    return values


def max_lights_per_cluster():
    source = (ROOT / "Source" / "EngineConfig.h").read_text(encoding="utf-8")
    match = re.search(r"constexpr U16 MAX_LIGHTS_PER_CLUSTER = (\d+)u;", source)
    if not match:
        raise ValueError("Maximum cluster light budget not found")
    return int(match.group(1))


def shadow_config():
    source = (ROOT / "Source" / "EngineConfig.h").read_text(encoding="utf-8")
    names = (
        "MAX_CSM_SPLITS_PER_LIGHT", "MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS",
        "MAX_SHADOW_CASTING_POINT_LIGHTS", "MAX_SHADOW_CASTING_SPOT_LIGHTS",
    )
    values = {name: int(value) for name, value in re.findall(r"constexpr U8 (\w+) = (\d+)u;", source) if name in names}
    if set(values) != set(names):
        raise ValueError("Shadow configuration not found")
    return values


def engine_header(stage, backend, defines, cluster_light_limit=64):
    debug_source = (SHADERS / "FragmentAtoms" / "debug.frag").read_text(encoding="utf-8")
    debug_names = sorted(set(re.findall(r"\bcase\s+(DEBUG_[A-Z_]+)", debug_source)))
    texture_source = (SHADERS / "FragmentAtoms" / "materialData.frag").read_text(encoding="utf-8")
    texture_names = sorted(set(re.findall(r"\bTEX_[A-Z_]+", texture_source)))

    stage_define = {"tesc": "TESS_CTRL", "tese": "TESS_EVAL", "geom": "GEOM", "comp": "COMPUTE"}.get(stage, stage.upper())
    compute_access = ["#define COMP_ONLY_W ACCESS_W", "#define COMP_ONLY_R ACCESS_R", "#define COMP_ONLY_RW ACCESS_RW"]
    graphics_access = ["#define COMP_ONLY_W readonly", "#define COMP_ONLY_R", "#define COMP_ONLY_RW readonly"]
    shadows = shadow_config()
    header = [
        "#version 460",
        "#extension GL_ARB_shader_draw_parameters : require",
        f"#define TARGET_{backend.upper()}",
        f"#define {stage_define}_SHADER",
        "#define float2 vec2",
        "#define float3 vec3",
        "#define float4 vec4",
        "#define lerp mix",
        "#define ACCESS_R readonly",
        "#define ACCESS_W writeonly",
        "#define ACCESS_RW",
        *(compute_access if stage == "comp" else graphics_access),
        "#define M_PI 3.14159265358979323846",
        "#define M_PI_DIV_2 1.57079632679489661923",
        "#define INV_M_PI 0.31830988618379067153",
        "#define TWO_M_PI 6.28318530717958647692",
        "#define M_EPSILON 1e-5f",
        "#define Squared(X) (X * X)",
        "#define Saturate(X) clamp(X, 0, 1)",
        "#define Mad(a, b, c) (a * b + c)",
        "#define MAX_CLIP_PLANES 4",
        "#define MAX_CULL_DISTANCES 1",
        "#define GLOBAL_WATER_BODIES_COUNT 1",
        "#define GLOBAL_PROBE_COUNT 2",
        "#define MAX_SHININESS 255.f",
        f"#define MAX_CSM_SPLITS_PER_LIGHT {shadows['MAX_CSM_SPLITS_PER_LIGHT']}",
        f"#define MAX_SHADOW_CASTING_POINT_LIGHTS {shadows['MAX_SHADOW_CASTING_POINT_LIGHTS']}",
        f"#define MAX_SHADOW_CASTING_SPOT_LIGHTS {shadows['MAX_SHADOW_CASTING_SPOT_LIGHTS']}",
        f"#define MAX_SHADOW_CASTING_DIR_LIGHTS {shadows['MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS']}",
        f"#define MAX_LIGHTS_PER_CLUSTER {cluster_light_limit}",
        *(f"#define {name} {value}" for name, value in cluster_config().items()),
        "#define SKY_LIGHT_LAYER_IDX 0",
        f"#define WORLD_AO_LAYER_INDEX {shadows['MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS'] * shadows['MAX_CSM_SPLITS_PER_LIGHT']}",
        "#define REFLECTION_PROBE_RESOLUTION 128",
        "#define REFLECTION_PROBE_MIP_COUNT 7",
        "#define SHADING_COUNT 6",
        "#define ALPHA_DISCARD_THRESHOLD 0.5f",
        "#define TARGET_ALBEDO 0",
        "#define TARGET_VELOCITY 1",
        "#define TARGET_NORMALS 2",
        "#define TARGET_ACCUMULATION 0",
        "#define TARGET_REVEALAGE 1",
        "#define TARGET_MODULATE 3",
        "#define ATTRIB_POSITION 0",
        "#define ATTRIB_TEXCOORD 1",
        "#define ATTRIB_NORMAL 2",
        "#define ATTRIB_TANGENT 3",
        "#define ATTRIB_COLOR 4",
        "#define ATTRIB_BONE_WEIGHT 5",
        "#define ATTRIB_BONE_INDICE 6",
        "#define ATTRIB_FREE_START 12",
        "#define TESSELLATION_OUTPUT_VERTICES 4",
        "#define WORLD_X_AXIS vec3(1.f, 0.f, 0.f)",
        "#define WORLD_Y_AXIS vec3(0.f, 1.f, 0.f)",
        "#define WORLD_Z_AXIS vec3(0.f, 0.f, 1.f)",
        "#define WORLD_SCALE_X 1.f",
        "#define WORLD_SCALE_Y 1.f",
        "#define WORLD_SCALE_Z 1.f",
        "#define TERRAIN_HEIGHT_OFFSET 0.f",
        "#define INV_CONTROL_VTX_PER_TILE_EDGE 0.25f",
        "#define CONTROL_VTX_PER_TILE_EDGE 4",
        "#define PATCHES_PER_TILE_EDGE 4",
        "#define MAX_TEXTURE_LAYERS 1",
        "#define TEXTURE_TILE_SIZE 1",
        "#define UNDERWATER_TILE_SCALE 1",
        "#define CURRENT_TILE_FACTORS float[4](1.f, 1.f, 1.f, 1.f)",
    ]
    header.extend(f"#define {name} {index}" for index, name in enumerate(debug_names))
    header.extend(f"#define {name} {index}" for index, name in enumerate(texture_names))
    header.extend(f"#define {define}" for define in defines)
    header.extend([
        "#if defined(PRE_PASS) || defined(SHADOW_PASS)",
        "#define DEPTH_PASS",
        "#endif",
        "#if defined(COMPUTE_TBN) && !defined(ENABLE_TBN)",
        "#define ENABLE_TBN",
        "#endif",
    ])

    if backend == "vulkan":
        header.extend(f"#define {usage} {index}" for usage, index in descriptor_set_usages().items())
        header.extend([
            "#define dvd_VertexIndex gl_VertexIndex",
            "#define dvd_InstanceIndex gl_InstanceIndex",
            "#define DVD_GL_BASE_INSTANCE gl_BaseInstanceARB",
            "#define DESCRIPTOR_SET_RESOURCE(SET, BINDING) layout(set = SET, binding = BINDING)",
            "#define DESCRIPTOR_SET_RESOURCE_LAYOUT(SET, BINDING, LAYOUT) layout(set = SET, binding = BINDING, LAYOUT)",
            "layout(push_constant) uniform constants { mat4 PushData0; mat4 PushData1; };",
        ])
    else:
        header.extend(f"#define {usage}_{slot} {binding}" for (usage, slot), binding in opengl_bindings().items())
        header.extend([
            "#define dvd_VertexIndex gl_VertexID",
            "#define dvd_InstanceIndex gl_InstanceID",
            "#define DVD_GL_BASE_INSTANCE gl_BaseInstance",
            "#define CONCATENATE_IMPL(s1, s2) s1 ## _ ## s2",
            "#define CONCATENATE(s1, s2) CONCATENATE_IMPL(s1, s2)",
            "#define DESCRIPTOR_SET_RESOURCE(SET, BINDING) layout(binding = CONCATENATE(SET, BINDING))",
            "#define DESCRIPTOR_SET_RESOURCE_LAYOUT(SET, BINDING, LAYOUT) layout(binding = CONCATENATE(SET, BINDING), LAYOUT)",
            "layout(location = 18) uniform mat4 PushData0;",
            "layout(location = 19) uniform mat4 PushData1;",
        ])

    varyings = [
        "vec4 _vertexW;",
        "vec4 _vertexWV;",
        "vec3 _normalWV;",
        "vec2 _texCoord;",
        "flat uvec4 _indirectionIDs;",
        "flat uint _LoDLevel;",
        "flat uint _SelectionFlag;",
        "#if defined(HAS_VELOCITY)",
        "vec4 _prevVertexWVP;",
        "#endif",
        "#if defined(ENABLE_TBN)",
        "mat3 _tbnWV;",
        "#endif",
    ]
    members = "\n".join(varyings)
    if stage == "vert":
        header.extend([f"layout(location = 0) out Data {{\n{members}\n}} _out;", "#define VAR _out"])
    elif stage == "frag":
        header.extend([f"layout(location = 0) in Data {{\n{members}\n}} _in;", "#define VAR _in"])
    elif stage != "comp":
        input_size = "gl_MaxPatchVertices" if stage in ("tesc", "tese") else "INPUT_PRIMITIVE_SIZE"
        output_size = "[TESSELLATION_OUTPUT_VERTICES]" if stage == "tesc" else ""
        header.extend([
            f"layout(location = 0) in Data {{\n{members}\n}} _in[{input_size}];",
            f"layout(location = 0) out Data {{\n{members}\n}} _out{output_size};",
            "#define VAR _in[gl_InvocationID]" if stage == "tesc" else "#define VAR _in[0]",
        ])
        target = "_out[gl_InvocationID]" if stage == "tesc" else "_out"
        names = [line.split()[-1].rstrip(";") for line in varyings if line.endswith(";")]
        copies = "\n".join(
            (f"#if defined(HAS_VELOCITY)\n{target}.{name} = _in[index].{name};\n#endif"
             if name == "_prevVertexWVP" else
             f"#if defined(ENABLE_TBN)\n{target}.{name} = _in[index].{name};\n#endif"
             if name == "_tbnWV" else f"{target}.{name} = _in[index].{name};")
            for name in names
        )
        header.append(f"void PassData(in int index) {{\n{copies}\n}}")
    return "\n".join(header) + "\n"


def pack_loose_uniforms(source, backend):
    declarations = []

    def collect(match):
        # Like ShaderDataUploader (g_uniformPattern), default initialisers are dropped when packing.
        declarations.append(re.sub(r"\s*=[^;]*;$", ";", match.group(1)))
        return ""

    source = re.sub(r"(?m)^\s*uniform\s+([^;{}]+;)\s*$", collect, source)
    if declarations:
        layout = (f"set = {descriptor_set_usages()['PER_DRAW']}, binding = 15, std140" if backend == "vulkan"
                  else f"binding = {opengl_bindings()[('PER_DRAW', 15)]}, std140")
        block = f"layout({layout}) uniform TestUniforms {{\n" + "\n".join(declarations) + "\n};\n"
        insertion = re.search(r"(?m)^#version[^\n]*\n(?:#extension[^\n]*\n)*", source)
        if not insertion:
            raise ValueError("Missing version directive in preprocessed shader")
        source = source[:insertion.end()] + block + source[insertion.end():]
    return source


class ShaderCompilationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not VALIDATOR:
            raise RuntimeError("glslangValidator is required to run the shader compilation tests")

    def prepare_source(self, body, stage, backend, defines=(), cluster_light_limit=64):
        source = engine_header(stage, backend, defines, cluster_light_limit) + expand_includes(body)
        preprocessed = subprocess.run(
            [VALIDATOR, "--stdin", "-S", stage, "-E"],
            input=source, capture_output=True, text=True, encoding="utf-8",
        )
        self.assertEqual(preprocessed.returncode, 0, preprocessed.stdout + preprocessed.stderr)
        return pack_loose_uniforms(preprocessed.stdout, backend)

    def compile_source(self, body, stage, backend, defines=(), cluster_light_limit=64):
        source = self.prepare_source(body, stage, backend, defines, cluster_light_limit)
        with tempfile.TemporaryDirectory() as directory:
            shader = Path(directory) / f"test.{stage}"
            binary = Path(directory) / "test.spv"
            shader.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [VALIDATOR, "--target-env", "vulkan1.4" if backend == "vulkan" else "opengl",
                 "-S", stage, "-o", str(binary), str(shader)],
                capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            if SPIRV_VALIDATOR:
                result = subprocess.run(
                    [SPIRV_VALIDATOR, "--target-env", "vulkan1.4" if backend == "vulkan" else "opengl4.5", str(binary)],
                    capture_output=True, text=True, encoding="utf-8",
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def compile_shader(self, filename, key, stage, backend, defines=(), cluster_light_limit=64):
        self.compile_source(section(filename, key), stage, backend, defines, cluster_light_limit)

    def link_shaders(self, filename, stages, backend, defines=()):
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for key, stage, stage_defines in stages:
                body = section(filename, key)
                source = self.prepare_source(body, stage, backend, list(defines) + list(stage_defines))
                shader = Path(directory) / f"test.{stage}"
                shader.write_text(source, encoding="utf-8")
                paths.append(str(shader))
            result = subprocess.run(
                [VALIDATOR, "--target-env", "vulkan1.4" if backend == "vulkan" else "opengl",
                 "-l", "-o", str(Path(directory) / "test.spv"), *paths],
                capture_output=True, text=True, encoding="utf-8",
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_velocity_vertex(self):
        defines = ["MAIN_DISPLAY_PASS", "HAS_VELOCITY", "HAS_POSITION_ATTRIBUTE", "HAS_NORMAL_ATTRIBUTE"]
        for backend in ("opengl", "vulkan"):
            for skinning in ([], ["USE_GPU_SKINNING", "USE_MATRIX_SKINNING", "HAS_BONE_WEIGHT_ATTRIBUTE", "HAS_BONE_INDICE_ATTRIBUTE"],
                             ["USE_GPU_SKINNING", "USE_DUAL_QUAT_SKINNING", "HAS_BONE_WEIGHT_ATTRIBUTE", "HAS_BONE_INDICE_ATTRIBUTE"]):
                with self.subTest(backend=backend, skinning=skinning):
                    self.compile_shader("baseVertexShaders.glsl", "Vertex.BasicData", "vert", backend, defines + skinning)

    def test_registered_opengl_bindings(self):
        bindings = opengl_bindings()
        self.assertEqual(bindings[("PER_DRAW", 0)], 0)
        self.assertEqual(bindings[("PER_DRAW", 12)], 0)
        self.assertEqual(bindings[("PER_DRAW", 14)], 1)
        self.assertEqual(bindings[("PER_DRAW", 15)], 2)
        self.assertEqual(bindings[("PER_BATCH", 1)], 3)
        self.assertEqual(bindings[("PER_PASS", 9)], 10)
        self.assertEqual(bindings[("PER_FRAME", 8)], 14)

    def test_exposure_histogram_compute_variants(self):
        host = (ROOT / "Source" / "Rendering" / "PostFX" / "PreRenderBatch.cpp").read_text(encoding="utf-8")
        dimensions = dict(re.findall(r"constexpr U8\s+(GROUP_[XY]_THREADS)\s*=\s*(\d+)u;", host))
        self.assertEqual(set(dimensions), {"GROUP_X_THREADS", "GROUP_Y_THREADS"})
        x, y = int(dimensions["GROUP_X_THREADS"]), int(dimensions["GROUP_Y_THREADS"])
        group_defines = [f"GROUP_SIZE {x * y}", f"THREADS_X {x}", f"THREADS_Y {y}"]
        variants = (
            ("Compute.Create", ["u_params PushData0[0]"]),
            ("Compute.Average", ["dvd_minLogLum PushData0[0].x", "dvd_logLumRange PushData0[0].y",
                                 "dvd_timeCoeff PushData0[0].z", "dvd_numPixels PushData0[0].w"]),
        )
        for backend in ("opengl", "vulkan"):
            for key, defines in variants:
                with self.subTest(backend=backend, variant=key):
                    self.compile_shader("luminanceCalc.glsl", key, "comp", backend, group_defines + defines)

    def test_light_impostor_descriptor_visibility(self):
        host = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        renderer = (ROOT / "Source" / "Rendering" / "Renderer.cpp").read_text(encoding="utf-8")
        for slot, visibility in ((8, "COMPUTE_AND_FRAGMENT"), (9, "VERTEX_FRAGMENT_COMPUTE"),
                                 (10, "COMPUTE_AND_FRAGMENT"), (11, "COMPUTE_AND_FRAGMENT")):
            with self.subTest(slot=slot):
                self.assertRegex(
                    host,
                    rf"RegisterSetLayoutBinding\(\s*DescriptorSetUsage::PER_PASS,\s*{slot},\s*"
                    rf"DescriptorSetBindingType::\w+,\s*ShaderStageVisibility::{visibility}\s*\)",
                )
                self.assertRegex(
                    renderer,
                    rf"AddBinding\(\s*cmd->_set,\s*{slot}u,\s*ShaderStageVisibility::{visibility}\s*\)",
                )

        graph = (ROOT / "Source" / "Graphs" / "SceneGraphNode.cpp").read_text(encoding="utf-8")
        self.assertRegex(graph, r"AddBinding\(\s*set,\s*13u,\s*ShaderStageVisibility::VERTEX\s*\)")
        light_input = (SHADERS / "Common" / "lightInput.cmn").read_text(encoding="utf-8")
        self.assertRegex(light_input, r"DESCRIPTOR_SET_RESOURCE_LAYOUT\(PER_PASS,\s*9,\s*std430\)"
                                      r"\s+coherent\s+ACCESS_R\s+buffer\s+dvd_LightBlock")
        self.assertRegex(section("lightImpostorShader.glsl", "Vertex"), r"dvd_LightSource\[dvd_VertexIndex\]")
        for backend in ("opengl", "vulkan"):
            with self.subTest(backend=backend):
                self.compile_shader("lightImpostorShader.glsl", "Vertex", "vert", backend)

    def test_particle_variants(self):
        variants = [
            ("Fragment.PrePass", ["MAIN_DISPLAY_PASS", "PRE_PASS", "HAS_VELOCITY"]),
            ("Fragment.PrePass", ["MAIN_DISPLAY_PASS", "PRE_PASS", "HAS_VELOCITY", "HAS_TEXTURE", "USE_UNIT0_TEXTURE"]),
            ("Fragment", ["MAIN_DISPLAY_PASS", "HAS_VELOCITY"]),
            ("Fragment", ["MAIN_DISPLAY_PASS", "HAS_VELOCITY", "HAS_TEXTURE", "USE_UNIT0_TEXTURE"]),
            ("Fragment", ["MAIN_DISPLAY_PASS", "HAS_VELOCITY", "OIT_PASS"]),
            ("Fragment", ["MAIN_DISPLAY_PASS", "HAS_VELOCITY", "OIT_PASS", "HAS_TEXTURE", "USE_UNIT0_TEXTURE"]),
            ("Fragment.Shadow.VSM", ["SHADOW_PASS", "ORTHO_PROJECTION"]),
            ("Fragment.Shadow.VSM", ["SHADOW_PASS", "HAS_TEXTURE", "USE_UNIT0_TEXTURE"]),
        ]
        host_source = (ROOT / "Source" / "Dynamics" / "Entities" / "Particles" / "ParticleEmitter.cpp").read_text(encoding="utf-8")
        self.assertRegex(
            host_source,
            r"stagePass\._stage == RenderStage::DISPLAY\s*&&\s*stagePass\._passType == RenderPassType::PRE_PASS"
            r"[\s\S]*?blockInOut\._depthWriteEnabled = false;",
        )
        self.assertRegex(
            host_source,
            r"blockInOut\._colourWrite\.b\[0\]\s*=\s*blockInOut\._colourWrite\.b\[1\]\s*="
            r"\s*blockInOut\._colourWrite\.b\[2\]\s*=\s*blockInOut\._colourWrite\.b\[3\]\s*=\s*false;",
        )
        for backend in ("opengl", "vulkan"):
            with self.subTest(backend=backend, stage="vertex"):
                self.compile_shader("particles.glsl", "Vertex", "vert", backend, ["MAIN_DISPLAY_PASS", "HAS_VELOCITY"])
            for key, defines in variants:
                with self.subTest(backend=backend, stage=key, defines=defines):
                    self.compile_shader("particles.glsl", key, "frag", backend, defines)
            for key, defines in variants:
                with self.subTest(backend=backend, link=key, defines=defines):
                    fragment_defines = [define for define in defines if define in ("HAS_TEXTURE", "OIT_PASS")]
                    global_defines = [define for define in defines if define not in fragment_defines]
                    self.link_shaders(
                        "particles.glsl", [("Vertex", "vert", ()), (key, "frag", fragment_defines)],
                        backend, global_defines,
                    )

    def test_terrain_variants(self):
        defines = [
            "MAIN_DISPLAY_PASS", "ENABLE_TBN", "OVERRIDE_DATA_IDX", "NEED_TEXTURE_DATA_ALL_STAGES",
            "SHADING_MODE_FLAT", "DISABLE_SHADOW_MAPPING", "NO_SSAO", "USE_HEIGHTMAP_TEXTURE",
            "USE_UNIT0_TEXTURE", "USE_UNIT1_TEXTURE", "USE_OPACITY_TEXTURE", "USE_NORMALMAP_TEXTURE",
            "USE_SPECULAR_TEXTURE", "USE_METALNESS_TEXTURE", "USE_EMISSIVE_TEXTURE",
        ]
        host_source = (ROOT / "Source" / "Environment" / "Terrain" / "Terrain.cpp").read_text(encoding="utf-8")
        base_stages = [
            ("Vertex", "vert", ()),
            ("TessellationC", "tesc", ()),
            ("TessellationE", "tese", ()),
        ]
        for backend in ("opengl", "vulkan"):
            for key, stage, stage_defines in base_stages:
                with self.subTest(backend=backend, stage=key):
                    self.compile_shader("terrainTess.glsl", key, stage, backend, defines + list(stage_defines))
                    if stage == "tesc":
                        preprocessed = self.prepare_source(section("terrainTess.glsl", key), stage, backend, defines)
                        self.assertNotRegex(preprocessed, r"\bTransformMatrix\s*\(")
            for mode in ("TOGGLE_LODS", "TOGGLE_TESS_LEVEL", "TOGGLE_BLEND_MAP"):
                with self.subTest(backend=backend, stage="fragment", mode=mode):
                    self.compile_shader("terrainTess.glsl", "Fragment", "frag", backend, defines + [mode])
                    self.link_shaders("terrainTess.glsl", base_stages + [("Fragment", "frag", ())], backend, defines + [mode])
            for mode in ("TOGGLE_WIREFRAME", "TOGGLE_NORMALS"):
                with self.subTest(backend=backend, stage="geometry", mode=mode):
                    self.assertRegex(host_source, r'geomModule\._defines\.emplace_back\(\s*"INPUT_PRIMITIVE_SIZE 3"\s*\)')
                    geometry = ("Geometry", "geom", ("INPUT_PRIMITIVE_SIZE 3",))
                    debug_defines = defines + ["TOGGLE_DEBUG", mode]
                    self.compile_shader("terrainTess.glsl", geometry[0], geometry[1], backend, debug_defines + list(geometry[2]))
                    self.link_shaders("terrainTess.glsl", base_stages + [geometry, ("Fragment", "frag", ())], backend, debug_defines)

    def test_water_variants(self):
        defines = [
            "MAIN_DISPLAY_PASS", "ENABLE_TBN", "COMPUTE_TBN", "USE_NORMALMAP_TEXTURE",
            "SHADING_MODE_BLINN_PHONG", "DISABLE_SHADOW_MAPPING", "NO_SSAO",
        ]
        host_source = (ROOT / "Source" / "Environment" / "Water" / "Water.cpp").read_text(encoding="utf-8")
        names = {
            "_refractionTint", "_specularShininess", "_waterDistanceFogColour",
            "_noiseTile", "_noiseFactor", "_fogStartEndDistances",
        }
        host_defines = re.findall(r'_globalDefines\.emplace_back\(\s*"(_\w+\s+[^"]+)"\s*\)', host_source)
        host_defines = [define for define in host_defines if define.split()[0] in names]
        self.assertEqual({define.split()[0] for define in host_defines}, names)
        defines.extend(host_defines)
        stages = [("Vertex", "vert", ()), ("Fragment", "frag", ())]
        for backend in ("opengl", "vulkan"):
            for variant in (["SKIP_REFLECT_REFRACT"], ["PRE_PASS", "SKIP_REFLECT_REFRACT"],
                            ["USE_PLANAR_REFLECTOR", "USE_PLANAR_REFRACTOR"]):
                with self.subTest(backend=backend, variant=variant):
                    self.link_shaders("water.glsl", stages, backend, defines + variant)
            body = section("water.glsl", "Fragment") + "\nvec2 testNoiseFactor() { return _noiseFactor; }\n"
            with self.subTest(backend=backend, variant="noise factor define"):
                self.compile_source(body, "frag", backend, defines + ["SKIP_REFLECT_REFRACT"])

    def test_cluster_light_compute(self):
        renderer = (ROOT / "Source" / "Rendering" / "Renderer.cpp").read_text(encoding="utf-8")
        aabb_defines = re.findall(r'buildDescritpor\._globalDefines\.emplace_back\(\s*"([^"]+)"\s*\)', renderer)
        self.assertEqual({define.split()[0] for define in aabb_defines}, {"inverseProjectionMatrix", "viewport", "_zPlanes"})
        for backend in ("opengl", "vulkan"):
            for key in ("Compute", "Compute.ResetCounter"):
                with self.subTest(backend=backend, shader="lightCull", stage=key):
                    self.compile_shader("lightCull.glsl", key, "comp", backend)
            with self.subTest(backend=backend, shader="lightBuildClusteredAABBs"):
                self.compile_shader("lightBuildClusteredAABBs.glsl", "Compute", "comp", backend, aabb_defines)
            with self.subTest(backend=backend, shader="lightCull", check="batched shared-memory barriers"):
                preprocessed = self.prepare_source(section("lightCull.glsl", "Compute"), "comp", backend)
                self.assertRegex(preprocessed, r"barrier\(\)[\s\S]*barrier\(\)")
        light_input = (SHADERS / "Common" / "lightInput.cmn").read_text(encoding="utf-8")
        cluster_index = re.search(r"^uint GetClusterIndex\(.*?^\}$", light_input, re.DOTALL | re.MULTILINE).group(0)
        self.assertNotIn("TARGET_VULKAN", cluster_index)

    def test_clustered_lighting_requires_positive_depth_planes(self):
        config = (ROOT / "Source" / "EngineConfig.h").read_text(encoding="utf-8")
        self.assertRegex(config, r"constexpr bool IsValidClusterDepthRange\( const F32 zNear, const F32 zFar \) noexcept"
                                 r"\s*\{\s*return zNear > 0\.f && zFar > zNear && zFar <= F32_MAX;")
        renderer = (ROOT / "Source" / "Rendering" / "Renderer.cpp").read_text(encoding="utf-8")
        prepare = re.search(r"void Renderer::prepareLighting\(.*?^\}$", renderer, re.DOTALL | re.MULTILINE).group(0)
        guard = prepare.index("IsValidClusterDepthRange( cameraSnapshot._zPlanes.min, cameraSnapshot._zPlanes.max )")
        self.assertRegex(prepare[guard:], r"^[^;]*\)\s*\)\s*\{\s*return;\s*\}")
        self.assertLess(guard, prepare.index("Renderer Cull Lights"))
        self.assertLess(guard, prepare.index("_lightBuildClusteredAABBsPipelineCmd"))
        gfx = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        self.assertIn("if ( Config::Lighting::ClusteredForward::IsValidClusterDepthRange( zNear, zFar ) )", gfx)

    def test_light_constructors_apply_requested_range(self):
        light = (ROOT / "Source" / "Rendering" / "Lighting" / "Light.cpp").read_text(encoding="utf-8")
        constructor = re.search(r"Light::Light\((.*?)\)\s*:(.*?)\{", light, re.DOTALL)
        self.assertIn("const F32 range", constructor.group(1))
        self.assertNotIn("maybe_unused", constructor.group(1))
        self.assertRegex(constructor.group(2), r"_range\( range \)")
        components = ROOT / "Source" / "ECS" / "Components"
        for name, expected in (("PointLightComponent.cpp", "20.0f"), ("SpotLightComponent.cpp", "30.0f"),
                               ("DirectionalLightComponent.cpp", "g_defaultLightDistance")):
            with self.subTest(component=name):
                source = (components / name).read_text(encoding="utf-8")
                self.assertRegex(source, rf"Light\(sgn, {re.escape(expected)}, LightType::")
        directional = (components / "DirectionalLightComponent.cpp").read_text(encoding="utf-8")
        self.assertNotIn("range(g_defaultLightDistance)", directional)

    def test_cluster_light_budget_limits(self):
        body = section("lightCull.glsl", "Compute")
        for backend in ("opengl", "vulkan"):
            for limit in (1, max_lights_per_cluster()):
                with self.subTest(backend=backend, limit=limit):
                    header = engine_header("comp", backend, (), cluster_light_limit=limit)
                    self.assertEqual(len(re.findall(r"^#define MAX_LIGHTS_PER_CLUSTER\b", header, re.MULTILINE)), 1)
                    source = self.prepare_source(body, "comp", backend, cluster_light_limit=limit)
                    self.assertRegex(source, rf"uint\s+visibleLightIndices\s*\[\s*{limit}\s*\]")
                    self.compile_shader("lightCull.glsl", "Compute", "comp", backend, cluster_light_limit=limit)

    def test_cluster_light_budget_host_and_cache_wiring(self):
        gfx = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        start = gfx.index("ErrorCode GFXDevice::initRenderingAPI(")
        end = gfx.index("ShaderProgram::OnStartup( context() )", start)
        init = gfx[start:end]
        self.assertGreater(gfx.index("hardwareState = initDescriptorSets();", end), end)
        self.assertIn("return ShaderProgram::SubmitSetLayouts(*this);", gfx)
        self.assertIn("configuredLightsPerCluster = config.rendering.numLightsPerCluster", init)
        self.assertIn("ResolveLightsPerCluster( configuredLightsPerCluster )", init)
        self.assertRegex(
            init,
            r'if\s*\(\s*configuredLightsPerCluster\s*==\s*0\s*\)\s*\{\s*'
            r'Console::warnfn\(\s*LOCALE_STR\(\s*"WARN_ZERO_LIGHTS_PER_CLUSTER"\s*\),\s*numLightsPerCluster\s*\)',
        )
        self.assertRegex(
            init,
            r"if\s*\(\s*numLightsPerCluster\s*!=\s*configuredLightsPerCluster\s*\)\s*\{\s*"
            r"config\.rendering\.numLightsPerCluster\s*=\s*numLightsPerCluster;\s*config\.changed\(\s*true\s*\);",
        )
        locale = (ROOT / "Localisation" / "enGB.ini").read_text(encoding="utf-8")
        self.assertRegex(locale, r"(?m)^WARN_ZERO_LIGHTS_PER_CLUSTER\s*=.*\{\}")

        kernel = (ROOT / "Source" / "Core" / "Kernel.cpp").read_text(encoding="utf-8")
        self.assertLess(kernel.index(".gfx().initRenderingAPI("), kernel.index(".gfx().postInitRenderingAPI("))
        self.assertIn("_renderer = std::make_unique<Renderer>( context() );", gfx)
        renderer = (ROOT / "Source" / "Rendering" / "Renderer.cpp").read_text(encoding="utf-8")
        self.assertIn("numClusters * to_U32(config.rendering.numLightsPerCluster)", renderer)
        shader_host = (ROOT / "Source" / "Platform" / "Video" / "Shaders" / "ShaderProgram.cpp").read_text(encoding="utf-8")
        self.assertIn('"#define MAX_LIGHTS_PER_CLUSTER " + Util::to_string( config.rendering.numLightsPerCluster )',
                      shader_host)
        self.assertIn("InitGLSW( gfx.renderAPI(), gfx.context().config() )", shader_host)
        startup_start = shader_host.index("ErrorCode ShaderProgram::OnStartup(")
        startup_end = shader_host.index("ErrorCode ShaderProgram::SubmitSetLayouts(", startup_start)
        startup = shader_host[startup_start:startup_end]
        self.assertIn("s_clusterLightLimit = config.rendering.numLightsPerCluster;", startup)
        parent_start = shader_host.index("ResourcePath ShaderParentCacheLocation()")
        parent_end = shader_host.index("ResourcePath SpvCacheLocation()", parent_start)
        parent = shader_host[parent_start:parent_end]
        self.assertIn("ShaderBuildCacheLocation() / ShaderAPILocation()", parent)
        self.assertIn('Util::StringFormat( "cluster-light-limit-{}", s_clusterLightLimit )', parent)
        for name, next_name in (("SpvCacheLocation", "ReflCacheLocation"),
                                ("ReflCacheLocation", "TxtCacheLocation"),
                                ("TxtCacheLocation", "ShaderBinaryCacheTag")):
            with self.subTest(cache=name):
                start = shader_host.index(f"ResourcePath {name}()")
                end = shader_host.index(next_name, start)
                self.assertIn("ShaderParentCacheLocation()", shader_host[start:end])
        for start_name, end_name in (("bool ValidateCacheLocked(", "bool DeleteCacheLocked("),
                                     ("bool DeleteCacheLocked(", "bool DeleteCache("),
                                     ("bool ShaderProgram::SaveToCache(", "bool ShaderProgram::LoadFromCache("),
                                     ("bool ShaderProgram::LoadFromCache(", "bool ShaderProgram::loadInternal(")):
            start = shader_host.index(start_name)
            end = shader_host.index(end_name, start)
            for cache in ("SpvCacheLocation()", "ReflCacheLocation()", "TxtCacheLocation()"):
                with self.subTest(operation=start_name, cache=cache):
                    self.assertIn(cache, shader_host[start:end])

    def test_hiz_compute(self):
        host = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        hiz_block = re.search(r"ShaderModuleDescriptor compModule = \{\};((?:(?!ShaderModuleDescriptor).)*?)"
                              r'compModule\._sourceFile = "HiZConstruct\.glsl";', host, re.DOTALL).group(1)
        hiz_defines = re.findall(r'compModule\._defines\.emplace_back\(\s*"([^"]+)"\s*\)', hiz_block)
        self.assertEqual({define.split()[0] for define in hiz_defines}, {"imageSizeIn", "imageSizeOut", "isBaseLevel"})
        self.assertRegex(host, r"_vec\[1\]\.x\s*=\s*i\s*==\s*0u")
        hiz_local_size = re.search(r"constexpr U32 DEPTH_REDUCE_LOCAL_SIZE = (\d+)u;", host).group(1)
        self.assertLessEqual(int(hiz_local_size) ** 2, 256)
        hiz_defines.append(f"LOCAL_SIZE {hiz_local_size}")
        for backend in ("opengl", "vulkan"):
            with self.subTest(backend=backend, shader="HiZConstruct"):
                self.compile_shader("HiZConstruct.glsl", "Compute", "comp", backend, hiz_defines)
            with self.subTest(backend=backend, shader="HiZOcclusionCull"):
                self.compile_shader("HiZOcclusionCull.glsl", "Compute", "comp", backend, ["WORK_GROUP_SIZE 64"])

    def test_compute_workgroup_requirements_match_sources(self):
        gfx = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        self.assertNotRegex(gfx, r"_THREADS\s*<\s*s_deviceInformation\._maxWorkgroup")
        self.assertRegex(gfx, r"if \( !SupportsComputeWorkgroup\( s_deviceInformation, group\._x, group\._y, group\._z \) \)"
                              r"[^}]*return ErrorCode::GFX_OLD_HARDWARE;")
        table = re.search(r"REQUIRED_COMPUTE_WORKGROUPS\s*\{\s*\{(.*?)\}\s*\};", gfx, re.DOTALL).group(1)
        literal = {name: tuple(int(v) for v in dims)
                   for name, *dims in re.findall(r'\{ "([^"]+)", (\d+)u, (\d+)u, (\d+)u \}', table)}
        declared = int(re.search(r"std::array<RequiredComputeWorkgroup, (\d+)>", gfx).group(1))
        self.assertEqual(declared, len(re.findall(r'\{ "[^"]+",', table)))

        for variant, label in (("Irradiance", "irradiance"), ("LUT", "BRDF LUT"), ("PreFilter", "prefilter")):
            threads = int(re.search(r"#define THREADS (\d+)", section("irradianceCalc.glsl", f"Compute.{variant}")).group(1))
            with self.subTest(shader="irradianceCalc", variant=variant):
                self.assertEqual(literal[f"irradianceCalc.glsl ({label})"], (threads, threads, 1))
        lut_threads = int(re.search(r"#define THREADS (\d+)", section("irradianceCalc.glsl", "Compute.LUT")).group(1))
        probes = (ROOT / "Source" / "Scenes" / "SceneEnvironmentProbePool.cpp").read_text(encoding="utf-8")
        self.assertIn(f"constexpr U32 LUT_THREADS = {lut_threads}u;", probes)
        self.assertNotIn("s_LUTTextureSize / to_F32(8)", probes)
        for variant in ("Compute.Irradiance", "Compute.LUT"):
            with self.subTest(bounds=variant):
                self.assertRegex(section("irradianceCalc.glsl", variant),
                                 r"if \(any\(greaterThanEqual\([^;]*\)\)\) \{\s*return;")

        vegetation = (ROOT / "Source" / "Environment" / "Vegetation" / "Vegetation.cpp").read_text(encoding="utf-8")
        veg_size = int(re.search(r"constexpr U32 WORK_GROUP_SIZE = (\d+);", vegetation).group(1))
        self.assertEqual(literal["instanceCullVegetation.glsl"], (veg_size, 1, 1))
        prerender = (ROOT / "Source" / "Rendering" / "PostFX" / "PreRenderBatch.cpp").read_text(encoding="utf-8")
        dims = dict(re.findall(r"constexpr U8\s+(GROUP_[XY]_THREADS)\s*=\s*(\d+)u;", prerender))
        x, y = int(dims["GROUP_X_THREADS"]), int(dims["GROUP_Y_THREADS"])
        self.assertEqual(literal["luminanceCalc.glsl (histogram)"], (x, y, 1))
        self.assertEqual(literal["luminanceCalc.glsl (average)"], (x * y, 1, 1))
        for name in ("lightCull.glsl", "HiZConstruct.glsl", "HiZOcclusionCull.glsl"):
            self.assertIn(f'{{ "{name}",', table)

    def test_irradiance_compute_variants(self):
        common = ["imgSizeX PushData0[0].x", "imgSizeY PushData0[0].y", "layerIndex PushData0[0].z"]
        prefilter = ["mipLevel uint(PushData0[1].x)", "roughness PushData0[1].y"]
        for backend in ("opengl", "vulkan"):
            for variant, extra in (("Compute.Irradiance", []), ("Compute.LUT", []), ("Compute.PreFilter", prefilter)):
                with self.subTest(backend=backend, variant=variant):
                    self.compile_shader("irradianceCalc.glsl", variant, "comp", backend, common + extra)

    def test_vegetation_cull_compute(self):
        host = (ROOT / "Source" / "Environment" / "Vegetation" / "Vegetation.cpp").read_text(encoding="utf-8")
        for name in ("dvd_grassInstanceCount", "dvd_treeInstanceCount"):
            self.assertRegex(host, rf'_ID\(\s*"{name}"\s*\)')
        defines = ["WORK_GROUP_SIZE 64", "MAX_TREE_INSTANCES 128", "MAX_GRASS_INSTANCES 256"]
        for backend in ("opengl", "vulkan"):
            for variant in ([], ["CULL_TREES"]):
                with self.subTest(backend=backend, variant=variant):
                    self.compile_shader("instanceCullVegetation.glsl", "Compute", "comp", backend, defines + variant)

    def test_vegetation_compute_barrier_follows_dispatches(self):
        host = (ROOT / "Source" / "Environment" / "Vegetation" / "Vegetation.cpp").read_text(encoding="utf-8")
        start = host.index('"Occlusion Cull Vegetation"')
        end = host.index("GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );", start)
        cull_block = host[start:end]

        dispatch_positions = [
            match.start()
            for match in re.finditer(r"GFX::EnqueueCommand\( bufferInOut, computeCmd \);", cull_block)
        ]
        self.assertEqual(len(dispatch_positions), 2)
        barrier_position = cull_block.index("GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )")
        self.assertGreater(barrier_position, max(dispatch_positions))
        for buffer in ("_grassData", "_treeData"):
            self.assertRegex(
                cull_block,
                rf"BufferSyncUsage::GPU_WRITE_TO_GPU_READ,\s*\._buffer = {buffer}->getBufferImpl\(\)",
            )

    def test_custom_pass_hiz_uses_pass_targets(self):
        executor = (ROOT / "Source" / "Rendering" / "RenderPass" / "RenderPassExecutor.cpp").read_text(encoding="utf-8")
        self.assertIn(
            "occlusionPass( playerIdx, camSnapshot, visibleNodeCount, params._target, params._targetHIZ, bufferInOut );",
            executor,
        )
        self.assertNotIn(
            "occlusionPass( playerIdx, camSnapshot, visibleNodeCount, RenderTargetNames::SCREEN,",
            executor,
        )

        vegetation = (ROOT / "Source" / "Environment" / "Vegetation" / "Vegetation.cpp").read_text(encoding="utf-8")
        for stage, target in (
            ("REFLECTION", "REFLECT::UTILS.HI_Z"),
            ("REFRACTION", "REFRACT::UTILS.HI_Z"),
        ):
            self.assertRegex(
                vegetation,
                rf"case RenderStage::{stage}:\s*return RenderTargetNames::{target};",
            )
        self.assertIn("return RenderTargetNames::UTILS.HI_Z;", vegetation)

    def test_tree_vertex_cull_sentinel(self):
        source = (SHADERS / "tree.glsl").read_text(encoding="utf-8")
        self.assertRegex(
            source,
            r"const bool isCulled = data\.data\.z < 0\.f;\s*"
            r"VAR\._LoDLevel = isCulled \? 3u : uint\(data\.data\.z\);",
        )
        self.assertIn("gl_CullDistance[0] = isCulled ? -0.01f : 0.01f;", source)
        self.assertRegex(source, r"if \(isCulled\)\s*\{\s*scale = 0\.f;")
        for backend in ("opengl", "vulkan"):
            for defines in ([], ["HAS_CULLING_OUT"]):
                with self.subTest(backend=backend, defines=defines):
                    self.compile_shader(
                        "tree.glsl", "Vertex", "vert", backend,
                        ["MAIN_DISPLAY_PASS", "MAX_TREE_INSTANCES 128"] + defines,
                    )

    def test_vegetation_instanced_draw_contract(self):
        rendering = (ROOT / "Source" / "ECS" / "Components" / "RenderingComponent.cpp").read_text(encoding="utf-8")
        self.assertRegex(
            rendering,
            r"isInstanced\(\s*RequiresInstancedIndirection\(\s*isInstanced\(\),\s*"
            r"mat->properties\(\)\.isInstanced\(\),\s*0u\s*\)\s*\)",
        )
        self.assertIn("mat->properties().isInstanced( isInstanced() );", rendering)
        self.assertIn("RequiresInstancedIndirection( isInstanced(), false, cmd._cmd.instanceCount )", rendering)
        self.assertIn("IndirectionBaseInstance( isInstanced(), _indirectionBufferEntry )", rendering)
        self.assertRegex(
            rendering,
            r"if\s*\(\s*isInstanced\(\)\s*\)\s*\{\s*pkg\.pushConstantsCmd\(\)\._uniformData->set"
            r'\(\s*_ID\(\s*"INDIRECT_DATA_IDX"\s*\),\s*PushConstantType::UINT,\s*_indirectionBufferEntry\s*\)',
        )

        shader_program = (ROOT / "Source" / "Platform" / "Video" / "Shaders" / "ShaderProgram.cpp").read_text(encoding="utf-8")
        for definition in (
            "#define dvd_InstanceIndex gl_InstanceID", "#define DVD_GL_BASE_INSTANCE gl_BaseInstance",
            "#define dvd_InstanceIndex gl_InstanceIndex", "#define DVD_GL_BASE_INSTANCE gl_BaseInstanceARB",
        ):
            self.assertIn(definition, shader_program)

        for shader, capacity, extra in (
            ("grass.glsl", "MAX_GRASS_INSTANCES 256", ["ENABLE_TBN"]),
            ("tree.glsl", "MAX_TREE_INSTANCES 128", []),
        ):
            source = (SHADERS / shader).read_text(encoding="utf-8")
            self.assertNotIn("dvd_InstanceIndex", source)
            self.assertIn("Data(VEGETATION_INSTANCE_INDEX)", source)
            if shader == "grass.glsl":
                self.assertIn("_instanceID = VEGETATION_INSTANCE_INDEX;", source)
            for backend in ("opengl", "vulkan"):
                for variant in (["MAIN_DISPLAY_PASS", "HAS_VELOCITY"],
                                ["MAIN_DISPLAY_PASS", "PRE_PASS", "HAS_VELOCITY"], ["SHADOW_PASS"]):
                    with self.subTest(shader=shader, backend=backend, variant=variant):
                        defines = [capacity, "OVERRIDE_DATA_IDX"] + extra + variant
                        preprocessed = self.prepare_source(section(shader, "Vertex"), "vert", backend, defines)
                        self.assertIn("uint INDIRECT_DATA_IDX;", preprocessed)
                        self.compile_source(section(shader, "Vertex"), "vert", backend, defines)

    def test_vegetation_uploads_allow_empty_and_grass_only_chunks(self):
        vegetation = (ROOT / "Source" / "Environment" / "Vegetation" / "Vegetation.cpp").read_text(encoding="utf-8")
        self.assertIn("bufferDescriptor._updateFrequency = BufferUpdateFrequency::OCCASIONAL;", vegetation)
        self.assertRegex(
            vegetation,
            r"if\s*\(\s*context\(\)\.config\(\)\.debug\.renderFilter\.treeInstances\s*&&\s*"
            r"!parentPtr->_treeMeshNames\.empty\(\)\s*\)",
        )
        for kind in ("grass", "tree"):
            self.assertRegex(
                vegetation,
                rf"if\s*\(\s*!data\.empty\(\)\s*\)\s*\{{\s*"
                rf"DIVIDE_ASSERT\(\s*parentPtr->_{kind}Data\s*!=\s*nullptr[^;]*;\s*"
                rf"const BufferLock lock = parentPtr->_{kind}Data->writeData\(",
            )
        self.assertIn("treeData ? to_U32( ID % parent->_treeMeshNames.size() ) : 0u", vegetation)
        prepare_draw = vegetation[vegetation.index("void Vegetation::prepareDraw"):vegetation.index("void Vegetation::prepareRender")]
        self.assertRegex(
            prepare_draw,
            r"if\s*\(\s*instance->_instanceCountTrees\s*>\s*0\s*&&\s*!_treeMeshNames\.empty\(\)\s*\)"
            r"\s*\{\s*const U32 meshID = to_U32\(\s*ID % _treeMeshNames\.size\(\)\s*\)",
        )

    def test_vulkan_buffer_updates_flush_noncoherent_writes(self):
        vulkan = ROOT / "Source" / "Platform" / "Video" / "RenderBackend" / "Vulkan" / "Buffers"
        header = (vulkan / "Headers" / "vkBufferImpl.h").read_text(encoding="utf-8")
        source = (vulkan / "vkBufferImpl.cpp").read_text(encoding="utf-8")
        self.assertIn("bool _isMemoryCoherent{ false };", header)
        self.assertIn("_isMemoryCoherent = (memPropFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;", source)
        write = source[source.index("BufferLock vkBufferImpl::writeBytes"):source.index("void vkBufferImpl::readBytes")]
        self.assertRegex(
            write,
            r"if\s*\(\s*!_isMemoryMappable\s*\|\|\s*!_isMemoryCoherent\s*\)\s*\{"
            r"\s*LockGuard<Mutex> w_lock\([^;]+;\s*"
            r"VK_CHECK\(\s*vmaFlushAllocation\([^,]+,\s*"
            r"_isMemoryMappable\s*\?\s*_allocation\s*:\s*_stagingBuffer->_allocation,\s*"
            r"mappedOffset,\s*range\._length\s*\)\s*\)",
        )
        self.assertLess(write.index("memcpy("), write.index("vmaFlushAllocation"))
        self.assertLess(write.index("vmaFlushAllocation"), write.index("RegisterTransferRequest"))

    def test_tree_chunk_offset_flows_to_each_draw_package(self):
        vegetation = (ROOT / "Source" / "Environment" / "Vegetation" / "Vegetation.cpp").read_text(encoding="utf-8")
        self.assertRegex(vegetation, r"rComp->dataFlag\(\s*to_F32\( ID \)\s*\)")

        rendering = (ROOT / "Source" / "ECS" / "Components" / "RenderingComponent.cpp").read_text(encoding="utf-8")
        self.assertRegex(
            rendering,
            r'pkg\.pushConstantsCmd\(\)\._uniformData->set\(\s*_ID\(\s*"dvd_terrainChunkOffset"\s*\),'
            r"\s*PushConstantType::UINT,\s*to_U32\( dataFlag\(\) \)\s*\)",
        )
        self.assertLess(
            rendering.index('_ID( "dvd_terrainChunkOffset" )'),
            rendering.index("Attorney::SceneGraphNodeComponent::prepareRender"),
        )

        tree_shader = (SHADERS / "tree.glsl").read_text(encoding="utf-8")
        self.assertIn("treeData[dvd_terrainChunkOffset * MAX_TREE_INSTANCES + instanceID]", tree_shader)

    def test_shadow_slot_and_camera_contracts(self):
        host = (ROOT / "Source" / "Rendering" / "Lighting" / "LightPool.cpp").read_text(encoding="utf-8")
        start = host.index("void LightPool::generateShadowMaps")
        end = host.index("void LightPool::debugLight", start)
        shadow_pass = host[start:end]
        self.assertIn("light->shadowPropertyIndex( -1 );", shadow_pass)
        self.assertIn("camera._viewMatrix, camera._projectionMatrix, camera._zPlanes.x, camera._zPlanes.y", shadow_pass)
        self.assertLess(shadow_pass.index("light->staticShadowsDirty( true )"), shadow_pass.index("slots.hasCapacity"))
        self.assertLess(shadow_pass.index("isShadowCacheInvalidated( light )"), shadow_pass.index("slots.hasCapacity"))
        self.assertLess(shadow_pass.index("slots.hasCapacity"), shadow_pass.index("IsLightInViewFrustum"))
        self.assertLess(shadow_pass.index("ShadowMap::generateShadowMaps"), shadow_pass.index("slots.reserve"))
        self.assertIn("!cacheInvalidated && ShadowMap::tryReuseShadowMaps( *light )", shadow_pass)
        self.assertIn("propsSource._lightPosition[i]._v", shadow_pass)
        self.assertIn("ShadowMap::getLightLayerRequirements( *light )", shadow_pass)
        self.assertIn("light->castsShadows() ? light->shadowPropertyIndex() : -1", host)
        self.assertIn("light->_shadowRangeState.update( light->range() )", host)
        self.assertIn("directional._shadowDirectionalSettings.update(", host)

        allocation = (ROOT / "Source" / "Rendering" / "Lighting" / "ShadowMapping" / "ShadowMap.cpp").read_text(encoding="utf-8")
        self.assertIn("TryReuseShadowLayers( lifetimeMask, light.getGUID(), offset, reservationCount, contentCount )", allocation)
        self.assertIn("ReleaseShadowLayers( lifetimeMask, light.getGUID(), MAX_SHADOW_FRAME_LIFETIME )", allocation)
        self.assertIn("isCSM ? WORLD_AO_LAYER_INDEX : shadowMapDescriptor._layerCount", allocation)
        self.assertIn("FindAvailableShadowLayers( lifetimeMask, reservationCount, 1u )", allocation)
        self.assertIn("s_shadowMapGenerators[i] = std::make_unique<CascadedShadowMapsGenerator>", allocation)
        self.assertIn("s_shadowMapGenerators[i] = std::make_unique<SingleShadowMapGenerator>", allocation)
        self.assertIn("s_shadowMapGenerators[i] = std::make_unique<CubeShadowMapGenerator>", allocation)
        self.assertIn("isCSM && !settings.enabled && !settings.csm.enabled", allocation)
        csm_generator = (ROOT / "Source" / "Rendering" / "Lighting" / "ShadowMapping" / "CascadedShadowMapsGenerator.cpp").read_text(encoding="utf-8")
        self.assertIn("dirLight.feedBackContainers().resize( numSplits )", csm_generator)
        for shadow_type in ("csm", "point", "spot"):
            self.assertIn(f"shadowSettings.{shadow_type}.enabled", shadow_pass)

        shader = (SHADERS / "FragmentAtoms" / "lightingCalc.frag").read_text(encoding="utf-8")
        for kind in ("Directional", "Point", "Spot"):
            self.assertRegex(shader, rf"shadowIndex >= 0 && receivesShadows \? getShadowMultiplier{kind}\(")
        shadow_mapping = (SHADERS / "FragmentAtoms" / "shadowMapping.frag").read_text(encoding="utf-8")
        self.assertRegex(shadow_mapping, r"if \(Split >= MAX_CSM_SPLITS_PER_LIGHT\)\s*\{\s*return 1\.f;")

    def test_spotlight_angles_and_bounds_are_wired(self):
        component = (ROOT / "Source" / "ECS" / "Components" / "SpotLightComponent.cpp").read_text(encoding="utf-8")
        self.assertIn("_coneAngles.setInnerClamped(_coneAngles._inner)", component)
        self.assertIn("_coneAngles.setOuterClamped(_coneAngles._outer)", component)
        self.assertRegex(component, r"SpotLightComponent::loadFromXML[\s\S]*?SGNComponent::loadFromXML\(pt\);"
                                    r"\s*_coneAngles\.normalize\(\);")
        light = (ROOT / "Source" / "Rendering" / "Lighting" / "Light.cpp").read_text(encoding="utf-8")
        self.assertIn("SpotConeBoundingSphere( slantHeight, cosOuter )", light)
        self.assertIn("direction.normalize();", light)
        self.assertRegex(light, r"void Light::range\([\s\S]*?staticShadowsDirty\( true \);"
                                r"\s*dynamicShadowsDirty\( true \);")
        self.assertIn("rangeField._dataSetter", light)
        editor = (ROOT / "Source" / "Editor" / "Headers" / "Utils.inl").read_text(encoding="utf-8")
        self.assertIn("ImGui::IsItemDeactivatedAfterEdit()", editor)
        self.assertIn("RecordPropertyEdits(editState, ret.activatedItem, ret.deactivatedItem", editor)
        self.assertIn("undo._onChangedCbk = field._onUndoRedo", editor)
        shader = (SHADERS / "FragmentAtoms" / "lightingCalc.frag").read_text(encoding="utf-8")
        self.assertIn("#define SpotConeIntensity(", shader)
        self.assertIn("SpotConeIntensity(theta, cosInnerConeAngle, cosOuterConeAngle)", shader)

    def test_lighting_shadow_fragment_variants(self):
        for backend in ("opengl", "vulkan"):
            for mode in ("SHADING_MODE_BLINN_PHONG", "SHADING_MODE_PBR", "SHADING_MODE_FLAT"):
                with self.subTest(backend=backend, mode=mode):
                    self.compile_shader("material.glsl", "Fragment", "frag", backend,
                                        ["MAIN_DISPLAY_PASS", mode, "NO_SSAO"])
            for shadow_type in ("CSM", "POINT", "SPOT"):
                with self.subTest(backend=backend, disabled=shadow_type):
                    self.compile_shader("material.glsl", "Fragment", "frag", backend,
                                        ["MAIN_DISPLAY_PASS", "SHADING_MODE_PBR", "NO_SSAO",
                                         f"DISABLE_SHADOW_MAPPING_{shadow_type}"])

    def test_scene_data_visible_to_compute(self):
        host = (ROOT / "Source" / "Platform" / "Video" / "GFXDevice.cpp").read_text(encoding="utf-8")
        visibility = re.search(
            r"RegisterSetLayoutBinding\(\s*DescriptorSetUsage::PER_FRAME,\s*8,\s*DescriptorSetBindingType::\w+,\s*"
            r"ShaderStageVisibility::(\w+)\s*\)", host)
        self.assertIsNotNone(visibility)
        self.assertIn(visibility.group(1), ("ALL", "COMPUTE_AND_FRAGMENT",
                                            "VERTEX_FRAGMENT_COMPUTE", "COMPUTE_AND_GEOMETRY"))

    def test_common_easing_and_packing_helpers(self):
        names = [
            "easeInSine", "easeOutSine", "easeInOutSine",
            "easeInQuad", "easeOutQuad", "easeInOutQuad",
            "easeInCubic", "easeOutCubic", "easeInOutCubic",
            "easeInQuart", "easeOutQuart", "easeInOutQuart",
            "easeInQuint", "easeOutQuint", "easeInOutQuint",
            "easeInEXpo", "easeOutEXpo", "easeInOutEXpo",
            "easeInCirc", "easeOutCirc", "easeInOutCirc",
            "easeInBack", "easeOutBack", "easeInOutBack",
            "easeInElastic", "easeOutElastic", "easeInOutElastic",
            "easeInBounce", "easeOutBounce", "easeInOutBounce",
        ]
        calls = "\n".join(f"    result += {name}(x);" for name in names)
        inline_names = names[-9:]
        calls += "\n" + "\n".join(f"    result += {name}(x + 0.05f);" for name in inline_names)
        body = f"""
#include "utility.frag"
layout(location = 0) out vec4 testColour;
void main() {{
    const float x = gl_FragCoord.x * 0.001f;
    float result = 0.f;
{calls}
    testColour = vec4(result, unpackVec2(packVec2(x + 0.1f, 0.5f)), 1.f);
}}
"""
        for backend in ("opengl", "vulkan"):
            with self.subTest(backend=backend):
                preprocessed = self.prepare_source(body, "frag", backend)
                for name in inline_names:
                    self.assertNotRegex(preprocessed, rf"\b{name}\s*\(")
                self.compile_source(body, "frag", backend)


if __name__ == "__main__":
    unittest.main()
