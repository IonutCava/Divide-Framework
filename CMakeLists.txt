# CMakeList.txt : Top-level CMake project file, do global configuration and include sub-projects here.
cmake_minimum_required (VERSION 3.29.2)

option(BUILD_TESTING_INTERNAL "Build unit test executables" ON)
option(RUN_TESTING_INTERNAL "Run unit test after every build" ON)
option(PRINT_ALL_CMAKE_VARIABLES "Print all of the CMake variables and their values at the end of the configure step. Mostly for debugging." OFF)
option(ENABLE_AVX2 "Build with AVX2 support if available. Will also enable AVX and SSE42" ON)
option(ENABLE_AVX "Build with AVX support if available. Will also enable SSE42." ON)
option(ENABLE_SSE42 "Build with SSE4.2 support if available" ON)
option(ENABLE_ASAN "Enable address sanitization" OFF)
option(ENABLE_UBSAN "Enable undefined behbaviour sanitization" OFF)
option(ENABLE_JUST_MY_CODE "Enable jsut my code debugging in MSVC in debug builds (/JMC)" OFF)

# This is required to be before the first project() call of the CMake build.
#set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file.")

include("${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/PlatformHelpers/OptimizeForArchitecture.cmake")

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
          VERSION 0.2
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

set(AVX2_OPT OFF)
set(AVX_OPT OFF)
set(SSE42_OPT OFF)

if ( ENABLE_SSE42 OR ENABLE_AVX OR ENABLE_AVX2 )
  AutodetectHostArchitecture()
  OptimizeForArchitecture()

  foreach(flag ${_enable_vector_unit_list})  # from OptimizeForArchitecture()
      string(TOUPPER "${flag}" flag)
      string(REPLACE "\." "_" flag "${flag}")
      string(REPLACE "_" "" flag "${flag}")

      if("${flag}" MATCHES "AVX2")
        set(AVX2_OPT ON)
      endif()

      if("${flag}" MATCHES "AVX")
          set(AVX_OPT ON)
      endif()
      
      if("${flag}" MATCHES "SSE42")
          set(SSE42_OPT ON)
      endif()

  endforeach(flag)

  if (AVX2_OPT AND ENABLE_AVX2)
    message("Enabling AVX2 + AVX + SSE4.2 support!")

    add_compile_definitions(HAS_AVX2 HAS_AVX HAS_SSE42)
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
      add_compile_options(/arch:AVX2)
    else()
      add_compile_options(-mavx2 -mavx -msse4.2)
    endif()
  elseif(AVX_OPT AND (ENABLE_AVX2 OR ENABLE_AVX))
    if (ENABLE_AVX2)
      message("AVX2 requested but only AVX available.")
    endif()

    message("Enabling AVX + SSE4.2 support!")

    add_compile_definitions(HAS_AVX HAS_SSE42)
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
      add_compile_options(/arch:AVX)
    else()
      add_compile_options(-mno-avx2 -mavx -msse4.2)
    endif()
  elseif(SSE42_OPT AND (ENABLE_AVX2 OR ENABLE_AVX OR ENABLE_SSE42))
    if (ENABLE_AVX2 OR ENABLE_AVX)
      message("AVX/AVX2 requested but only SSE4.2 available.")
    endif()

    message("Enabling SSE4.2 support!")

    add_compile_definitions(HAS_SSE42)
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
      add_compile_options(/d2archSSE42)
    else()
      add_compile_options(-mno-avx2 -mno-avx -msse4.2)
    endif()
  else()
    message("None of the requested SIMD instruction sets found!")
  endif()
else ()
  message("No SIMD instruction set enabled!")
endif ()

if ( NOT AVX2_OPT AND NOT AVX_OPT AND NOT SSE42_OPT)
  add_compile_definitions(NO_SIMD)
  if (NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_compile_options(-mno-avx2 -mno-avx -mno-sse4.2)
  endif()
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  set(CMAKE_DEBUG_PREFIX /D)

  add_compile_definitions(USING_MSVC _CRT_SECURE_NO_WARNINGS)
  add_compile_options(/Zc:__cplusplus /Zm256 /utf-8) 
  add_compile_options("$<$<CONFIG:DEBUG>:/d2Obforceinline>")
  add_link_options(/INCREMENTAL)

  set (CMAKE_VS_JUST_MY_CODE_DEBUGGING $<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_JUST_MY_CODE}>>,ON,OFF>)

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


    if ( ENABLE_SSE42 OR ENABLE_AVX OR ENABLE_AVX2 )
      foreach(flag ${Vc_ARCHITECTURE_FLAGS})
        message("Architecture flag: " ${flag})
      endforeach()
    endif()

endif()
