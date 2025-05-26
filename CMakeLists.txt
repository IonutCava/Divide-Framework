# CMakeList.txt : Top-level CMake project file, do global configuration and include sub-projects here.
cmake_minimum_required (VERSION 3.29.2)

option(BUILD_TESTING_INTERNAL "Build unit test executables" ON)
option(RUN_TESTING_INTERNAL "Run unit test after every build" ON)
option(ENABLE_JUST_MY_CODE "Enable jsut my code debugging in MSVC in debug builds (/JMC)" OFF)
option(PRINT_ALL_CMAKE_VARIABLES "Print all of the CMake variables and their values at the end of the configure step. Mostly for debugging." OFF)
option(SHOW_CONSOLE_WINDOW "Show the console window and Win32 builds and direct all log output to it" OFF)
option(ENABLE_MIMALLOC "Use mimalloc to override malloc/free calls" OFF)

include("${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/PlatformHelpers/OptimizeForArchitecture.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/PlatformHelpers/ToggleVectorExtensions.cmake")

set(vcpkg ${CMAKE_CURRENT_LIST_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)
if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
    if ( EXISTS "${vcpkg}")
        set(CMAKE_TOOLCHAIN_FILE "${vcpkg}" CACHE FILEPATH "CMake toolchain file")
    else()
        message( FATAL_ERROR "Missing VCPKCG CMake toolchain file" )
    endif()
endif()
  
message(STATUS "vcpkg toolchain found in: ${CMAKE_TOOLCHAIN_FILE}")

if(DEFINED ENV{GITHUB_ACTIONS})
    set(IS_GITHUB_ACTIONS TRUE)
else()
    set(IS_GITHUB_ACTIONS FALSE)
endif()

# Enable Hot Reload for MSVC compilers if supported.
if (NOT RUN_ASAN AND NOT RUN_UBSAN AND POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:EditAndContinue>")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:RelWithDebInfo>:ProgramDatabase>")
endif()

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)
set(CXX_SCAN_FOR_MODULES Off)
set(FETCHCONTENT_QUIET TRUE)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/OverlayPorts")

project (Divide-Framework
          VERSION 0.2
          LANGUAGES C CXX
          HOMEPAGE_URL "http://divide-studio.co.uk"
          DESCRIPTION "A test framework for various game development and rendering techniques"
)

set(PROJECT_LICENSE_FILE "${CMAKE_SOURCE_DIR}/../LICENSE" CACHE FILEPATH "Path to the license file for this project.")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(MSVC_COMPILER OFF) 
set(CLANG_COMPILER OFF)
set(GNU_COMPILER OFF)

message("Trying to match compiler for ID: ${CMAKE_CXX_COMPILER_ID}")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    message("Using MSVC compiler")
    set(MSVC_COMPILER ON) 
    add_compile_definitions(USING_MSVC)
endif()
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    message("Using Clang compiler")
    set(CLANG_COMPILER ON) 
    add_compile_definitions(USING_CLANG)
endif()
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    message("Using GNU compiler")
    set(GNU_COMPILER ON) 
    add_compile_definitions(USING_GCC)
endif()

if(WINDOWS_OS_BUILD)
    message("Detected Windows Build")
    add_compile_definitions(IS_WINDOWS_BUILD)
elseif(LINUX_OS_BUILD)
    message("Detected Linux Build")
    add_compile_definitions(IS_LINUX_BUILD)
elseif(MAC_OS_BUILD)
    message("Detected MacOS Build")
    add_compile_definitions(IS_MACOS_BUILD)
else()
    message(FATAL_ERROR "Unknow target operating system!")
endif()

if ( ENABLE_OPTICK_PROFILER )
    add_compile_definitions(ENABLE_OPTICK_PROFILER)
    message("Enabling Optick sampling!")
else()
    message("Optick sampling disabled!")
endif()

if ( ENABLE_FUNCTION_PROFILING )
    add_compile_definitions(ENABLE_FUNCTION_PROFILING)
    message("Enabling internal function sampling!")
else()
    message("Disabling internal function sampling!")
endif()

Toggle_Available_Vector_Extensions()

set(EXTRA_COMPILE_FLAGS "")
set(EXTRA_LINK_FLAGS "")

if (ENABLE_MIMALLOC)
    add_compile_definitions(ENABLE_MIMALLOC)
endif()

if (MSVC_COMPILER)
  set(CMAKE_DEBUG_PREFIX /D)

  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  add_compile_options(/Zc:__cplusplus /Zm256 /utf-8) 
  add_compile_options("$<$<CONFIG:Debug>:/d2Obforceinline>")

  set (CMAKE_VS_JUST_MY_CODE_DEBUGGING $<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_JUST_MY_CODE}>>,ON,OFF>)

  if ( RUN_ASAN )
    message("ASAN enabled!")
    add_compile_definitions(_DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION)
    list(APPEND EXTRA_COMPILE_FLAGS "/fsanitize=address")
    list(APPEND EXTRA_LINK_FLAGS "/fsanitize=address")
    add_link_options(/INCREMENTAL)
  else()
    message("ASAN disabled!")
    add_link_options("$<IF:$<CONFIG:Debug>,/INCREMENTAL,/INCREMENTAL:NO>")
    if(NOT IS_GITHUB_ACTIONS)
        add_compile_options("$<$<CONFIG:RelWithDebInfo>:/dynamicdeopt>")
        add_link_options("$<$<CONFIG:RelWithDebInfo>:/dynamicdeopt /DEBUG>")
      else()
        message(STATUS "Skipping /dynamicdeopt for GitHub Actions build")
    endif()
  endif ()

  if ( RUN_UBSAN )
    message("UBSAN enabled but not yet supported!")
  else()
    message("UBSAN disabled!")
  endif()

  if ( ENABLE_FUNCTION_PROFILING )
    add_link_options(/PROFILE)
  endif()

  if (SHOW_CONSOLE_WINDOW)
    add_compile_definitions(SHOW_CONSOLE_WINDOW)
  endif()

else ()
  set(CMAKE_DEBUG_PREFIX -D)

  if (CLANG_COMPILER OR GNU_COMPILER)
    if (CLANG_COMPILER)
      add_compile_options( -Wno-unknown-argument -Wno-unused-command-line-argument -fdeclspec )
      
      if(LINUX_OS_BUILD)
        add_link_options("-fuse-ld=lld")
      endif()

    endif()
  else()
    message( FATAL_ERROR "Unknown compiler type")
  endif ()

    add_compile_options(-pthread $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++17-extensions>)

  if ( RUN_ASAN )
    list(APPEND EXTRA_COMPILE_FLAGS "-fsanitize=address")
    list(APPEND EXTRA_LINK_FLAGS "-fsanitize=address")
    message("ASAN enabled!")
  else()
    message("ASAN disabled!")
  endif ()

  if ( RUN_UBSAN )
    list(APPEND EXTRA_COMPILE_FLAGS "-fsanitize=undefined")
    list(APPEND EXTRA_LINK_FLAGS "-fsanitize=undefined")
    message("UBSAN enabled!")
  else()
    message("UBSAN disabled!")
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

    foreach(flag ${Vc_ARCHITECTURE_FLAGS})
      message("Architecture flag: " ${flag})
    endforeach()
endif()
