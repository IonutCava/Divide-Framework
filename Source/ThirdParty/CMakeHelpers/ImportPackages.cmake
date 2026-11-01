include(FetchContent)

set(BUILD_TESTING OFF)

add_compile_definitions(IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
add_compile_definitions(IMGUI_DISABLE_OBSOLETE_KEYIO)
add_compile_definitions(IMGUI_USE_STB_SPRINTF)

if( MAC_OS_BUILD )
find_package(Iconv REQUIRED)
endif()

if(ENABLE_OPTICK_PROFILER)
    message("Fetching Optick Lib")
    set(OPTICK_ENABLED TRUE CACHE BOOL "Enable profiling with Optick" FORCE)
    set(OPTICK_BUILD_CONSOLE_SAMPLE FALSE CACHE BOOL "Build Optick console sample app" FORCE)
    set(OPTICK_BUILD_GUI_APP FALSE CACHE BOOL "Build Optick gui viewer app" FORCE)
    set(OPTICK_INSTALL_TARGETS FALSE CACHE BOOL "Install Optick targets" FORCE)
    set(OPTICK_USE_D3D12 FALSE CACHE BOOL "Built-in support for DirectX 12" FORCE)
    set(OPTICK_USE_VULKAN TRUE CACHE BOOL "Built-in support for Vulkan" FORCE)

    FetchContent_Declare(
        optick
        GIT_REPOSITORY https://github.com/bombomby/optick.git
        GIT_TAG        8abd28dee1a4034c973a3d32cd1777118e72df7e
        EXCLUDE_FROM_ALL
    )

    if(CLANG_COMPILER)
        set(CMAKE_CXX_FLAGS_OLD "${CMAKE_CXX_FLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-ignored-attributes -Wno-unsafe-buffer-usage -Wno-missing-field-initializers -Wno-static-in-inline")
    endif()

    FetchContent_MakeAvailable(optick)

    if(CLANG_COMPILER)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}")
    endif()
    include_directories(SYSTEM ${optick_SOURCE_DIR}/src)
endif()

# Small, rarely changing libraries vendored under Source/ThirdParty (see Source/ThirdParty/VENDORED.md)
get_filename_component(DIVIDE_VENDORED_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(skarupke_SOURCE_DIR              "${DIVIDE_VENDORED_DIR}/flat_hash_map")
set(memory_pool_SOURCE_DIR           "${DIVIDE_VENDORED_DIR}/MemoryPool")
set(tileable_volume_noise_SOURCE_DIR "${DIVIDE_VENDORED_DIR}/TileableVolumeNoise")
set(curl_noise_SOURCE_DIR            "${DIVIDE_VENDORED_DIR}/CurlNoise")
set(simple_file_watcher_SOURCE_DIR   "${DIVIDE_VENDORED_DIR}/SimpleFileWatcher")
set(fcpp_SOURCE_DIR                  "${DIVIDE_VENDORED_DIR}/fcpp")
set(imgui_club_SOURCE_DIR            "${DIVIDE_VENDORED_DIR}/imgui_club")
set(icon_font_cpp_headers_SOURCE_DIR "${DIVIDE_VENDORED_DIR}/IconFontCppHeaders")

if(NOT WINDOWS_OS_BUILD)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_OLD})
endif()

include(ThirdParty/CMakeHelpers/ImportLargeLibs.cmake)

if (BUILD_TESTING_INTERNAL)
    set(BUILD_TESTING ON)
endif()

include_directories(
    SYSTEM
    ${spirv-reflect_SOURCE_DIR}
    ${vk_bootstrap_SOURCE_DIR}/src
    ${skarupke_SOURCE_DIR}
    ${memory_pool_SOURCE_DIR}
    ${tileable_volume_noise_SOURCE_DIR}
    ${curl_noise_SOURCE_DIR}
    ${simple_file_watcher_SOURCE_DIR}/include
    ${fcpp_SOURCE_DIR}
    ${imgui_club_SOURCE_DIR}
    ${icon_font_cpp_headers_SOURCE_DIR}
    #${SDL3_mixer_SOURCE_DIR}/include
    ${nri_SOURCE_DIR}/include
)

set( TILEABLE_VOLUME_NOISE_SRC_FILES ${tileable_volume_noise_SOURCE_DIR}/TileableVolumeNoise.cpp )

set( CURL_NOISE_SRC_FILES ${curl_noise_SOURCE_DIR}/CurlNoise/Curl.cpp
                          ${curl_noise_SOURCE_DIR}/CurlNoise/Noise.cpp
)

set( SIMPLE_FILE_WATCHER_SRC_FILES ${simple_file_watcher_SOURCE_DIR}/source/FileWatcher.cpp
                                   ${simple_file_watcher_SOURCE_DIR}/source/FileWatcherLinux.cpp
                                   ${simple_file_watcher_SOURCE_DIR}/source/FileWatcherOSX.cpp
                                   ${simple_file_watcher_SOURCE_DIR}/source/FileWatcherWin32.cpp
)

if(NOT MSVC_COMPILER)
    set_source_files_properties( ${SIMPLE_FILE_WATCHER_SRC_FILES} PROPERTIES COMPILE_FLAGS "-Wno-switch-default" )
endif()

set( FCPP_SRC_FILES ${fcpp_SOURCE_DIR}/cpp1.c
                    ${fcpp_SOURCE_DIR}/cpp2.c
                    ${fcpp_SOURCE_DIR}/cpp3.c
                    ${fcpp_SOURCE_DIR}/cpp4.c
                    ${fcpp_SOURCE_DIR}/cpp5.c
                    ${fcpp_SOURCE_DIR}/cpp6.c
                    #${fcpp_SOURCE_DIR}/usecpp.c
)

if (NOT MSVC_COMPILER)
    set_source_files_properties( ${FCPP_SRC_FILES} PROPERTIES COMPILE_FLAGS "-Wno-switch-default -Wno-date-time -Wno-pedantic -Wno-format -Wno-implicit-void-ptr-cast -Wno-c++-keyword" )
endif()

set( THIRD_PARTY_FETCH_SRC_FILES ${TILEABLE_VOLUME_NOISE_SRC_FILES}
                                 ${CURL_NOISE_SRC_FILES}
                                 ${SIMPLE_FILE_WATCHER_SRC_FILES}
                                 ${FCPP_SRC_FILES}
)
