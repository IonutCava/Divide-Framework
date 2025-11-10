# CMakeList.txt : Top-level CMake project file, do global configuration and include sub-projects here.
cmake_minimum_required (VERSION 3.29.2)

option(BUILD_TESTING_INTERNAL "Build unit test executables" ON)
option(RUN_TESTING_INTERNAL "Run unit test after every build" ON)
option(ENABLE_JUST_MY_CODE "Enable just my code debugging in MSVC in debug builds (/JMC)" OFF)
option(PRINT_ALL_CMAKE_VARIABLES "Print all of the CMake variables and their values at the end of the configure step. Mostly for debugging." OFF)
option(SHOW_CONSOLE_WINDOW "Show the console window and Win32 builds and direct all log output to it" OFF)
option(ENABLE_MIMALLOC "Use mimalloc to override malloc/free calls" OFF)
option(ENABLE_DYNAMIC_DEOPTIMISATION "Enable Dynamic Deoptimisation" ON)

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

if(DEFINED ENV{GITHUB_ACTIONS})
    set(IS_GITHUB_ACTIONS TRUE)
else()
    set(IS_GITHUB_ACTIONS FALSE)
endif()

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
set(CLANG_MSVC_FRONTEND OFF)
set(EXTRA_COMPILE_FLAGS "")
set(EXTRA_LINK_FLAGS "")
set(EXTRA_DEFINITIONS "")

message("Trying to match compiler for ID: ${CMAKE_CXX_COMPILER_ID}")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    message("Using MSVC compiler")
    set(MSVC_COMPILER ON) 
    list(APPEND EXTRA_DEFINITIONS USING_MSVC)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(CLANG_MSVC_FRONTEND ON)
        message("Using Clang compiler with the MSVC frontend.")
    else()
        message("Using Clang compiler with the regular frontend.")
    endif()
    
    set(CLANG_COMPILER ON) 
    list(APPEND EXTRA_DEFINITIONS USING_CLANG)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    message("Using GNU compiler")
    set(GNU_COMPILER ON) 
    list(APPEND EXTRA_DEFINITIONS USING_GCC)
else()
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}. Please use MSVC, Clang or GCC.")
endif()

if(WINDOWS_OS_BUILD)
    message("Detected Windows Build")
    list(APPEND EXTRA_DEFINITIONS IS_WINDOWS_BUILD)
elseif(LINUX_OS_BUILD)
    message("Detected Linux Build")
    list(APPEND EXTRA_DEFINITIONS IS_LINUX_BUILD)
elseif(MAC_OS_BUILD)
    message("Detected MacOS Build")
    list(APPEND EXTRA_DEFINITIONS IS_MACOS_BUILD)
else()
    message(FATAL_ERROR "Unknown target operating system!")
endif()

if ( ENABLE_OPTICK_PROFILER )
    list(APPEND EXTRA_DEFINITIONS ENABLE_OPTICK_PROFILER)
    message("Enabling Optick sampling!")
else()
    message("Optick sampling disabled!")
endif()

if ( ENABLE_FUNCTION_PROFILING )
    list(APPEND EXTRA_DEFINITIONS ENABLE_FUNCTION_PROFILING)
    message("Enabling internal function sampling!")
else()
    message("Disabling internal function sampling!")
endif()

Toggle_Available_Vector_Extensions()

if (ENABLE_MIMALLOC)
    list(APPEND EXTRA_DEFINITIONS ENABLE_MIMALLOC)
endif()

if (DIVIDE_STARTUP_TYPE STREQUAL "IN_EDITOR")
    list(APPEND EXTRA_DEFINITIONS _START_IN_EDITOR)
endif()

