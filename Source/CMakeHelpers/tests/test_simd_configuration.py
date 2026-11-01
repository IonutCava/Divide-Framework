# Copyright (c) 2026 DIVIDE-Studio
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class SimdConfigurationTests(unittest.TestCase):
    def test_unavailable_isa_is_rejected_instead_of_downgrading(self):
        helper = pathlib.Path(__file__).resolve().parents[1] / "PlatformHelpers/ToggleVectorExtensions.cmake"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.29.2)
project(SimdConfigurationTest NONE)
set(WINDOWS_OS_BUILD ON)
set(MSVC_COMPILER ON)
set(DIVIDE_VECTOR_EXTENSION_SET AVX2)
macro(OptimizeForArchitecture)
    set(_enable_vector_unit_list SSE4.1 SSE4.2 POPCNT)
endmacro()
include("{helper.as_posix()}")
Toggle_Available_Vector_Extensions()
message(FATAL_ERROR "Unreachable: unsupported requests must not continue")
""")
            result = subprocess.run(
                [shutil.which("cmake") or "cmake", "-S", str(root), "-B", str(root / "build")],
                text=True, capture_output=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("needs AVX2_OPT", result.stderr)
            self.assertNotIn("Unreachable", result.stderr)

    def test_windows_simd_levels_emit_project_owned_sse42_definition(self):
        helper = pathlib.Path(__file__).resolve().parents[1] / "PlatformHelpers/ToggleVectorExtensions.cmake"
        for level, arch in (("SSE42", ""), ("AVX", "/arch:AVX"), ("AVX2", "/arch:AVX2")):
            with self.subTest(level=level), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                (root / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.29.2)
project(SimdConfigurationTest NONE)
set(WINDOWS_OS_BUILD ON)
set(MSVC_COMPILER ON)
set(DIVIDE_VECTOR_EXTENSION_SET {level})
macro(OptimizeForArchitecture)
    set(_enable_vector_unit_list SSE4.1 SSE4.2 POPCNT AVX AVX2 F16C FMA BMI LZCNT)
endmacro()
include("{helper.as_posix()}")
Toggle_Available_Vector_Extensions()
get_directory_property(definitions COMPILE_DEFINITIONS)
if(NOT "HAS_SSE42" IN_LIST definitions)
    message(FATAL_ERROR "MSVC requires the project-owned SSE42 definition")
endif()
if(NOT "${{EXTRA_COMPILE_FLAGS}}" STREQUAL "{arch}")
    message(FATAL_ERROR "Wrong MSVC ISA flags: ${{EXTRA_COMPILE_FLAGS}}")
endif()
""")
                result = subprocess.run(
                    [shutil.which("cmake") or "cmake", "-S", str(root), "-B", str(root / "build")],
                    text=True, capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
