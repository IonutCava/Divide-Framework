# CMakeList.txt : Top-level CMake project file, do global configuration and include sub-projects here.
cmake_minimum_required (VERSION 3.29.2)

include(CMakeDependentOption)

option(BUILD_TESTING_INTERNAL "Build unit test executables" ON)
cmake_dependent_option(RUN_TESTING_INTERNAL "Run unit test after every build" ON BUILD_TESTING_INTERNAL OFF)

option(ENABLE_JUST_MY_CODE "Enable just my code debugging in MSVC in debug builds (/JMC)" OFF)
option(PRINT_ALL_CMAKE_VARIABLES "Print all of the CMake variables and their values at the end of the configure step. Mostly for debugging." OFF)
option(SHOW_CONSOLE_WINDOW "Show the console window and Win32 builds and direct all log output to it" OFF)
option(ENABLE_MIMALLOC "Use mimalloc to override malloc/free calls" OFF)
option(ENABLE_DYNAMIC_DEOPTIMISATION "Enable Dynamic Deoptimisation" OFF)

include("${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/PlatformHelpers/OptimizeForArchitecture.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/PlatformHelpers/ToggleVectorExtensions.cmake")

set(vcpkg ${CMAKE_CURRENT_LIST_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)

if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
    if ( EXISTS "${vcpkg}")
        set(CMAKE_TOOLCHAIN_FILE "${vcpkg}" CACHE FILEPATH "CMake toolchain file")
    else()
        message( FATAL_ERROR "Missing VCPKG CMake toolchain file" )
    endif()
endif()
  
message(STATUS "vcpkg toolchain found in: ${CMAKE_TOOLCHAIN_FILE}")

cmake_policy(SET CMP0017 NEW)
cmake_policy(SET CMP0045 NEW)
cmake_policy(SET CMP0077 NEW)
cmake_policy(SET CMP0141 NEW)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CXX_SCAN_FOR_MODULES OFF)
set(FETCHCONTENT_QUIET TRUE)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/Source/CMakeHelpers/OverlayPorts")

project(
    Divide-Framework
    VERSION 0.2
    LANGUAGES C CXX
    HOMEPAGE_URL "http://divide-studio.co.uk"
    DESCRIPTION "A test framework for various game development and rendering techniques"
)

set(PROJECT_LICENSE_FILE "${CMAKE_SOURCE_DIR}/../LICENSE" CACHE FILEPATH "Path to the license file for this project.")

set(APP_LIB_DIVIDE "Divide-Framework-Lib")
set(APP_EXE_DIVIDE "Divide-Framework")
set(APP_EXE_PROJECT_MANAGER "Divide-Project-Manager")

set(APP_BUILD_EXES ${APP_EXE_DIVIDE} ${APP_EXE_PROJECT_MANAGER} )
set(APP_TEST_EXES "")


if( BUILD_TESTING_INTERNAL )
    set(APP_EXE_TEST_PLATFORM "Divide-Test-Platform")
    set(APP_EXE_TEST_ENGINE "Divide-Test-Engine")

    list(APPEND APP_TEST_EXES ${APP_EXE_TEST_PLATFORM} ${APP_EXE_TEST_ENGINE} )
    list(APPEND APP_BUILD_EXES ${APP_TEST_EXES} )
endif() #BUILD_TESTING_INTERNAL

set(APP_BUILD_TARGETS ${APP_LIB_DIVIDE} ${APP_BUILD_EXES} )

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(MSVC_COMPILER OFF) 
set(CLANG_COMPILER OFF)
set(GNU_COMPILER OFF)
set(CLANG_MSVC_FRONTEND OFF)
set(EXTRA_COMPILE_FLAGS "")
set(EXTRA_LINK_FLAGS "")

if( DEFINED ENV{GITHUB_ACTIONS} )
    set(IS_GITHUB_ACTIONS TRUE)
else()
    set(IS_GITHUB_ACTIONS FALSE)
endif()

message("Trying to match compiler for ID: ${CMAKE_CXX_COMPILER_ID}")

if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC" )
    set(MSVC_COMPILER ON) 
    add_compile_definitions( USING_MSVC )
    message("Using MSVC compiler")
elseif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang" )
    set(CLANG_COMPILER ON) 
    add_compile_definitions( USING_CLANG )
    message("Using Clang compiler.")

    if( CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" )
        set(CLANG_MSVC_FRONTEND ON)
        add_compile_definitions( USING_MSVC_FRONTEND )
        message("Using MSVC frontend for the Clang compiler.")
    endif()
elseif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
    message("Using GNU compiler")
    set(GNU_COMPILER ON) 
    add_compile_definitions( USING_GCC )
else()
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}. Please use MSVC, Clang or GCC.")
endif()

if( WINDOWS_OS_BUILD )
    message("Detected Windows Build")
    add_compile_definitions( IS_WINDOWS_BUILD )
elseif( LINUX_OS_BUILD )
    message("Detected Linux Build")
    add_compile_definitions( IS_LINUX_BUILD )
elseif( MAC_OS_BUILD )
    message("Detected MacOS Build")
    add_compile_definitions( IS_MACOS_BUILD )
else()
    message(FATAL_ERROR "Unknown target operating system!")
endif()

if( ENABLE_OPTICK_PROFILER )
    add_compile_definitions( ENABLE_OPTICK_PROFILER )
    message("Enabling Optick sampling!")
else()
    message("Optick sampling disabled!")
endif()

if( ENABLE_FUNCTION_PROFILING )
    add_compile_definitions( ENABLE_FUNCTION_PROFILING )
    message("Enabling internal function sampling!")
else()
    message("Disabling internal function sampling!")
endif()

if( ENABLE_MIMALLOC )
    add_compile_definitions( ENABLE_MIMALLOC )
    message("Using mimalloc allocator.")
