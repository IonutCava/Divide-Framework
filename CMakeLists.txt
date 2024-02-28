# CMakeList.txt : Top-level CMake project file, do global configuration and include sub-projects here.
cmake_minimum_required (VERSION 3.18)

# This is required to be before the first project() call of the CMake build.
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file.")
set(CMAKE_VERBOSE_MAKEFILE OFF)
set(PRINT_ALL_CMAKE_VARIABLES TRUE)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/OverlayPorts")

project ( Divide-Framework
          VERSION 0.1
          LANGUAGES C CXX
          HOMEPAGE_URL "http://divide-studio.co.uk"
          DESCRIPTION "A test framework for various game development and rendering techniques"
)

set(PROJECT_LICENSE_FILE "${CMAKE_SOURCE_DIR}/../LICENSE" CACHE FILEPATH "Path to the license file for this project.")

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)
set(FETCHCONTENT_QUIET TRUE)

set(CMAKE_DEBUG_POSTFIX _d)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  add_compile_definitions(CLANG_COMPILER)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_definitions(GCC_COMPILER)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
  add_compile_definitions(INTEL_COMPILER)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  add_compile_definitions(MSVC_COMPILER)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  add_compile_options(/W4)
  add_compile_options(/bigobj)
  add_compile_options(/arch:AVX2)
  add_compile_options(/Zc:__cplusplus)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
      add_compile_options(/Zm256)
  endif()
  add_compile_options("/ZI")
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  add_link_options("/INCREMENTAL")
else ()
  add_compile_options(-Wall)
  add_compile_options(-mbig-obj)
  add_compile_options(-mavx2)
  add_compile_options(-Wno-c++98-compat -Wno-c++98-compat-pedantic)
endif ()

set(BUILD_TYPE_STR "")
set(BUILD_SELECTION_STR "")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    SET(BUILD_TYPE_STR "Config: DEBUG")
    add_compile_definitions(_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    SET(BUILD_TYPE_STR "Config: PROFILE")
    add_compile_definitions(_PROFILE)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    SET(BUILD_TYPE_STR "Config: RELEASE")
    add_compile_definitions(_RELEASE)
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    message( FATAL_ERROR "Divide: MinSizeRel unused for now!" )
else()
    message( FATAL_ERROR "Divide: Unknown build configuration!" )
endif()

if (DIVIDE_STARTUP_TYPE STREQUAL "IN_EDITOR")
    add_compile_definitions(_START_IN_EDITOR)
    set(BUILD_SELECTION_STR "Divide: Editor build selected!")
else()
    set(BUILD_SELECTION_STR "Divide: Game build selected!")
endif()

message("${BUILD_SELECTION_STR} ${BUILD_TYPE_STR}")

add_subdirectory(Source)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Divide-Framework)

if (PRINT_ALL_CMAKE_VARIABLES)
    get_cmake_property(_variableNames VARIABLES)
    list (SORT _variableNames)
    foreach (_variableName ${_variableNames})
        message(STATUS "${_variableName}=${${_variableName}}")
    endforeach()
endif()
