# CMakeList.txt : Top-level CMake project file, do global configuration and include sub-projects here.
cmake_minimum_required (VERSION 3.18)


option(BUILD_TESTING_INTERNAL "Build and run unit tests" ON)
option(PRINT_ALL_CMAKE_VARIABLES "Print all of the CMake variables and their values at the end of the configure step. Mostly for debugging." OFF)
option(ENABLE_AVX2 "Toggle AVX2 specific compiler flags" ON)

# This is required to be before the first project() call of the CMake build.
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file.")

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

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  add_compile_definitions(USING_MSVC)
  set(CMAKE_DEBUG_PREFIX /D)

  add_compile_options(/W4)
  add_compile_options(/bigobj)
  add_compile_options(/Zc:__cplusplus)
  add_compile_options(/Zm256)
  add_compile_options(/ZI)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

  #ToDo(Ionut): These should be disabled only for third party libs
  #>>>
  add_compile_definitions(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
  #<<<

  add_link_options(/INCREMENTAL)

  if( ENABLE_AVX2 )
    add_compile_options(/arch:AVX2)
  endif()
else ()
  set(CMAKE_DEBUG_PREFIX -D)

  add_compile_options(-Wall)
  add_compile_options(-Wno-unused-function)

  #ToDo(Ionut): These should be disabled only for third party libs
  #>>>
  add_compile_options(-Wno-deprecated-declarations)
  add_compile_options(-Wno-language-extension-token)
  add_compile_options(-Wno-unsafe-buffer-usage)
  add_compile_options(-Wno-zero-as-null-pointer-constant)
  add_compile_options(-Wno-old-style-cast)
  add_compile_options(-Wno-global-constructors)
  #<<<

  if( ENABLE_AVX2 )
    add_compile_options(-mavx2)
  endif()

  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_definitions(USING_CLANG)

    add_compile_options(-Wno-c++98-compat -Wno-c++98-compat-pedantic)

    add_link_options(-v)
  else()
    add_compile_definitions(USING_GCC)
  endif ()
endif ()


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_DEBUG_PREFIX}_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_DEBUG_PREFIX}_RELEASE")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_DEBUG_PREFIX}_PROFILE")

if (DIVIDE_STARTUP_TYPE STREQUAL "IN_EDITOR")
    add_compile_definitions(_START_IN_EDITOR)
endif()

if (BUILD_TESTING_INTERNAL)
    list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")
    enable_testing()
endif()
add_subdirectory(Source)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT Divide-Framework)

if (PRINT_ALL_CMAKE_VARIABLES)
    get_cmake_property(_variableNames VARIABLES)
    list (SORT _variableNames)
    foreach (_variableName ${_variableNames})
        message(STATUS "${_variableName}=${${_variableName}}")
    endforeach()
endif()
