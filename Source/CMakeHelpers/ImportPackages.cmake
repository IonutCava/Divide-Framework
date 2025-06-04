set(IMGUI_USER_CONFIG_PATH "${CMAKE_SOURCE_DIR}/Source/Core/Headers/ImGUICustomConfig.h")

add_compile_definitions(IL_STATIC_LIB)
add_compile_definitions(BOOST_EXCEPTION_DISABLE)
add_compile_definitions(GLBINDING_STATIC_DEFINE)
add_compile_definitions(GLBINDING_AUX_STATIC_DEFINE)
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)
add_compile_definitions(HAVE_M_PI)
add_compile_definitions(EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED=1)
add_compile_definitions(IMGUI_USER_CONFIG=\"${IMGUI_USER_CONFIG_PATH}\")

if (ENABLE_MIMALLOC)
    find_package(mimalloc CONFIG REQUIRED)
    add_compile_definitions(EASTL_USER_DEFINED_ALLOCATOR)
endif()

find_package(PkgConfig REQUIRED)
find_package(Stb REQUIRED)
find_package(Freetype REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-concurrentqueue CONFIG REQUIRED)
find_package(DevIL REQUIRED)
find_package(EASTL CONFIG REQUIRED)
find_package(glbinding CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(expat CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(unofficial-imgui-node-editor CONFIG REQUIRED)
find_package(Vulkan)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(ctre CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)

if(MAC_OS_BUILD)
    find_package(date CONFIG REQUIRED)
endif()

if(LINUX_OS_BUILD)
    include(CMakeHelpers/PlatformHelpers/FindWayland.cmake)
endif()

find_path(SIMPLEINI_INCLUDE_DIRS "ConvertUTF.c")
find_path(expat_INCLUDE_DIR "expat.h")

set(NVTT_LIBRARIES "")

if(NOT MAC_OS_BUILD)
    if(NOT LINUX_OS_BUILD)
        find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)
        include_directories( SYSTEM ${OMNIVERSE-PHYSX-SDK_INCLUDE_DIRS} )
    endif()

    find_path(NVTT_INCLUDE_DIRS NAMES nvtt.h PATH_SUFFIXES nvtt)

    find_library(NVTT_LIBRARY_RELEASE NAMES nvtt PATH_SUFFIXES nvtt static shared)
    find_library(NVTT_LIBRARY_DEBUG NAMES nvtt_d PATH_SUFFIXES nvtt static shared)

    find_library(NVIMAGE_LIBRARY_RELEASE NAMES nvimage PATH_SUFFIXES nvtt static shared)
    find_library(NVIMAGE_LIBRARY_DEBUG NAMES nvimage_d PATH_SUFFIXES nvtt static shared)

    find_library(NVMATH_LIBRARY_RELEASE NAMES nvmath PATH_SUFFIXES nvtt static shared)
    find_library(NVMATH_LIBRARY_DEBUG NAMES nvmath_d PATH_SUFFIXES nvtt static shared)

    find_library(NVCORE_LIBRARY_RELEASE NAMES nvcore PATH_SUFFIXES nvtt static shared)
    find_library(NVCORE_LIBRARY_DEBUG NAMES nvcore_d PATH_SUFFIXES nvtt static shared)

    find_library(NVTHREAD_LIBRARY_RELEASE NAMES nvthread PATH_SUFFIXES nvtt static shared)
    find_library(NVTHREAD_LIBRARY_DEBUG NAMES nvthread_d PATH_SUFFIXES nvtt static shared)

    find_library(NVSQUISH_LIBRARY_RELEASE NAMES nvsquish squish PATH_SUFFIXES nvtt static shared)
    find_library(NVSQUISH_LIBRARY_DEBUG NAMES nvsquish_d squish_d PATH_SUFFIXES nvtt static shared)

    find_library(NVBC6H_LIBRARY_RELEASE NAMES bc6h PATH_SUFFIXES nvtt static shared)
    find_library(NVBC6H_LIBRARY_DEBUG NAMES bc6h_d PATH_SUFFIXES nvtt static shared)

    find_library(NVBC7_LIBRARY_RELEASE NAMES bc7 PATH_SUFFIXES nvtt static shared)
    find_library(NVBC7_LIBRARY_DEBUG NAMES bc7_d PATH_SUFFIXES nvtt static shared)

    set(LIBS_TO_SETUP "NVTT" "NVIMAGE" "NVCORE" "NVTHREAD" "NVBC7" "NVBC6H" "NVSQUISH" "NVMATH")

    foreach(LIB ${LIBS_TO_SETUP})
        if(${LIB}_LIBRARY_DEBUG)
           set(${LIB}_LIBRARIES  optimized ${${LIB}_LIBRARY_RELEASE} debug ${${LIB}_LIBRARY_DEBUG})
        else(${LIB}_LIBRARY_DEBUG)
           set(${LIB}_LIBRARY_DEBUG ${${LIB}_LIBRARY_RELEASE})
           set(${LIB}_LIBRARIES  optimized  ${${LIB}_LIBRARY_RELEASE} debug ${${LIB}_LIBRARY_DEBUG}) 
        endif(${LIB}_LIBRARY_DEBUG)
    endforeach(LIB ${LIBS_TO_SETUP})

    set(NVTT_LIBRARIES
        ${NVTT_LIBRARIES}
        ${NVIMAGE_LIBRARIES}
        ${NVCORE_LIBRARIES}
        ${NVTHREAD_LIBRARIES}
        ${NVBC7_LIBRARIES}
        ${NVBC6H_LIBRARIES}
        ${NVSQUISH_LIBRARIES}
        ${NVMATH_LIBRARIES}
    )
endif()

find_library(JASPER_LIBRARY NAMES jasper)
find_library(JPEG_LIBRARY NAMES jpeg)
find_library(TIFF_LIBRARY NAMES tiff)
find_library(LZMA_LIBRARY NAMES lzma)

set(IMAGE_LIBRARIES
    ${JASPER_LIBRARY}
    ${JPEG_LIBRARY}
    ${TIFF_LIBRARY}
)

include_directories(
    SYSTEM
    ${Stb_INCLUDE_DIR}
    ${IL_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIR}
    ${SIMPLEINI_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIR}
    ${Boost_INCLUDE_DIR}
    ${expat_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIR}/vma
)

include_directories(
    "ThirdParty/EntityComponentSystem/include/ECS"
    "ThirdParty/EntityComponentSystem/include"
)

include(ThirdParty/CMakeHelpers/ImportPackages.cmake)

set(EXTERNAL_LIBS
    ${NVTT_LIBRARIES}
    ${IL_LIBRARIES}
    ${ILU_LIBRARIES}
    ${ILUT_LIBRARIES}
    ${PYTHON_LIBRARIES}
    ${IMAGE_LIBRARIES}
    ${LZMA_LIBRARY}
    ${CEGUI_LIBRARIES}
    fmt::fmt
    OptickCore
    EASTL
    Jolt::Jolt
    OpenAL::OpenAL
    expat::expat
    imgui::imgui
    imguizmo::imguizmo
    assimp::assimp
    ctre::ctre
    unofficial::spirv-reflect
    meshoptimizer::meshoptimizer
    glbinding::glbinding glbinding::glbinding-aux
    vk-bootstrap::vk-bootstrap
    Freetype::Freetype
    Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
    RecastNavigation::Detour
    RecastNavigation::Recast
    RecastNavigation::DebugUtils
    RecastNavigation::DetourCrowd
    SDL3::SDL3
    SDL3_mixer::SDL3_mixer
    SDL3_image::SDL3_image
    glslang::glslang
    glslang::glslang-default-resource-limits
    glslang::SPIRV glslang::SPVRemapper
)

if(WINDOWS_OS_BUILD)
  set(EXTERNAL_LIBS ${EXTERNAL_LIBS}
                    unofficial::omniverse-physx-sdk::sdk)
endif()
if(MAC_OS_BUILD)
    set(EXTERNAL_LIBS ${EXTERNAL_LIBS} date::date date::date-tz)
    add_compile_definitions(USE_HINNANT_DATE)
endif()
if(WAYLAND_FOUND)
    set(EXTERNAL_LIBS ${EXTERNAL_LIBS} ${WAYLAND_LIBRARIES})
    include_directories(${WAYLAND_LIBRARIES})
    add_compile_definitions(HAS_WAYLAND_LIB)
endif()
add_compile_definitions(CEGUI_STATIC)

