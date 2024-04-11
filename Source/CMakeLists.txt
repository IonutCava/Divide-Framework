set(APP_THIRD_PARTY_LIB Divide-ThirdParty-Lib)
set(APP_LIB_DIVIDE Divide-Framework-Lib)
set(APP_EXE_DIVIDE Divide-Framework)

if(BUILD_TESTING_INTERNAL)
    set(APP_EXE_TEST_PLATFORM "Divide-Test-Platform")
    set(APP_EXE_TEST_ENGINE "Divide-Test-Engine")

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

include_directories( "${CMAKE_SOURCE_DIR}/Source" )
include_directories( "${CMAKE_SOURCE_DIR}/Source/ThirdParty" )

include(CMakeHelpers/ImportPackages.cmake)
include(CMakeHelpers/GlobSources.cmake)

add_library( ${APP_THIRD_PARTY_LIB} STATIC ${THIRD_PARTY_LOCAL_SRC_FILES} ${THIRD_PARTY_FETCH_SRC_FILES} )
target_precompile_headers(${APP_THIRD_PARTY_LIB} PRIVATE 
    "$<$<COMPILE_LANGUAGE:CXX>:stdafx.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:CEGUIIncludes.h>"
)

add_library(${APP_LIB_DIVIDE} STATIC "engineMain.cpp" ${ENGINE_SOURCE_CODE} )
target_link_libraries(${APP_LIB_DIVIDE} ${APP_THIRD_PARTY_LIB})
target_precompile_headers(${APP_LIB_DIVIDE} REUSE_FROM ${APP_THIRD_PARTY_LIB})

add_executable( ${APP_EXE_DIVIDE} "Executable/main.cpp" )
target_precompile_headers(${APP_EXE_DIVIDE} REUSE_FROM ${APP_LIB_DIVIDE})

set(COMMON_LIBS ${APP_LIB_DIVIDE} 
                ${APP_THIRD_PARTY_LIB}
                ${EXTERNAL_LIBS}
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

if(BUILD_TESTING_INTERNAL)
    include(CTest)
    include(Catch)

    add_custom_target(${APP_EXE_DIVIDE}BinGenerated DEPENDS ${APP_EXE_DIVIDE})

    add_executable( ${APP_EXE_TEST_PLATFORM} "UnitTests/main.cpp" ${TEST_PLATFORM_SOURCE})
    target_link_libraries( ${APP_EXE_TEST_PLATFORM} PRIVATE ${COMMON_LIBS} PRIVATE Catch2::Catch2 )
    target_compile_definitions( ${APP_EXE_TEST_PLATFORM} PUBLIC PLATFORM_TESTS )
    add_dependencies(${APP_EXE_TEST_PLATFORM} ${APP_EXE_DIVIDE}BinGenerated)
    target_precompile_headers(${APP_EXE_TEST_PLATFORM} REUSE_FROM ${APP_LIB_DIVIDE})
    catch_discover_tests( ${APP_EXE_TEST_PLATFORM} WORKING_DIRECTORY  "${CMAKE_SOURCE_DIR}" )

    add_executable( ${APP_EXE_TEST_ENGINE} "UnitTests/main.cpp" ${TEST_ENGINE_SOURCE})
    target_link_libraries( ${APP_EXE_TEST_ENGINE} PRIVATE ${COMMON_LIBS} PRIVATE Catch2::Catch2 )
    target_compile_definitions( ${APP_EXE_TEST_ENGINE} PUBLIC ENGINE_TESTS )
    add_dependencies(${APP_EXE_TEST_ENGINE} ${APP_EXE_DIVIDE}BinGenerated)
    target_precompile_headers(${APP_EXE_TEST_ENGINE} REUSE_FROM ${APP_LIB_DIVIDE})
    catch_discover_tests( ${APP_EXE_TEST_ENGINE} WORKING_DIRECTORY  "${CMAKE_SOURCE_DIR}" )
endif()

foreach( APP_EXE ${APP_BUILD_EXES} )
    target_link_directories( ${APP_EXE} PRIVATE "${cegui_BINARY_DIR}/lib" )
    set_target_properties(${APP_EXE} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()
