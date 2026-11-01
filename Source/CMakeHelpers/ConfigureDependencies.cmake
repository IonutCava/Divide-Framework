# Copyright (c) 2026 DIVIDE-Studio
# Manifest installation happens inside project(), before Source is configured.
list(REMOVE_ITEM VCPKG_MANIFEST_FEATURES tests)
if(BUILD_TESTING_INTERNAL)
    list(APPEND VCPKG_MANIFEST_FEATURES tests)
endif()

list(REMOVE_ITEM VCPKG_MANIFEST_FEATURES jolt-avx jolt-avx2 jolt-avx512)
string(TOUPPER "${DIVIDE_VECTOR_EXTENSION_SET}" _jolt_extension_set)
if(MAC_OS_BUILD AND CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    # Rosetta targets must remain usable without AVX, independent of the host CPU.
    set(_jolt_extension_set SSE42)
endif()
if(_jolt_extension_set MATCHES "^(AVX|AVX2|AVX512)$")
    string(TOLOWER "${_jolt_extension_set}" _jolt_feature)
    list(APPEND VCPKG_MANIFEST_FEATURES "jolt-${_jolt_feature}")
endif()