endif()

if( DIVIDE_STARTUP_TYPE STREQUAL "IN_EDITOR" )
    message("Target is an editor build.")
    add_compile_definitions( _START_IN_EDITOR )
endif()

Toggle_Available_Vector_Extensions()

if( MSVC_COMPILER OR CLANG_MSVC_FRONTEND )
    set( CMAKE_DEBUG_PREFIX /D )
    set( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /O2" )
    set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2" )

    add_compile_definitions( _DISABLE_STRING_ANNOTATION )
    add_compile_definitions( _DISABLE_VECTOR_ANNOTATION )
    add_compile_definitions( _CRT_SECURE_NO_WARNINGS )

    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:/Zm256>
        $<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
    )

    set(MSVC_DEBUG_COMPILE_OPTIONS
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
    )

    set(MSVC_DEBUG_LINK_OPTIONS
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Debug>:/INCREMENTAL:YES>
        $<$<CONFIG:Debug>:/OPT:NOREF>
    )

    set(MSVC_RELWITHDEBINFO_COMPILE_OPTIONS
        $<$<CONFIG:RelWithDebInfo>:/Ox>
        $<$<CONFIG:RelWithDebInfo>:/Zi>
    )

    set(MSVC_RELWITHDEBINFO_LINK_OPTIONS
        $<$<CONFIG:RelWithDebInfo>:/OPT:REF>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG>
        $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL:NO>
    )

    set(MSVC_RELEASE_COMPILE_OPTIONS
        $<$<CONFIG:Release>:/Ox>
        $<$<CONFIG:Release>:/Ob2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:Release>:/Oy>
        $<$<CONFIG:Release>:/GL>
    )

    set(MSVC_RELEASE_LINK_OPTIONS
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
    )

    if(MSVC_COMPILER)
        add_compile_options(
            $<$<CONFIG:Debug>:/d2Obforceinline>
        )

        # RelWithDebInfo: either dynamic-deopt mode or LTCG mode
        if (ENABLE_DYNAMIC_DEOPTIMISATION AND NOT RUN_ASAN AND NOT RUN_UBSAN AND NOT IS_GITHUB_ACTIONS)
            list(APPEND MSVC_RELWITHDEBINFO_COMPILE_OPTIONS
                $<$<CONFIG:RelWithDebInfo>:/GL->
                $<$<CONFIG:RelWithDebInfo>:/dynamicdeopt>
            )
            list(APPEND MSVC_RELWITHDEBINFO_LINK_OPTIONS
                $<$<CONFIG:RelWithDebInfo>:/dynamicdeopt>
                $<$<CONFIG:RelWithDebInfo>:/OPT:NOICF>
            )
        else()
            list(APPEND MSVC_RELWITHDEBINFO_COMPILE_OPTIONS
                $<$<CONFIG:RelWithDebInfo>:/GL>
            )
            list(APPEND MSVC_RELWITHDEBINFO_LINK_OPTIONS
                $<$<CONFIG:RelWithDebInfo>:/LTCG>
                $<$<CONFIG:RelWithDebInfo>:/OPT:ICF>
            )
        endif()
    endif()

    # Append function-profiling flag if requested
    if( ENABLE_FUNCTION_PROFILING )
        list(APPEND MSVC_RELWITHDEBINFO_LINK_OPTIONS $<$<CONFIG:RelWithDebInfo>:/PROFILE>)
    endif()

    # Apply options only to application targets defined in APP_BUILD_TARGETS
    foreach(_t IN LISTS APP_BUILD_TARGETS)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE ${MSVC_DEBUG_COMPILE_OPTIONS} ${MSVC_RELEASE_COMPILE_OPTIONS} ${MSVC_RELWITHDEBINFO_COMPILE_OPTIONS})
            target_link_options(${_t} PRIVATE ${MSVC_DEBUG_LINK_OPTIONS} ${MSVC_RELEASE_LINK_OPTIONS} ${MSVC_RELWITHDEBINFO_LINK_OPTIONS})
        endif()
    endforeach()

    set( CMAKE_VS_JUST_MY_CODE_DEBUGGING $<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_JUST_MY_CODE}>>,ON,OFF> )

    if( SHOW_CONSOLE_WINDOW )
        add_compile_definitions( SHOW_CONSOLE_WINDOW )
    endif()

else() #MSVC_COMPILER

    #non-MSVC frontends (regular clang/gcc)
    set( CMAKE_DEBUG_PREFIX -D )
    set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0" )
    set( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -g -O3" )
    set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g0 -O3" )

endif() #MSVC_COMPILER

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_DEBUG_PREFIX}_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_DEBUG_PREFIX}_RELEASE")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_DEBUG_PREFIX}_PROFILE")

if(BUILD_TESTING_INTERNAL)
    add_compile_definitions( ENABLE_UNIT_TESTING )
    list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")
    include(CTest)
    enable_testing()
endif()

add_subdirectory(Source)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT Divide-Framework)

if( PRINT_ALL_CMAKE_VARIABLES )
    get_cmake_property(_variableNames VARIABLES)
    list (SORT _variableNames)
    foreach (_variableName ${_variableNames})
        message("${_variableName}=${${_variableName}}")
    endforeach()

    foreach(flag ${Vc_ARCHITECTURE_FLAGS})
      message("Architecture flag: " ${flag})
    endforeach()
endif()

message("Extra definitions used for compilation: ")
get_directory_property(_DIR_DEFS COMPILE_DEFINITIONS)
if(_DIR_DEFS)
    message(STATUS "Directory COMPILE_DEFINITIONS:")
  foreach(_d IN LISTS _DIR_DEFS)
    message(STATUS "  ${_d}")
  endforeach()
endif()

