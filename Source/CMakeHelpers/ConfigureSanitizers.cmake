if( RUN_ASAN )
    message("ASAN enabled!")
else()
    message("ASAN disabled!")
endif()

if( RUN_UBSAN )
    message("UBSAN enabled!")
else()
    message("UBSAN disabled!")
endif()

if( RUN_ASAN OR RUN_UBSAN )
    include(CheckCXXCompilerFlag)

    set(SAN_COMPILE_FLAGS "")
    set(SAN_LINK_FLAGS "")
    set(CLANG_ASAN_LIB "")
    set(CLANG_ASAN_THUNK "")
    set(FOUND_UBSAN_LIB "")

    list(APPEND EXTRA_DEFINITIONS DIVIDE_ENABLE_CONVERSION_CHECKS=1)

    if( RUN_ASAN )
        list(APPEND EXTRA_DEFINITIONS DIVIDE_ASAN_REQUESTED)

        if( MSVC_COMPILER )
            list(APPEND SAN_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:/fsanitize=address>")
            list(APPEND SAN_LINK_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:/fsanitize=address>")
        else() #MSVC_COMPILER
            check_cxx_compiler_flag("-fsanitize=address" HAVE_FSAN_ADDRESS)
            if( HAVE_FSAN_ADDRESS )
                list(APPEND SAN_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>")
                list(APPEND SAN_LINK_FLAGS    "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>")

                check_cxx_compiler_flag("-fno-omit-frame-pointer" HAVE_FRAME_PTR)
                if( HAVE_FRAME_PTR )
                    list(APPEND SAN_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-omit-frame-pointer>")
                endif() #HAVE_FRAME_PTR
            endif() #HAVE_FSAN_ADDRESS

            if( GNU_COMPILER )
                check_cxx_compiler_flag("-static-libasan" HAVE_STATIC_LIBASAN)
                if( HAVE_STATIC_LIBASAN )
                    list(APPEND SAN_LINK_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-static-libasan>")
                endif() #HAVE_STATIC_LIBASAN
            endif() #GNU_COMPILER

            if( CLANG_MSVC_FRONTEND )
                execute_process(
                    COMMAND clang -print-file-name=clang_rt.asan_dynamic-x86_64.lib
                    OUTPUT_VARIABLE CLANG_ASAN_LIB
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )

                if( NOT CLANG_ASAN_LIB )
                    execute_process(
                        COMMAND clang++ -print-file-name=clang_rt.asan_dynamic-x86_64.lib
                        OUTPUT_VARIABLE CLANG_ASAN_LIB
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                    )
                endif() #!CLANG_ASAN_LIB

                if( CLANG_ASAN_LIB AND EXISTS "${CLANG_ASAN_LIB}" )
                    list(APPEND ASAN_LINK_LIBS "${CLANG_ASAN_LIB}")

                    execute_process(
                            COMMAND clang -print-file-name=clang_rt.asan_dynamic_runtime_thunk-x86_64.lib
                            OUTPUT_VARIABLE CLANG_ASAN_THUNK
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET
                    )

                    if( CLANG_ASAN_THUNK AND EXISTS "${CLANG_ASAN_THUNK}" )
                        list(APPEND ASAN_LINK_LIBS "${CLANG_ASAN_THUNK}")
                    endif()

                    message(STATUS "Linking ASan runtimes: ${ASAN_LINK_LIBS}")
                    list(APPEND EXTRA_LINK_FLAGS "${ASAN_LINK_LIBS}")

                else() #CLANG_ASAN_LIB
                    message(WARNING "Clang ASan runtime not found. Link may fail. Consider using the clang driver for linking or install LLVM with sanitizer runtimes.")
                endif() #CLANG_ASAN_LIB
            endif() #CLANG_MSVC_FRONTEND
        endif() #MSVC_COMPILER
    endif() #RUN_ASAN

    if( RUN_UBSAN )
        if( MSVC_COMPILER )
            message("UBSAN is requested but not supported with the MSVC compiler. Please use clang-cl or a Clang toolchain on Windows.")
        else() #MSVC_COMPILER
            list(APPEND EXTRA_DEFINITIONS DIVIDE_UBSAN_REQUESTED)

            check_cxx_compiler_flag("-fsanitize=undefined" HAVE_FSAN_UB)
            if (HAVE_FSAN_UB)
                list(APPEND SAN_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>")
                list(APPEND SAN_LINK_FLAGS    "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=undefined>")

                check_cxx_compiler_flag("-fno-sanitize-recover=all" HAVE_NO_SAN_RECOVER)
                if (HAVE_NO_SAN_RECOVER)
                    list(APPEND SAN_COMPILE_FLAGS "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-sanitize-recover=all>")
                    list(APPEND SAN_LINK_FLAGS    "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-sanitize-recover=all>")
                endif() #HAVE_NO_SAN_RECOVER
            endif() #HAVE_FSAN_UB

            if( CLANG_MSVC_FRONTEND )
                set(UBSAN_CANDIDATES "clang_rt.ubsan_standalone-x86_64.lib"
                                     "clang_rt.ubsan_standalone_cxx-x86_64.lib"
                                     "clang_rt.ubsan_standalone.lib")

                foreach(CANDIDATE ${UBSAN_CANDIDATES})
                    execute_process(
                        COMMAND clang -print-file-name=${CANDIDATE}
                        OUTPUT_VARIABLE UBSAN_PATH
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                    )

                    if (UBSAN_PATH AND EXISTS "${UBSAN_PATH}")
                        message(STATUS "Found UBSan runtime: ${UBSAN_PATH}")
                        list(APPEND EXTRA_LINK_FLAGS "${UBSAN_PATH}")
                        set(FOUND_UBSAN_LIB TRUE)
                        break()
                    endif() #UBSAN_PATH
                endforeach()

                if( NOT FOUND_UBSAN_LIB )
                    foreach(CANDIDATE ${UBSAN_CANDIDATES})
                        execute_process(
                            COMMAND clang++ -print-file-name=${CANDIDATE}
                            OUTPUT_VARIABLE UBSAN_PATH2
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET
                        )

                        if( UBSAN_PATH2 AND EXISTS "${UBSAN_PATH2}" )
                            message(STATUS "Found UBSan runtime via clang++: ${UBSAN_PATH2}")
                            list(APPEND EXTRA_LINK_FLAGS "${UBSAN_PATH2}")
                            set(FOUND_UBSAN_LIB TRUE)
                            break()
                        endif() #UBSAN_PATH2
                    endforeach()
                endif() #!FOUND_UBSAN_LIB

                if( NOT FOUND_UBSAN_LIB )
                    message(WARNING "Clang UBSan runtime not found for clang-cl. Linking may fail. Install LLVM with UBSan runtimes or use the clang driver for linking.")
                endif() #!FOUND_UBSAN_LIB

            endif() #CLANG_MSVC_FRONTEND
        endif() #MSVC_COMPILER
    endif() #RUN_UBSAN

    list(APPEND EXTRA_COMPILE_FLAGS ${SAN_COMPILE_FLAGS})
    list(APPEND EXTRA_LINK_FLAGS    ${SAN_LINK_FLAGS})

    if( WINDOWS_OS_BUILD )
        include(CMakeHelpers/PlatformHelpers/FindASANDLLs.cmake)
        set(SANITIZER_DLLS "${SANITIZER_DLLS}" PARENT_SCOPE)
    endif() #WINDOWS_OS_BUILD

    set(EXTRA_COMPILE_FLAGS "${EXTRA_COMPILE_FLAGS}" PARENT_SCOPE)
    set(EXTRA_LINK_FLAGS "${EXTRA_LINK_FLAGS}" PARENT_SCOPE)

else() #RUN_ASAN OR RUN_UBSAN
    list(APPEND EXTRA_DEFINITIONS DIVIDE_ENABLE_CONVERSION_CHECKS=0)
endif()

set(EXTRA_DEFINITIONS "${EXTRA_DEFINITIONS}" PARENT_SCOPE)
