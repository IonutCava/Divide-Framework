set(APP_THIRD_PARTY_LIB Divide-ThirdParty-Lib)
set(APP_LIB_DIVIDE Divide-Framework-Lib)
set(APP_EXE_DIVIDE Divide-Framework)

if(BUILD_TESTING)
    set(APP_EXE_TEST_PLATFORM Divide-Test-Platform)
    set(APP_EXE_TEST_ENGINE Divide-Test-Engine)

    set(APP_BUILD_EXES
        ${APP_EXE_DIVIDE}
        ${APP_EXE_TEST_PLATFORM}
        ${APP_EXE_TEST_ENGINE}
    )
else()
    set(APP_BUILD_EXES ${APP_EXE_DIVIDE})
endif()

set(APP_BUILD_TARGETS
    ${APP_THIRD_PARTY_LIB}
    ${APP_LIB_DIVIDE}
    ${APP_BUILD_EXES}
)

include(CMakeHelpers/ImportPackages.cmake)
include(CMakeHelpers/GlobSources.cmake)

include_directories( ${CMAKE_SOURCE_DIR}/Source )
include_directories( ${CMAKE_SOURCE_DIR}/Source/ThirdParty )

add_library(${APP_THIRD_PARTY_LIB} STATIC ${THIRD_PARTY_LOCAL_SRC_FILES}
                                          ${THIRD_PARTY_FETCH_SRC_FILES}
)

target_include_directories( ${APP_THIRD_PARTY_LIB} PUBLIC ${EXTERNAL_INCLUDES} )

add_library(${APP_LIB_DIVIDE} STATIC "engineMain.cpp"
                                     ${ENGINE_SOURCE_CODE}
)

target_include_directories( ${APP_LIB_DIVIDE} PUBLIC ${EXTERNAL_INCLUDES} )

target_link_libraries(${APP_LIB_DIVIDE} ${APP_THIRD_PARTY_LIB})

add_executable( ${APP_EXE_DIVIDE} "Executable/main.cpp" )

set(COMMON_LIBS ${APP_LIB_DIVIDE} 
                ${APP_THIRD_PARTY_LIB}
                ${EXTERNAL_LIBS}
                fmt::fmt
                OptickCore
                EASTL
                OpenAL::OpenAL
                expat::expat
                imgui::imgui
                assimp::assimp
                imguizmo::imguizmo
                spirv-reflect-static
                meshoptimizer::meshoptimizer
                glbinding::glbinding glbinding::glbinding-aux
                vk-bootstrap::vk-bootstrap
                Freetype::Freetype
                unofficial::omniverse-physx-sdk::sdk
                unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
                Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
                RecastNavigation::Detour RecastNavigation::Recast RecastNavigation::DebugUtils RecastNavigation::DetourCrowd
                $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
                $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
                $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
                unofficial::pcre::pcre unofficial::pcre::pcre16 unofficial::pcre::pcre32 unofficial::pcre::pcrecpp
                glslang::OSDependent glslang::glslang glslang::MachineIndependent glslang::GenericCodeGen glslang::glslang-default-resource-limits glslang::OGLCompiler glslang::SPVRemapper glslang::SPIRV glslang::HLSL
)

target_link_libraries( ${APP_EXE_DIVIDE} PRIVATE ${COMMON_LIBS}
                                         PRIVATE ZLIB::ZLIB
                                         PRIVATE glm::glm
                                         PRIVATE unofficial::concurrentqueue::concurrentqueue
                                         PRIVATE unofficial::imgui-node-editor::imgui-node-editor
)

if(TARGET unofficial::omniverse-physx-sdk::gpu-library)
    if(UNIX)
        # Add rpath setting to find .so libraries on unix based systems
        set_target_properties( ${APP_EXE_DIVIDE} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN" )
    endif()

    add_custom_command(TARGET ${APP_EXE_DIVIDE} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                                                                      $<TARGET_FILE:unofficial::omniverse-physx-sdk::gpu-library>
                                                                                      $<TARGET_FILE_DIR:${APP_EXE_DIVIDE}>
    )
    if(WIN32)
        add_custom_command(TARGET ${APP_EXE_DIVIDE} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                                                                        $<TARGET_FILE:unofficial::omniverse-physx-sdk::gpu-device-library>
                                                                                        $<TARGET_FILE_DIR:${APP_EXE_DIVIDE}>)
    endif()
else()
    message(WARNING "GPU acceleration library target not defined - GPU acceleration will NOT be available!")
endif()

foreach( APP_TARGET ${APP_BUILD_TARGETS} )
    target_precompile_headers(${APP_TARGET} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:stdafx.h>")
endforeach()

foreach( APP_EXE ${APP_BUILD_EXES} )
    target_link_directories( ${APP_EXE} PRIVATE ${cegui_BINARY_DIR}/lib )
    set_target_properties(${APP_EXE} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
    set_target_properties(${APP_EXE} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# Unit test setup
if(BUILD_TESTING)
    add_executable( ${APP_EXE_TEST_PLATFORM} ${TEST_PLATFORM_SOURCE} )
    add_executable( ${APP_EXE_TEST_ENGINE}   ${TEST_ENGINE_SOURCE}   )

    target_include_directories( ${APP_EXE_TEST_PLATFORM} PRIVATE ${unit_test_framework_SOURCE_DIR} )
    target_include_directories( ${APP_EXE_TEST_ENGINE}   PRIVATE ${unit_test_framework_SOURCE_DIR} )

    target_link_libraries( ${APP_EXE_TEST_PLATFORM} PRIVATE ${COMMON_LIBS} )
    target_link_libraries( ${APP_EXE_TEST_ENGINE}   PRIVATE ${COMMON_LIBS} )

    add_test(NAME PlatformTest COMMAND  $<TARGET_FILE:${APP_EXE_TEST_PLATFORM}> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME EngineTest   COMMAND  $<TARGET_FILE:${APP_EXE_TEST_ENGINE}>   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()