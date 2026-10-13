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

add_compile_definitions(JPH_OBJECT_STREAM)
add_compile_definitions(JPH_PROFILE_ENABLED)
add_compile_definitions(JPH_DEBUG_RENDERER)
if( AVX2_OPT )
    add_compile_definitions( JPH_USE_AVX2 )
endif()
if( AVX_OPT )
    add_compile_definitions( JPH_USE_AVX )
endif()
if ( NEON_OPT )
    add_compile_definitions( JPH_USE_NEON )
endif()

if (ENABLE_MIMALLOC)
    find_package(mimalloc CONFIG REQUIRED)
    add_compile_definitions(EASTL_USER_DEFINED_ALLOCATOR)
endif()

find_package(PkgConfig REQUIRED)
find_package(Stb REQUIRED)
find_package(Ktx 4.4.2 EXACT CONFIG REQUIRED)
find_package(PNG 1.6.59 EXACT REQUIRED)
find_package(libjpeg-turbo 3.2.0 EXACT CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
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
find_package(SDL3_mixer CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(SimpleIni CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)

if(MAC_OS_BUILD)
    find_package(date CONFIG REQUIRED)
    if (NEON_OPT)
        find_path(SSE2NEON_INCLUDE_DIRS "sse2neon/sse2neon.h")
        include_directories( SYSTEM ${SSE2NEON_INCLUDE_DIRS})
    endif()
endif()

if(LINUX_OS_BUILD)
    include(CMakeHelpers/PlatformHelpers/FindWayland.cmake)
endif()


find_path(expat_INCLUDE_DIR "expat.h")
find_path(CHAISCRIPT_INCLUDE_DIRS "chaiscript/chaiscript.hpp")

find_library(JASPER_LIBRARY NAMES jasper)
find_library(JPEG_LIBRARY NAMES jpeg)
find_library(JPEG12_LIBRARY NAMES jpeg12)
find_library(TIFF_LIBRARY NAMES tiff)
find_library(LZMA_LIBRARY NAMES lzma)

set(IMAGE_LIBRARIES
    ${JASPER_LIBRARY}
    ${TIFF_LIBRARY}
    ${JPEG_LIBRARY}
)

if(JPEG12_LIBRARY)
    list(APPEND IMAGE_LIBRARIES ${JPEG12_LIBRARY})
endif()

include_directories(
    SYSTEM
    ${Stb_INCLUDE_DIR}
    ${IL_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIR}
    ${PYTHON_INCLUDE_DIR}
    ${Boost_INCLUDE_DIR}
    ${expat_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIR}/vma
    ${CHAISCRIPT_INCLUDE_DIRS}
)

include_directories(
    "ThirdParty/EntityComponentSystem/include/ECS"
    "ThirdParty/EntityComponentSystem/include"
)

include(ThirdParty/CMakeHelpers/ImportPackages.cmake)

set(EXTERNAL_LIBS
    ${IL_LIBRARIES}
    ${ILU_LIBRARIES}
    ${ILUT_LIBRARIES}
    ${PYTHON_LIBRARIES}
    ${IMAGE_LIBRARIES}
    ${LZMA_LIBRARY}
    ${CEGUI_LIBRARIES}
    fmt::fmt
    KTX::ktx
    PNG::PNG
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
    OptickCore
    EASTL
    OpenAL::OpenAL
    imgui::imgui
    imguizmo::imguizmo
    assimp::assimp
    ctre::ctre
    unofficial::spirv-reflect
    meshoptimizer::meshoptimizer
    glbinding::glbinding glbinding::glbinding-aux
    vk-bootstrap::vk-bootstrap
    expat::expat
    Vulkan::Vulkan
    concurrentqueue::concurrentqueue
    GPUOpen::VulkanMemoryAllocator
    RecastNavigation::Detour
    RecastNavigation::Recast
    RecastNavigation::DebugUtils
    RecastNavigation::DetourCrowd
    SDL3::SDL3
    $<IF:$<TARGET_EXISTS:SDL3_mixer::SDL3_mixer>,SDL3_mixer::SDL3_mixer,SDL3_mixer::SDL3_mixer-static>
    glslang::glslang
    glslang::glslang-default-resource-limits
    glslang::SPIRV
    #glslang::SPVRemapper
    SimpleIni::SimpleIni
    Jolt::Jolt
)

if(MAC_OS_BUILD)
    set(EXTERNAL_LIBS ${EXTERNAL_LIBS} date::date date::date-tz)
    add_compile_definitions(USE_HINNANT_DATE)
endif()
if(WAYLAND_FOUND)
    set(EXTERNAL_LIBS ${EXTERNAL_LIBS} ${WAYLAND_LIBRARIES})
    include_directories(${WAYLAND_INCLUDE_DIR})
    add_compile_definitions(HAS_WAYLAND_LIB)
endif()
add_compile_definitions(CEGUI_STATIC)
