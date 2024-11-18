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

set(DIVIDE_COMPILE_OPTIONS "")
set(PCH_COMPILE_OPTIONS "")
set(THIRD_PARTY_COMPILE_OPTIONS "")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")

    # Bit of a long one, but this is what happens when projects don't get built with /Wall.
    set(DIVIDE_COMPILE_OPTIONS /Wall
                               /wd4355 #'this' used in base member initializer list. Yup, I know ...
                               /wd4599 #pch mismatch ...
                               /wd4605 #some defines don't really need to be in the PCH and command line at the same time. Some due, sure, but this causes more problems than it solves
                               /wd4668 #sometimes we don't actually define preoprocessor macros and want them to be interpreted as 0
                               $<$<CONFIG:Release>:/wd4711> # automatic inline expansion warning
    )

    set(PCH_COMPILE_OPTIONS /bigobj
                            /wd4005 #_WIN32_WINNT redefinition
                            /wd4100 #unreferecend format parameter
                            /wd4201 #nonstandard extension used: nameless struct/union
                            /wd4310 #cast truncates constant value
                            /wd4624 #destructor was implicitly defines as deleted
                            /wd5031 #pragma push/pop mismatch triggering in some EASTL headers as they split push and pop in separate headers for some things
                            /wd4996 
    )

    # All of these (including the Clang/GCC equivalents) could just be avoided with /W0..3, but I want to be explicit about the warnings I encounter with external libs
    set(THIRD_PARTY_COMPILE_OPTIONS /wd4061 /wd4062 /wd4127 /wd4191 /wd4242 /wd4244 /wd4255
                                    /wd4265 /wd4266 /wd4305 /wd4312 /wd4365 /wd4371 /wd4388
                                    /wd4435 /wd4456 /wd4457 /wd4458 /wd4464 /wd4477 /wd4505
                                    /wd4582 /wd4583 /wd4619 /wd4623 /wd4625 /wd4626 /wd4701
                                    /wd4706 /wd4710 /wd4774 /wd4777 /wd4800 /wd4820 /wd4868
                                    /wd5026 /wd5027 /wd5039 /wd5045 /wd5204 /wd5219 /wd5220
                                    /wd5262 /wd5267
    )

else()

    set(DIVIDE_COMPILE_OPTIONS -Wall
                               -pedantic
    )

    set(THIRD_PARTY_COMPILE_OPTIONS -Wno-unused-but-set-variable
                                    -Wno-parentheses
                                    -Wno-misleading-indentation
                                    -Wno-unused-variable
                                    -Wno-unused-function
                                    -Wno-char-subscripts
                                    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>
    )

    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")

        set(DIVIDE_COMPILE_OPTIONS ${DIVIDE_COMPILE_OPTIONS}  -Wno-macro-redefined           -Wno-clang-cl-pch
                                   -Wno-old-style-cast        -Wno-format-nonliteral         -Wno-c++98-compat
                                   -Wno-c++98-compat-pedantic -Wno-cast-function-type-strict -Wno-global-constructors #Really now?
        )
        set(THIRD_PARTY_COMPILE_OPTIONS ${THIRD_PARTY_COMPILE_OPTIONS}         -Wno-reserved-macro-identifier                -Wno-documentation
                                        -Wno-documentation-unknown-command     -Wno-ignored-attributes                       -Wno-undef
                                        -Wno-cast-qual                         -Wno-cast-align                               -Wno-cast-function-type
                                        -Wno-sign-conversion                   -Wno-float-conversion                         -Wno-string-conversion
                                        -Wno-string-plus-int                   -Wno-double-promotion                         -Wno-unused-member-function
                                        -Wno-conditional-uninitialized         -Wno-implicit-int-conversion                  -Wno-implicit-float-conversion
                                        -Wno-unsafe-buffer-usage               -Wno-covered-switch-default                   -Wno-unused-private-field
                                        -Wno-missing-braces                    -Wno-gnu-zero-variadic-macro-arguments        -Wno-float-equal
                                        -Wno-extra-semi                        -Wno-switch-enum -Wno-shadow-uncaptured-local -Wno-disabled-macro-expansion
                                        -Wno-nullability-extension             -Wno-zero-as-null-pointer-constant            -Wno-suggest-override
                                        -Wno-suggest-destructor-override       -Wno-reserved-identifier                      -Wno-implicit-int-float-conversion
                                        -Wno-unknown-pragmas                   -Wno-strict-prototypes                        -Wno-unused-template
                                        -Wno-unused-macros                     -Wno-shorten-64-to-32                         -Wno-missing-prototypes
                                        -Wno-newline-eof                       -Wno-extra-semi-stmt                          -Wno-comma
                                        -Wno-shadow                            -Wno-microsoft-enum-value                     -Wno-language-extension-token
                                        -Wno-deprecated-dynamic-exception-spec -Wno-exit-time-destructors                    -Wno-shadow-field-in-constructor
                                        -Wno-implicit-fallthrough              -Wno-inconsistent-missing-override            -Wno-inconsistent-missing-destructor-override
                                        -Wno-deprecated-declarations           -Wno-deprecated-copy-with-dtor                -Wno-deprecated-copy-with-user-provided-dtor
                                        -Wno-parentheses-equality              -Wno-return-type-c-linkage                    -Wno-missing-field-initializers
                                        -Wno-nonportable-system-include-path
        )

    else() #GCC
        set(THIRD_PARTY_COMPILE_OPTIONS ${THIRD_PARTY_COMPILE_OPTIONS}
                                        -Wno-format-overflow
        )
    endif()
