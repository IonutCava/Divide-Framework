# CMakeList.txt : Top-level CMake project file, do global configuration and include sub-projects here.
cmake_minimum_required (VERSION 3.28)

option(BUILD_TESTING_INTERNAL "Build unit test executables" ON)
option(RUN_TESTING_INTERNAL "Run unit test after every build" ON)
option(PRINT_ALL_CMAKE_VARIABLES "Print all of the CMake variables and their values at the end of the configure step. Mostly for debugging." OFF)
option(ENABLE_AVX2 "Build with AVX2 support" ON)
option(ENABLE_ASAN "Enable address sanitization" OFF)
option(ENABLE_UBSAN "Enable undefined behbaviour sanitization" OFF)

# This is required to be before the first project() call of the CMake build.
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file.")

# Enable Hot Reload for MSVC compilers if supported.
if (NOT ENABLE_ASAN AND NOT ENABLE_UBSAN AND POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)
set(CXX_SCAN_FOR_MODULES Off)
set(FETCHCONTENT_QUIET TRUE)

set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/OverlayPorts")

project ( Divide-Framework
          VERSION 0.1
          LANGUAGES C CXX
          HOMEPAGE_URL "http://divide-studio.co.uk"
          DESCRIPTION "A test framework for various game development and rendering techniques"
)

set(PROJECT_LICENSE_FILE "${CMAKE_SOURCE_DIR}/../LICENSE" CACHE FILEPATH "Path to the license file for this project.")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(EXTRA_COMPILE_FLAGS "")
set(EXTRA_LINK_FLAGS "")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  set(CMAKE_DEBUG_PREFIX /D)

  add_compile_definitions(USING_MSVC _CRT_SECURE_NO_WARNINGS)
  add_compile_options(/Zc:__cplusplus /Zm256 /utf-8 $<$<BOOL:${ENABLE_AVX2}>:/arch:AVX2>) 
  add_link_options(/INCREMENTAL)

  if ( ENABLE_ASAN )
    add_compile_definitions(_DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION)
    list(APPEND EXTRA_COMPILE_FLAGS "/fsanitize=address")
    list(APPEND EXTRA_LINK_FLAGS "/fsanitize=address")
    message("ASAN enabled!")
  endif ()

  if ( ENABLE_UBSAN )
    message("UBSAN enabled but NOT SUPPORTED!")
  endif ()

else ()

  set(CMAKE_DEBUG_PREFIX -D)
  add_compile_options(-march=native $<IF:$<BOOL:${ENABLE_AVX2}>,-mavx2,-msse4.2>)

  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_definitions(USING_CLANG)
    add_compile_options(-Wno-c++17-extensions -Wno-unknown-argument -Wno-unused-command-line-argument)
  else ()
    add_compile_definitions(USING_GCC)
  endif ()

  if ( ENABLE_ASAN )
    list(APPEND EXTRA_COMPILE_FLAGS "-fsanitize=address")
    list(APPEND EXTRA_LINK_FLAGS "-fsanitize=address")
    message("ASAN enabled!")
  endif ()

  if ( ENABLE_UBSAN )
    list(APPEND EXTRA_COMPILE_FLAGS "-fsanitize=undefined")
    list(APPEND EXTRA_LINK_FLAGS "-fsanitize=undefined")
    message("UBSAN enabled!")
  endif ()

endif ()

if( ENABLE_AVX2 )
  add_compile_definitions(HAS_AVX2)
  message("AVX2 enabled!")
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_DEBUG_PREFIX}_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_DEBUG_PREFIX}_RELEASE")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_DEBUG_PREFIX}_PROFILE")

if (DIVIDE_STARTUP_TYPE STREQUAL "IN_EDITOR")
    add_compile_definitions(_START_IN_EDITOR)
endif()

if (RUN_TESTING_INTERNAL)
    message("Unit testing enabled!")
    list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")
    enable_testing()
endif()

add_subdirectory(Source)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT Divide-Framework)

if (PRINT_ALL_CMAKE_VARIABLES)
    get_cmake_property(_variableNames VARIABLES)
    list (SORT _variableNames)
    foreach (_variableName ${_variableNames})
        message("${_variableName}=${${_variableName}}")
    endforeach()
endif()
