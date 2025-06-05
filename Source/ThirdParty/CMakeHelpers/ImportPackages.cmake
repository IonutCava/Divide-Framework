include(FetchContent)

set(BUILD_TESTING OFF)

add_compile_definitions(IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
add_compile_definitions(IMGUI_DISABLE_OBSOLETE_KEYIO)
add_compile_definitions(IMGUI_USE_STB_SPRINTF)

#Optick
FetchContent_Declare(
  optick
  GIT_REPOSITORY https://github.com/bombomby/optick.git
  GIT_TAG        8abd28dee1a4034c973a3d32cd1777118e72df7e
  #GIT_PROGRESS   TRUE
  EXCLUDE_FROM_ALL
)

set(OPTICK_BUILD_CONSOLE_SAMPLE FALSE)
set(OPTICK_BUILD_GUI_APP FALSE)
set(OPTICK_ENABLED TRUE)
set(OPTICK_INSTALL_TARGETS FALSE)
set(OPTICK_USE_D3D12 FALSE)
set(OPTICK_USE_VULKAN TRUE)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

#Skarupke hash maps
FetchContent_Declare(
    skarupke
    GIT_REPOSITORY https://github.com/skarupke/flat_hash_map.git
    GIT_TAG        2c4687431f978f02a3780e24b8b701d22aa32d9c
    #GIT_PROGRESS   TRUE
    EXCLUDE_FROM_ALL
)

#MemoryPool
FetchContent_Declare(
    memory_pool
    GIT_REPOSITORY https://github.com/cacay/MemoryPool.git
    GIT_TAG        1ab4683e38f24940afb397b06a2d86814d898e63
    #GIT_PROGRESS   TRUE
    EXCLUDE_FROM_ALL
)

#TileableVolumeNoise
FetchContent_Declare(
    tileable_volume_noise
    GIT_REPOSITORY https://github.com/IonutCava/TileableVolumeNoise.git
    GIT_TAG        master
    #GIT_PROGRESS   TRUE
    EXCLUDE_FROM_ALL
)

#CurlNoise
FetchContent_Declare(
    curl_noise
    GIT_REPOSITORY https://github.com/IonutCava/CurlNoise.git
    GIT_TAG        master
    #GIT_PROGRESS   TRUE
    EXCLUDE_FROM_ALL
)

#SimpleFileWatcher
FetchContent_Declare(
    simple_file_watcher
    GIT_REPOSITORY https://github.com/jameswynn/simplefilewatcher.git
    GIT_TAG        4bccd086621698f21a6e95f3ff77c559f862184a
    #GIT_PROGRESS   TRUE
    #SYSTEM
    EXCLUDE_FROM_ALL
)

#fcpp
FetchContent_Declare(
    fcpp
    GIT_REPOSITORY https://github.com/IonutCava/fcpp.git
    GIT_TAG        master
    #GIT_PROGRESS   TRUE
    #SYSTEM
    EXCLUDE_FROM_ALL
)

#imgui_club
FetchContent_Declare(
    imgui_club
    GIT_REPOSITORY https://github.com/ocornut/imgui_club.git
    GIT_TAG        da32fb90bfb7e5b2a687b972105cc3b6d3db0321
    #GIT_PROGRESS   TRUE
    EXCLUDE_FROM_ALL
)

#IconFontCppHeaders
FetchContent_Declare(
    icon_font_cpp_headers
    GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
    GIT_TAG        8886c5657bac22b8fee34354871e3ade2a596433
    #GIT_PROGRESS   TRUE
    EXCLUDE_FROM_ALL
)
#chaiscript
set(UNIT_TEST_LIGHT TRUE)
set(BUILD_SAMPLES FALSE)
FetchContent_Declare(
    chaiscript
    GIT_REPOSITORY https://github.com/IonutCava/ChaiScript.git
    GIT_TAG        b9e65ee927e2c461cb197197f407a17488b7a3a3
    #GIT_PROGRESS   TRUE
    SYSTEM
)

#SDL3_mixer
set(SDLMIXER_FLAC OFF)
set(SDLMIXER_GME OFF)
set(SDLMIXER_MOD OFF)
set(SDLMIXER_MIDI OFF)
set(SDLMIXER_OPUS OFF)
#set(SDLMIXER_INSTALL OFF)
FetchContent_Declare(
    SDL3_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG        daf0503cea6d9a521f585d37e785d88c2f066cd0
    #GIT_PROGRESS   TRUE
    #SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(
    optick
    skarupke
    memory_pool
    tileable_volume_noise
    curl_noise
    simple_file_watcher
    fcpp
    imgui_club
    icon_font_cpp_headers
    SDL3_mixer
    chaiscript
)

include(ThirdParty/CMakeHelpers/ImportCEGUI.cmake)

if (BUILD_TESTING_INTERNAL)
    set(BUILD_TESTING ON)
endif()

include_directories(
    SYSTEM
    ${spirv-reflect_SOURCE_DIR}
    ${optick_SOURCE_DIR}/src
    ${vk_bootstrap_SOURCE_DIR}/src
    ${skarupke_SOURCE_DIR}
    ${memory_pool_SOURCE_DIR}
    ${tileable_volume_noise_SOURCE_DIR}
    ${curl_noise_SOURCE_DIR}
    ${simple_file_watcher_SOURCE_DIR}/include
    ${fcpp_SOURCE_DIR}
    ${imgui_club_SOURCE_DIR}
    ${icon_font_cpp_headers_SOURCE_DIR}
    ${chaiscript_SOURCE_DIR}/include
    ${JoltPhysics_SOURCE_DIR}/..
    ${SDL3_mixer_SOURCE_DIR}/include
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
    set_source_files_properties( ${FCPP_SRC_FILES} PROPERTIES COMPILE_FLAGS "-Wno-switch-default -Wno-date-time -Wno-pedantic" )
endif()

set( THIRD_PARTY_FETCH_SRC_FILES ${TILEABLE_VOLUME_NOISE_SRC_FILES}
                                 ${CURL_NOISE_SRC_FILES}
                                 ${SIMPLE_FILE_WATCHER_SRC_FILES}
                                 ${FCPP_SRC_FILES}
)