endif()

add_library( ${APP_THIRD_PARTY_LIB} STATIC ${THIRD_PARTY_LOCAL_SRC_FILES} ${THIRD_PARTY_FETCH_SRC_FILES} )

if(WIN32)
    target_link_libraries(${APP_THIRD_PARTY_LIB} PUBLIC Shcore.lib Winmm.lib)
endif()

target_compile_options(${APP_THIRD_PARTY_LIB} PUBLIC ${DIVIDE_COMPILE_OPTIONS} ${PCH_COMPILE_OPTIONS} ${THIRD_PARTY_COMPILE_OPTIONS})

set(CMAKE_PCH_INSTANTIATE_TEMPLATES ON)
target_precompile_headers(${APP_THIRD_PARTY_LIB} PUBLIC 
    "$<$<COMPILE_LANGUAGE:CXX>:EngineIncludes_pch.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:CEGUIIncludes_pch.h>"
)

add_library(${APP_LIB_DIVIDE} STATIC "engineMain.cpp" ${ENGINE_SOURCE_CODE} )
target_link_libraries(${APP_LIB_DIVIDE} ${APP_THIRD_PARTY_LIB})
target_precompile_headers(${APP_LIB_DIVIDE} REUSE_FROM ${APP_THIRD_PARTY_LIB})
target_compile_options(${APP_LIB_DIVIDE} PUBLIC ${DIVIDE_COMPILE_OPTIONS})

add_executable( ${APP_EXE_DIVIDE} "Executable/main.cpp" )
target_precompile_headers(${APP_EXE_DIVIDE} REUSE_FROM ${APP_LIB_DIVIDE})

set(COMMON_LIBS $<IF:$<TARGET_EXISTS:mimalloc>,mimalloc,mimalloc-static>
                ${APP_LIB_DIVIDE} 
                ${APP_THIRD_PARTY_LIB}
                ${EXTERNAL_LIBS}
)

target_link_libraries( ${APP_EXE_DIVIDE} PUBLIC ${COMMON_LIBS}
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
    include(Catch)

    add_custom_target(${APP_EXE_DIVIDE}BinGenerated DEPENDS ${APP_EXE_DIVIDE})

    add_executable( ${APP_EXE_TEST_PLATFORM} "UnitTests/main.cpp" ${TEST_PLATFORM_SOURCE})
    target_link_libraries( ${APP_EXE_TEST_PLATFORM} PRIVATE ${COMMON_LIBS} PRIVATE Catch2::Catch2 )
    target_compile_definitions( ${APP_EXE_TEST_PLATFORM} PRIVATE PLATFORM_TESTS )
    add_dependencies(${APP_EXE_TEST_PLATFORM} ${APP_EXE_DIVIDE}BinGenerated)
    target_precompile_headers(${APP_EXE_TEST_PLATFORM} REUSE_FROM ${APP_LIB_DIVIDE})

    
    add_executable( ${APP_EXE_TEST_ENGINE} "UnitTests/main.cpp" ${TEST_ENGINE_SOURCE})
    target_link_libraries( ${APP_EXE_TEST_ENGINE} PRIVATE ${COMMON_LIBS} PRIVATE Catch2::Catch2 )
    target_compile_definitions( ${APP_EXE_TEST_ENGINE} PRIVATE ENGINE_TESTS )
    add_dependencies(${APP_EXE_TEST_ENGINE} ${APP_EXE_DIVIDE}BinGenerated)
    target_precompile_headers(${APP_EXE_TEST_ENGINE} REUSE_FROM ${APP_LIB_DIVIDE})

    if(RUN_TESTING_INTERNAL)
        include(CTest)

        catch_discover_tests( ${APP_EXE_TEST_PLATFORM} WORKING_DIRECTORY  "${CMAKE_SOURCE_DIR}" )
        catch_discover_tests( ${APP_EXE_TEST_ENGINE} WORKING_DIRECTORY  "${CMAKE_SOURCE_DIR}" )
    endif()

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        target_compile_options(${APP_EXE_TEST_PLATFORM} PRIVATE /wd4866 /wd4868)
        target_compile_options(${APP_EXE_TEST_ENGINE} PRIVATE /wd4866 /wd4868)
    endif()
endif()

foreach( APP_EXE ${APP_BUILD_EXES} )
    target_link_directories( ${APP_EXE} PRIVATE "${cegui_BINARY_DIR}/lib" )
    set_target_properties(${APP_EXE} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    target_compile_options(${APP_EXE} PRIVATE ${DIVIDE_COMPILE_OPTIONS} ${EXTRA_COMPILE_FLAGS})
    target_link_options(${APP_EXE} PRIVATE ${EXTRA_LINK_FLAGS})
endforeach()
