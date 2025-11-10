set(APP_LIB_DIVIDE "Divide-Framework-Lib")
set(APP_EXE_DIVIDE "Divide-Framework")
set(APP_EXE_PROJECT_MANAGER "Divide-Project-Manager")

set(APP_BUILD_EXES ${APP_EXE_DIVIDE} ${APP_EXE_PROJECT_MANAGER} )

if(BUILD_TESTING_INTERNAL)
    set(APP_EXE_TEST_PLATFORM "Divide-Test-Platform")
    set(APP_EXE_TEST_ENGINE "Divide-Test-Engine")

    list(APPEND APP_BUILD_EXES ${APP_EXE_TEST_PLATFORM} ${APP_EXE_TEST_ENGINE} )
endif()

set(APP_BUILD_TARGETS ${APP_LIB_DIVIDE} ${APP_BUILD_EXES} )
 
include_directories( "${CMAKE_SOURCE_DIR}/Source" )
include_directories( "${CMAKE_SOURCE_DIR}/Source/ThirdParty" )

set(DIVIDE_CXX_STANDARD 0)
set(DIVIDE_HAS_CXX20 0)
set(DIVIDE_HAS_CXX23 0)
set(DIVIDE_HAS_CXX26 0)

if(DEFINED CMAKE_CXX_STANDARD)
  set(DIVIDE_CXX_STANDARD ${CMAKE_CXX_STANDARD})
endif()

if(DIVIDE_CXX_STANDARD GREATER_EQUAL 20)
  set(DIVIDE_HAS_CXX20 1)
endif()

if(DIVIDE_CXX_STANDARD GREATER_EQUAL 23)
  set(DIVIDE_HAS_CXX23 1)
endif()

if(DIVIDE_CXX_STANDARD GREATER_EQUAL 26)
  set(DIVIDE_HAS_CXX26 1)
endif()

configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/CMakeHelpers/CXXConfig.h.in"
  "${CMAKE_BINARY_DIR}/generated/CXXConfig.h"
  @ONLY
)
include_directories("${CMAKE_BINARY_DIR}/generated")

include(CMakeHelpers/ImportPackages.cmake)
include(CMakeHelpers/GlobSources.cmake)

set(DIVIDE_COMPILE_OPTIONS "")
set(PCH_COMPILE_OPTIONS "")
set(THIRD_PARTY_COMPILE_OPTIONS "")

if (MSVC_COMPILER)

   list(APPEND EXTRA_DEFINITIONS "$<$<COMPILE_LANGUAGE:C,CXX>:_CRT_SECURE_NO_WARNINGS>")
   list(APPEND EXTRA_LINK_FLAGS "$<$<CONFIG:Debug>:/OPT:NOREF>")
   list(APPEND EXTRA_LINK_FLAGS "$<IF:$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Debug>>,/INCREMENTAL,/INCREMENTAL:NO>")
   
   if ( ENABLE_FUNCTION_PROFILING )
       list(APPEND EXTRA_LINK_FLAGS "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:RelWithDebInfo>>:/PROFILE>")
   endif()

   # Bit of a long one, but this is what happens when projects don't get built with /Wall.
   list(APPEND DIVIDE_COMPILE_OPTIONS /Wall
                                      /wd4355 #'this' used in base member initializer list. Yup, I know ...
                                      /wd4599 #pch mismatch ...
                                      /wd4605 #some defines don't really need to be in the PCH and command line at the same time. Some due, sure, but this causes more problems than it solves
                                      /wd4668 #sometimes we don't actually define preoprocessor macros and want them to be interpreted as 0
                                      $<$<CONFIG:Release>:/wd4711> # automatic inline expansion warning
                                      $<$<COMPILE_LANGUAGE:C,CXX>:/Zm256>
                                      $<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>
                                      $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
                                      $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Debug>>:/d2Obforceinline>
   )                                  

   list(APPEND DIVIDE_COMPILE_OPTIONS "$<$<CONFIG:Debug,RelWithDebInfo>:/Zi>")

   list(APPEND PCH_COMPILE_OPTIONS /bigobj
                                   /wd4005 #_WIN32_WINNT redefinition
                                   /wd4100 #unreferecend format parameter
                                   /wd4201 #nonstandard extension used: nameless struct/union
                                   /wd4310 #cast truncates constant value
                                   /wd4624 #destructor was implicitly defines as deleted
                                   /wd5031 #pragma push/pop mismatch triggering in some EASTL headers as they split push and pop in separate headers for some things
                                   /wd4996 
    )

    # All of these (including the Clang/GCC equivalents) could just be avoided with /W0..3, but I want to be explicit about the warnings I encounter with external libs
    list(APPEND THIRD_PARTY_COMPILE_OPTIONS /wd4061 /wd4062 /wd4127 /wd4191 /wd4242 /wd4244 /wd4255
                                            /wd4265 /wd4266 /wd4305 /wd4312 /wd4365 /wd4371 /wd4388
                                            /wd4100 /wd4267 /wd4435 /wd4456 /wd4457 /wd4458 /wd4464
                                            /wd4477 /wd4505 /wd4582 /wd4583 /wd4619 /wd4623 /wd4625
                                            /wd4626 /wd4701 /wd4706 /wd4710 /wd4774 /wd4777 /wd4800
                                            /wd4820 /wd4868 /wd5026 /wd5027 /wd5039 /wd5045 /wd5204
                                            /wd5219 /wd5220 /wd5262 /wd5267 /wd5045
    )

