# Copyright (c) 2026 DIVIDE-Studio
cmake_minimum_required(VERSION 3.29.2)
project(DivideSIMDPreflight LANGUAGES CXX)

set(WINDOWS_OS_BUILD ${WIN32})
set(MAC_OS_BUILD ${APPLE})
set(MSVC_COMPILER ${MSVC})
include("${CMAKE_CURRENT_LIST_DIR}/../PlatformHelpers/OptimizeForArchitecture.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../PlatformHelpers/ToggleVectorExtensions.cmake")
Toggle_Available_Vector_Extensions()
