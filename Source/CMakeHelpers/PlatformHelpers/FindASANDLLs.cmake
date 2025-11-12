if (NOT WINDOWS_OS_BUILD)   
    message(FATAL_ERROR "FindASANDLLs.cmake is only supported on Windows builds.")
endif() # WINDOWS_OS_BUILD

set(ASAN_DLL_CAND "clang_rt.asan_dynamic-x86_64.dll" "clang_rt.asan_dynamic_runtime_thunk-x86_64.dll")
set(UBSAN_DLL_CAND "clang_rt.ubsan_standalone-x86_64.dll" "clang_rt.ubsan_standalone_cxx-x86_64.dll")

if ( NOT MSVC_COMPILER )
    find_program(CLANG_EXE clang)
    find_program(CLANGPP_EXE clang++)
endif()

function(find_and_append_runtime OUT_LIST CANDIDATE)
    set(DLL_PATH "")

    if ( MSVC_COMPILER )
        set(PATH_DIRECTORIES "$ENV{PATH}")

        foreach(DIR IN LISTS PATH_DIRECTORIES)
            string(STRIP "${DIR}" DIR)
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" DIR "${DIR}")
            if (DIR STREQUAL "" )
                continue()
            endif()

            get_filename_component(ABS_DIR "${DIR}" ABSOLUTE)
            set(CANDIDATE_PATH "${ABS_DIR}/${CANDIDATE}")
            if (EXISTS "${CANDIDATE_PATH}")
                set(DLL_PATH "${CANDIDATE_PATH}")
                break()
            endif()
        endforeach()

    else() #MSVC_COMPILER

        if (CLANGPP_EXE)
            execute_process(COMMAND ${CLANGPP_EXE} -print-file-name=${CANDIDATE}
                            OUTPUT_VARIABLE DLL_PATH
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET)
        endif()
        if (NOT DLL_PATH AND CLANG_EXE)
            execute_process(COMMAND ${CLANG_EXE} -print-file-name=${CANDIDATE}
                            OUTPUT_VARIABLE DLL_PATH
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET)
        endif()

    endif() #MSVC_COMPILER

    if (NOT DLL_PATH)
    
        set(VISUAL_STUDIO_ROOT_CANDIDATES
            "${ProgramFiles}/Microsoft Visual Studio"
            "C:/Program Files/Microsoft Visual Studio"
        )

        if(DEFINED ENV{ProgramFiles})
            list(APPEND VISUAL_STUDIO_ROOT_CANDIDATES_CANDIDATES "$ENV{ProgramFiles}/Microsoft Visual Studio")
        endif()

        foreach(VISUAL_STUDIO_ROOT_CANDIDATES IN LISTS VISUAL_STUDIO_ROOT_CANDIDATES_CANDIDATES)
            if (EXISTS "${VISUAL_STUDIO_ROOT_CANDIDATES}")

                if (MSVC_COMPILER)
                    file(GLOB FOUND_LIBS RELATIVE 
                        "${CMAKE_CURRENT_LIST_DIR}"
                        "${VISUAL_STUDIO_ROOT_CANDIDATES}/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/${CANDIDATE}"
                    )
                else()
                    file(GLOB FOUND_LIBS RELATIVE 
                        "${CMAKE_CURRENT_LIST_DIR}"
                        "${VISUAL_STUDIO_ROOT_CANDIDATES}/*/*/VC/Tools/Llvm/x64/lib/clang/*/lib/windows/${CANDIDATE}"
                    )
                endif()

                foreach(FILE IN LISTS FOUND_LIBS)
                    get_filename_component(ABS "${VISUAL_STUDIO_ROOT_CANDIDATES}/${FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
                    if (EXISTS "${ABS}")
                        set(DLL_PATH "${ABS}")
                        break()
                    endif()
                endforeach()
            endif()
            if (DLL_PATH)
                break()
            endif()
        endforeach()

    endif() #!DLL_PATH

    if (DLL_PATH AND EXISTS "${DLL_PATH}")
        list(APPEND ${OUT_LIST} "${DLL_PATH}")
        # expose updated list to caller
        set(${OUT_LIST} "${${OUT_LIST}}" PARENT_SCOPE)
    endif()
endfunction()

set(SANITIZER_DLLS "")

foreach(CANDIDATE IN LISTS ASAN_DLL_CAND)
    find_and_append_runtime(SANITIZER_DLLS ${CANDIDATE})
endforeach()

if (NOT MSVC_COMPILER)
    foreach(CANDIDATE IN LISTS UBSAN_DLL_CAND)
        find_and_append_runtime(SANITIZER_DLLS ${CANDIDATE})
    endforeach()
endif()

set(SANITIZER_DLLS "${SANITIZER_DLLS}" PARENT_SCOPE)
