# Copyright (c) 2026 DIVIDE-Studio
# Use the same target/compiler detector as the engine, without installing vcpkg.
set(_simd_preflight_args "")
foreach(variable IN ITEMS CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_TARGET
        CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR
        CMAKE_OSX_ARCHITECTURES CMAKE_OSX_SYSROOT CMAKE_APPLE_SILICON_PROCESSOR
        CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET TARGET_ARCHITECTURE)
    if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
        list(APPEND _simd_preflight_args "-D${variable}=${${variable}}")
    endif()
endforeach()
if(VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    get_filename_component(_simd_toolchain "${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
elseif(CMAKE_TOOLCHAIN_FILE)
    get_filename_component(_simd_toolchain "${CMAKE_TOOLCHAIN_FILE}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
    file(TO_CMAKE_PATH "${_simd_toolchain}" _simd_toolchain)
    if(_simd_toolchain MATCHES "/scripts/buildsystems/vcpkg\\.cmake$")
        set(_simd_toolchain "")
    endif()
else()
    set(_simd_toolchain "")
endif()
if(_simd_toolchain)
    list(APPEND _simd_preflight_args "-DCMAKE_TOOLCHAIN_FILE=${_simd_toolchain}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_LIST_DIR}/SIMDPreflight"
        -B "${CMAKE_BINARY_DIR}/simd-preflight"
        -G "${CMAKE_GENERATOR}"
        "-DDIVIDE_VECTOR_EXTENSION_SET=${DIVIDE_VECTOR_EXTENSION_SET}"
        ${_simd_preflight_args}
    RESULT_VARIABLE _simd_preflight_result
    OUTPUT_VARIABLE _simd_preflight_output
    ERROR_VARIABLE _simd_preflight_error
    TIMEOUT 180
)
if(NOT "${_simd_preflight_result}" STREQUAL "0")
    message(FATAL_ERROR "SIMD request rejected before dependency installation:\n${_simd_preflight_output}\n${_simd_preflight_error}")
endif()
