﻿cmake_minimum_required (VERSION 3.28)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file.")

project ( Divide-ProjectManager
          VERSION 0.1
          LANGUAGES C CXX
          HOMEPAGE_URL "http://divide-studio.co.uk"
          DESCRIPTION "A project manager app for the Divide Framework"
)

set(PROJECT_LICENSE_FILE "${CMAKE_SOURCE_DIR}/../LICENSE" CACHE FILEPATH "Path to the license file for this project.")

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)
set(CXX_SCAN_FOR_MODULES Off)

set(FETCHCONTENT_QUIET TRUE)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  add_compile_definitions(USING_MSVC)
  #add_compile_options(/experimental:module)
  set(CMAKE_DEBUG_PREFIX /D)
else ()
  set(CMAKE_DEBUG_PREFIX -D)
endif ()

if(WINDOWS_OS_BUILD)
    add_compile_definitions(IS_WINDOWS_BUILD)
elseif(LINUX_OS_BUILD)
    add_compile_definitions(IS_LINUX_BUILD)
elseif(MAC_OS_BUILD)
    add_compile_definitions(IS_MACOS_BUILD)
else()
    message( FATAL_ERROR "Unknow target operating system!");
endif()

set(IMGUI_USER_CONFIG_PATH "${CMAKE_SOURCE_DIR}/../Source/Core/Headers/ImGUICustomConfig.h")

find_package(imgui CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)

# Add source to this project's executable.
add_executable (ProjectManager
                "ProjectManager.cpp"
                "ProjectManager.h"
                "${CMAKE_SOURCE_DIR}/../Source/Platform/PlatformDataTypes.cpp")

target_include_directories(ProjectManager PRIVATE "${CMAKE_SOURCE_DIR}/../Source")
target_include_directories(ProjectManager PRIVATE "${CMAKE_SOURCE_DIR}/../Source/ThirdParty")
target_link_libraries(ProjectManager PRIVATE 
                                     imgui::imgui
                                     fmt::fmt
                                     $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
                                     $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
)