else()

    list(APPEND DIVIDE_COMPILE_OPTIONS -Wall
                                       -pedantic
                                       $<$<COMPILE_LANGUAGE:C,CXX>:-pthread>
                                       $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++17-extensions>
    )

    list(APPEND THIRD_PARTY_COMPILE_OPTIONS -Wno-unused-but-set-variable -Wno-parentheses
                                            -Wno-misleading-indentation  -Wno-unused-variable
                                            -Wno-unused-function         -Wno-char-subscripts
                                            $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>
    )

    if(CLANG_COMPILER)

        list(APPEND DIVIDE_COMPILE_OPTIONS -Wno-macro-redefined       -Wno-clang-cl-pch
                                           -Wno-old-style-cast        -Wno-format-nonliteral
                                           -Wno-c++98-compat          -Wno-c++98-compat-pedantic
                                           -Wno-global-constructors #Really now?
                                           $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wno-cast-function-type-strict>
                                           $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unknown-argument>
                                           $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-undefined-func-template>
                                           $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-command-line-argument>
                                           $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-nullability-completeness>
                                           $<$<COMPILE_LANGUAGE:C,CXX>:-fdeclspec>
        )

        if(LINUX_OS_BUILD)
            list(APPEND EXTRA_LINK_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fuse-ld=lld>")
        endif()

        list(APPEND THIRD_PARTY_COMPILE_OPTIONS -Wno-reserved-macro-identifier         -Wno-documentation                            -Wno-nonportable-system-include-path
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
        )

    elseif(GNU_COMPILER)
        list(APPEND THIRD_PARTY_COMPILE_OPTIONS -Wno-format-overflow
                                                -Wno-unused-result
                                                -Wno-stringop-truncation
        )

        #Sadly, with array-bounds set to default, anything that uses EASTL will spam the build log
        list(APPEND DIVIDE_COMPILE_OPTIONS -Warray-bounds=0)
    else()
        message( FATAL_ERROR "Unknow compiler type")
    endif()
endif()

list(APPEND DIVIDE_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:CXX,C>:${EXTRA_COMPILE_FLAGS}>")

add_library(${APP_LIB_DIVIDE} STATIC "EngineMain.cpp" 
                                     ${ENGINE_SOURCE_CODE}
                                     ${THIRD_PARTY_LOCAL_SRC_FILES}
                                     ${THIRD_PARTY_FETCH_SRC_FILES}
)

target_link_libraries(${APP_LIB_DIVIDE} PUBLIC Jolt)
target_link_libraries(${APP_LIB_DIVIDE} PUBLIC NRI)

if(WIN32)
    target_link_libraries(${APP_LIB_DIVIDE} PUBLIC Shcore.lib Winmm.lib)
endif()

target_compile_options(${APP_LIB_DIVIDE} PUBLIC ${DIVIDE_COMPILE_OPTIONS}
                                                ${PCH_COMPILE_OPTIONS}
                                                ${THIRD_PARTY_COMPILE_OPTIONS})

target_link_options(${APP_LIB_DIVIDE} PRIVATE ${EXTRA_LINK_FLAGS})
target_compile_definitions(${APP_LIB_DIVIDE} PRIVATE ${EXTRA_DEFINITIONS})

set(CMAKE_PCH_INSTANTIATE_TEMPLATES ON)
    target_precompile_headers(${APP_LIB_DIVIDE} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:EngineIncludes_pch.h>"
                                                       "$<$<COMPILE_LANGUAGE:CXX>:CEGUIIncludes_pch.h>"
)

add_executable( ${APP_EXE_DIVIDE} "Executable/main.cpp" )
target_precompile_headers(${APP_EXE_DIVIDE} REUSE_FROM ${APP_LIB_DIVIDE})

if (ENABLE_MIMALLOC)
    target_compile_definitions( ${APP_EXE_DIVIDE} PUBLIC MIMALLOC_OVERRIDE_NEW_DELETE)
endif()

set(COMMON_LIBS ${APP_LIB_DIVIDE} 
                ${EXTERNAL_LIBS}
)

if (ENABLE_MIMALLOC)
    list(APPEND COMMON_LIBS $<IF:$<TARGET_EXISTS:mimalloc>,mimalloc,mimalloc-static>)
endif()

target_link_libraries( ${APP_EXE_DIVIDE} PUBLIC ${COMMON_LIBS}
                                         PRIVATE ZLIB::ZLIB
                                         PRIVATE glm::glm
                                         PRIVATE unofficial::imgui-node-editor::imgui-node-editor
)


if(WINDOWS_OS_BUILD)
    if ( RUN_ASAN OR RUN_UBSAN )
        include(CMakeHelpers/PlatformHelpers/FindASANDLLs.cmake)
        if (SANITIZER_DLLS)
            message(STATUS "Sanitizer DLLs found and will be copied to executables: ${SANITIZER_DLLS}")

            foreach(APP_EXE IN LISTS APP_BUILD_EXES)
                if (TARGET ${APP_EXE})
                    foreach(DLL IN LISTS SANITIZER_DLLS)
                        add_custom_command(TARGET ${APP_EXE} POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "$<TARGET_FILE_DIR:${APP_EXE}>"
                            COMMENT "Copy sanitizer runtime ${DLL} -> $<TARGET_FILE_DIR:${APP_EXE}>"
                        )
                    endforeach()
                endif()
            endforeach()
        else()
            message(WARNING "Sanitizer runtimes not found — executables built with ASAN/UBSAN may fail to start.")
        endif()
    endif()

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
                                                                                            $<TARGET_FILE_DIR:${APP_EXE_DIVIDE}>
            )
        endif()
    else()
        message(WARNING "GPU acceleration library target not defined - GPU acceleration will NOT be available!")
    endif()


endif()

add_custom_target(${APP_EXE_DIVIDE}BinGenerated DEPENDS ${APP_EXE_DIVIDE})

if(BUILD_TESTING_INTERNAL)
    include(Catch)

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

    if (MSVC_COMPILER)
        target_compile_options(${APP_EXE_TEST_PLATFORM} PRIVATE /wd4866 /wd4868)
        target_compile_options(${APP_EXE_TEST_ENGINE} PRIVATE /wd4866 /wd4868)
    endif()
endif()

add_executable (${APP_EXE_PROJECT_MANAGER} "ProjectManager/ProjectManager.cpp"
                                           "ProjectManager/ProjectManager.h"
                                           "Platform/PlatformDataTypes.cpp"
)

target_link_libraries(${APP_EXE_PROJECT_MANAGER} PRIVATE ${COMMON_LIBS}
                                                         imgui::imgui
                                                         fmt::fmt
                                                         SDL3::SDL3
                                                         SDL3_image::SDL3_image
)

add_dependencies(${APP_EXE_PROJECT_MANAGER} ${APP_EXE_DIVIDE}BinGenerated)

target_precompile_headers(${APP_EXE_PROJECT_MANAGER} REUSE_FROM ${APP_LIB_DIVIDE})


foreach( APP_EXE ${APP_BUILD_EXES} )
    target_compile_options(${APP_EXE} PRIVATE ${DIVIDE_COMPILE_OPTIONS})
    target_link_directories( ${APP_EXE} PRIVATE "${cegui_BINARY_DIR}/lib" )
    target_link_options(${APP_EXE} PRIVATE ${EXTRA_LINK_FLAGS})
    target_compile_definitions(${APP_EXE} PRIVATE ${EXTRA_DEFINITIONS})

    set_target_properties(${APP_EXE} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()