if(WINDOWS_OS_BUILD)
    set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od" )
    set( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /O2" )
    set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2" )
else()
    set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0" )
    set( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -g -O3" )
    set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g0 -O3" )
endif()

if ( RUN_ASAN OR RUN_UBSAN )
    list(APPEND EXTRA_DEFINITIONS DIVIDE_ENABLE_CONVERSION_CHECKS=1)
else()
    list(APPEND EXTRA_DEFINITIONS DIVIDE_ENABLE_CONVERSION_CHECKS=0)
endif() 

if (MSVC_COMPILER OR CLANG_MSVC_FRONTEND)
    set(CMAKE_DEBUG_PREFIX /D)
    list(APPEND EXTRA_DEFINITIONS _DISABLE_STRING_ANNOTATION)
    list(APPEND EXTRA_DEFINITIONS _DISABLE_VECTOR_ANNOTATION)

    set( CMAKE_VS_JUST_MY_CODE_DEBUGGING $<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_JUST_MY_CODE}>>,ON,OFF> )
    if ( RUN_ASAN OR RUN_UBSAN )
        if ( RUN_ASAN )
            message("ASAN enabled!")

            if (MSVC_COMPILER)
                list(APPEND EXTRA_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:/fsanitize=address>")
                list(APPEND EXTRA_LINK_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:/fsanitize=address>")
            else() #MSVC_COMPILER
                list(APPEND EXTRA_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>")

                set(_CLANG_ASAN_LIB "")
                execute_process(
                    COMMAND clang -print-file-name=clang_rt.asan_dynamic-x86_64.lib
                    OUTPUT_VARIABLE _CLANG_ASAN_LIB
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                if (NOT _CLANG_ASAN_LIB)
                    execute_process(
                        COMMAND clang++ -print-file-name=clang_rt.asan_dynamic-x86_64.lib
                        OUTPUT_VARIABLE _CLANG_ASAN_LIB
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                    )
                endif()

                if (_CLANG_ASAN_LIB AND EXISTS "${_CLANG_ASAN_LIB}")
                    list(APPEND _ASAN_LINK_LIBS "${_CLANG_ASAN_LIB}")

                    set(_CLANG_ASAN_THUNK "")
                    execute_process(
                            COMMAND clang -print-file-name=clang_rt.asan_dynamic_runtime_thunk-x86_64.lib
                            OUTPUT_VARIABLE _CLANG_ASAN_THUNK
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET
                    )
                    if(_CLANG_ASAN_THUNK AND EXISTS "${_CLANG_ASAN_THUNK}")
                        list(APPEND _ASAN_LINK_LIBS "${_CLANG_ASAN_THUNK}")
                    endif()

                    if(_ASAN_LINK_LIBS)
                        message(STATUS "Linking ASan runtimes: ${_ASAN_LINK_LIBS}")
                        list(APPEND EXTRA_LINK_FLAGS "${_ASAN_LINK_LIBS}")
                    endif()
                else()
                    message(WARNING "Clang ASan runtime not found. Link may fail. Consider using the clang driver for linking or install LLVM with sanitizer runtimes.")
                endif()
            endif() #MSVC_COMPILER
        else() #RUN_ASAN
            message("ASAN disabled!")
        endif() #RUN_ASAN

        if ( RUN_UBSAN )
            message("UBSAN requested.")

            if (CLANG_MSVC_FRONTEND)
                message("Enabling UBSan for clang-cl frontend.")
                # clang-cl expects '-fsanitize=undefined'
                list(APPEND EXTRA_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>")
                list(APPEND EXTRA_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-sanitize-recover=undefined>")

                # Candidate UBSan runtime names (static import libs shipped with clang)
                set(_UBSAN_CANDIDATES
                    "clang_rt.ubsan_standalone-x86_64.lib"
                    "clang_rt.ubsan_standalone_cxx-x86_64.lib"
                    "clang_rt.ubsan_standalone.lib"
                )

                set(_found_ubsan_lib "")
                foreach(_cand ${_UBSAN_CANDIDATES})
                    execute_process(
                        COMMAND clang -print-file-name=${_cand}
                        OUTPUT_VARIABLE _ubsan_path
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                    )
                    if (_ubsan_path AND EXISTS "${_ubsan_path}")
                        message(STATUS "Found UBSan runtime: ${_ubsan_path}")
                        list(APPEND EXTRA_LINK_FLAGS "${_ubsan_path}")
                        set(_found_ubsan_lib TRUE)
                        break()
                    endif()
                endforeach()

                if (NOT _found_ubsan_lib)
                    # try clang++
                    foreach(_cand ${_UBSAN_CANDIDATES})
                        execute_process(
                            COMMAND clang++ -print-file-name=${_cand}
                            OUTPUT_VARIABLE _ubsan_path2
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET
                        )
                        if (_ubsan_path2 AND EXISTS "${_ubsan_path2}")
                            message(STATUS "Found UBSan runtime via clang++: ${_ubsan_path2}")
                            list(APPEND EXTRA_LINK_FLAGS "${_ubsan_path2}")
                            set(_found_ubsan_lib TRUE)
                            break()
                        endif()
                    endforeach()
                endif()

                if (NOT _found_ubsan_lib)
                    message(WARNING "Clang UBSan runtime not found for clang-cl. Linking may fail. Install LLVM with UBSan runtimes or use the clang driver for linking.")
                endif()
            else() #CLANG_MSVC_FRONTEND
                if (MSVC_COMPILER)
                    message(WARNING "UBSan requested but native MSVC does not support UBSan instrumentation. Use clang-cl or a Clang toolchain on Windows to enable UBSan.")
                else()
                    list(APPEND EXTRA_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>")
                    list(APPEND EXTRA_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-sanitize-recover=undefined>")
                    list(APPEND EXTRA_LINK_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>")
                    list(APPEND EXTRA_LINK_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-sanitize-recover=undefined>")
                endif()
            endif() #CLANG_MSVC_FRONTEND
        else() #RUN_UBSAN
            message("UBSAN disabled!")
        endif() #RUN_UBSAN

    else() # RUN_ASAN OR RUN_UBSAN
        if ( ENABLE_DYNAMIC_DEOPTIMISATION AND NOT IS_GITHUB_ACTIONS)
            set( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /dynamicdeopt" )
            list(APPEND EXTRA_COMPILE_FLAGS "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:RelWithDebInfo>>:/GL->")
            list(APPEND EXTRA_LINK_FLAGS "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:RelWithDebInfo>>:/OPT:NOICF>")
        else()
            message("Skipping /dynamicdeopt for current build")
        endif()
    endif() # RUN_ASAN OR RUN_UBSAN

    if (SHOW_CONSOLE_WINDOW)
        list(APPEND EXTRA_DEFINITIONS SHOW_CONSOLE_WINDOW)
    endif()

else() # non-MSVC frontends (regular clang/gcc)

    set(CMAKE_DEBUG_PREFIX -D)

    if ( RUN_ASAN )
        list(APPEND EXTRA_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>")
        list(APPEND EXTRA_LINK_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>")
        message("ASAN enabled!")
    else()
        message("ASAN disabled!")
    endif ()

    if ( RUN_UBSAN )
        list(APPEND EXTRA_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>")
        list(APPEND EXTRA_LINK_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>")
        message("UBSAN enabled!")
    else()
        message("UBSAN disabled!")
    endif ()

endif () #MSVC_COMPILER

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_DEBUG_PREFIX}_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_DEBUG_PREFIX}_RELEASE")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_DEBUG_PREFIX}_PROFILE")


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
