set(IMGUI_USER_CONFIG_PATH "${CMAKE_SOURCE_DIR}/Source/Core/Headers/ImGUICustomConfig.h")

add_compile_definitions(IL_STATIC_LIB)
add_compile_definitions(BOOST_EXCEPTION_DISABLE)
add_compile_definitions(GLBINDING_STATIC_DEFINE)
add_compile_definitions(GLBINDING_AUX_STATIC_DEFINE)
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)
add_compile_definitions(EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED=1)
add_compile_definitions(IMGUI_USER_CONFIG=\"${IMGUI_USER_CONFIG_PATH}\")

find_package(Catch2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(DevIL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(expat CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(unofficial-concurrentqueue CONFIG REQUIRED)
find_package(EASTL CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(glbinding CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(unofficial-imgui-node-editor CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(Vulkan)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_NO_WARN_NEW_VERSIONS 1)
find_package(Boost COMPONENTS date_time
                              regex
                              serialization
                              unit_test_framework
                              timer
                    REQUIRED
)

find_path(SIMPLEINI_INCLUDE_DIRS "ConvertUTF.c")
find_path(expat_INCLUDE_DIR "expat.h")

find_path(NVTT_INCLUDE_DIRS NAMES nvtt.h PATH_SUFFIXES nvtt)

find_library(NVTT_LIBRARY NAMES nvtt PATH_SUFFIXES nvtt static shared)

find_library(NVIMAGE_LIBRARY NAMES nvimage PATH_SUFFIXES nvtt static shared)

find_library(NVMATH_LIBRARY NAMES nvmath PATH_SUFFIXES nvtt static shared)

find_library(NVCORE_LIBRARY NAMES nvcore PATH_SUFFIXES nvtt static shared)

find_library(NVTHREAD_LIBRARY NAMES nvthread PATH_SUFFIXES nvtt static shared)

find_library(NVSQUISH_LIBRARY NAMES nvsquish squish PATH_SUFFIXES nvtt static shared)

find_library(NVBC6H_LIBRARY NAMES bc6h PATH_SUFFIXES nvtt static shared)

find_library(NVBC7_LIBRARY NAMES bc7 PATH_SUFFIXES nvtt static shared)

set(NVTT_LIBRARIES
    ${NVTT_LIBRARY}
    ${NVIMAGE_LIBRARY}
    ${NVCORE_LIBRARY}
    ${NVTHREAD_LIBRARY}
    ${NVBC7_LIBRARY}
    ${NVBC6H_LIBRARY}
    ${NVSQUISH_LIBRARY}
    ${NVMATH_LIBRARY}
)

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
    ${Stb_INCLUDE_DIR}
    ${IL_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIR}
    ${SIMPLEINI_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIR}
    ${Boost_INCLUDE_DIR}
    ${OMNIVERSE-PHYSX-SDK_INCLUDE_DIRS}
    ${expat_INCLUDE_DIR}
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
    ${Boost_LIBRARIES}
    ${CEGUI_LIBRARIES}
    ${IMAGE_LIBRARIES}
    ${LZMA_LIBRARY}
)
