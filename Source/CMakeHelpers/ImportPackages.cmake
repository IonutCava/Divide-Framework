set(IMGUI_USER_CONFIG_PATH "${CMAKE_SOURCE_DIR}/Source/Core/Headers/ImGUICustomConfig.h")


add_compile_definitions(IL_STATIC_LIB)
add_compile_definitions(BOOST_EXCEPTION_DISABLE)
add_compile_definitions(GLBINDING_STATIC_DEFINE)
add_compile_definitions(GLBINDING_AUX_STATIC_DEFINE)
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)
add_compile_definitions(EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED=1)
add_compile_definitions(IMGUI_USER_CONFIG=\"${IMGUI_USER_CONFIG_PATH}\")

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
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
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
find_path(CHAISCRIPT_INCLUDE_DIRS "chaiscript/chaiscript.hpp")

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

set(LIBS_TO_SETUP "NVTT" "NVCORE" "NVMATH" "NVIMAGE" "NVTHREAD" "NVBC7" "NVBC6H" "NVSQUISH")

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
    ${NVCORE_LIBRARIES} 
    ${NVMATH_LIBRARIES} 
    ${NVIMAGE_LIBRARIES} 
    ${NVTHREAD_LIBRARIES} 
    ${NVBC7_LIBRARIES} 
    ${NVBC6H_LIBRARIES} 
    ${NVSQUISH_LIBRARIES}
)

include(ThirdParty/CMakeHelpers/ImportPackages.cmake)

set(EXTERNAL_LIBS
    ${IL_LIBRARIES}
    ${ILU_LIBRARIES}
    ${ILUT_LIBRARIES}
    ${NVTT_LIBRARIES}
    ${PYTHON_LIBRARIES}
    ${Boost_LIBRARIES}
    ${CEGUI_LIBRARIES}
)

set(EXTERNAL_INCLUDES
    ${cegui_SOURCE_DIR}/cegui/include
    ${cegui_BINARY_DIR}/cegui/include
    ${vk_bootstrap_SOURCE_DIR}/src
    ${skarupke_SOURCE_DIR}
    ${Stb_INCLUDE_DIR}
    ${IL_INCLUDE_DIR}
    ${VULKAN_INCLUDE_DIRS}
    ${CHAISCRIPT_INCLUDE_DIRS}
    ${SIMPLEINI_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS} 
    ${spirv-reflect_SOURCE_DIR}
    ${OMNIVERSE-PHYSX-SDK_INCLUDE_DIRS}
    "ThirdParty/EntityComponentSystem/include/ECS"
    "ThirdParty/EntityComponentSystem/include"
)