
#ref: https://github.com/madler/zlib/issues/759

macro(FetchContent_MakeAvailableExcludeFromAll)
    foreach(contentName IN ITEMS ${ARGV})
        string(TOLOWER ${contentName} contentNameLower)
        FetchContent_GetProperties(${contentName})
        if(NOT ${contentNameLower}_POPULATED)
            FetchContent_Populate(${contentName})
            if(EXISTS ${${contentNameLower}_SOURCE_DIR}/CMakeLists.txt)
                add_subdirectory(${${contentNameLower}_SOURCE_DIR}
                    ${${contentNameLower}_BINARY_DIR} EXCLUDE_FROM_ALL)
            endif()
        endif()
    endforeach()
endmacro()


function (FetchContent_MakeAvailable_JoltPhysics)
    FetchContent_GetProperties(JoltPhysics)
    string(TOLOWER "JoltPhysics" lc_JoltPhysics)
    if (NOT ${lc_JoltPhysics}_POPULATED)
        FetchContent_Populate(JoltPhysics)
        message("BEGIN: Configuring JoltPhysics library")

        option(USE_SSE4_1 "Enable SSE4.1" ON)

        option(USE_SSE4_2 "Enable SSE4.2" ${SSE42_OPT})
        message("Enabling SSE4.2 support:" ${SSE42_OPT})

        option(USE_AVX "Enable AVX" ${AVX_OPT})
        message("Enabling AVX support:" ${AVX_OPT})

        option(USE_AVX2 "Enable AVX2" ${AVX2_OPT})
        message("Enabling AVX2 support:" ${AVX2_OPT})
        
        option(USE_AVX512 "Enable AVX512" ${AVX512_OPT})
        message("Enabling AVX512 support:" ${AVX512_OPT})

        option(USE_LZCNT "Enable LZCNT" ${BMI2_OPT})
        message("Enabling LZCNT support:" ${BMI2_OPT})

        option(USE_TZCNT "Enable TZCNT" ${BMI2_OPT})
        message("Enabling TZCNT support:" ${BMI2_OPT})

        option(USE_F16C "Enable F16C" ${F16C_OPT})
        message("Enabling F16C support:" ${F16C_OPT})

        option(USE_FMADD "Enable FMADD" ${FMA4_OPT})
        message("Enabling FMADD support:" ${FMA4_OPT})

        option(USE_ASSERTS "Enable asserts" ON)
        option(DOUBLE_PRECISION "Use double precision math" OFF)

        if (${GNU_COMPILER})
            option(ENABLE_ALL_WARNINGS "Enable all warnings and warnings as errors" OFF)
            message("Enable all warnings and warnings as errors: OFF")
        else()
            option(ENABLE_ALL_WARNINGS "Enable all warnings and warnings as errors" ON)
            message("Enable all warnings and warnings as errors: ON")
        endif()
        
        option(GENERATE_DEBUG_SYMBOLS "Generate debug symbols" $<$<CONFIG:Debug,RelWithDebInfo>:ON>$<$<CONFIG:Release>:OFF>)
        option(OVERRIDE_CXX_FLAGS "Override CMAKE_CXX_FLAGS_DEBUG/RELEASE" OFF)
        option(CROSS_PLATFORM_DETERMINISTIC "Cross platform deterministic" ON)
        option(CROSS_COMPILE_ARM "Cross compile to aarch64-linux-gnu" OFF)
        option(BUILD_SHARED_LIBS "Compile Jolt as a shared library" OFF)
        option(INTERPROCEDURAL_OPTIMIZATION "Enable interprocedural optimizations" OFF)
        option(FLOATING_POINT_EXCEPTIONS_ENABLED "Enable floating point exceptions" ON)
        option(CPP_RTTI_ENABLED "Enable C++ RTTI" OFF)
        option(OBJECT_LAYER_BITS "Number of bits in ObjectLayer" 16)
        option(USE_WASM_SIMD "Enable SIMD for WASM" OFF)


        option(TRACK_BROADPHASE_STATS "Track Broadphase Stats" OFF)
        option(TRACK_NARROWPHASE_STATS "Track Narrowphase Stats" OFF)
        option(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE "Enable debug renderer in Debug and Release builds" ON)
        option(DEBUG_RENDERER_IN_DISTRIBUTION "Enable debug renderer in all builds" OFF)
        option(PROFILER_IN_DEBUG_AND_RELEASE "Enable the profiler in Debug and Release builds" ON)
        option(PROFILER_IN_DISTRIBUTION "Enable the profiler in all builds" OFF)
        option(DISABLE_CUSTOM_ALLOCATOR "Disable support for a custom memory allocator" OFF)
        option(USE_STD_VECTOR "Use std::vector instead of own Array class" OFF)
        option(ENABLE_OBJECT_STREAM "Compile the ObjectStream class and RTTI attribute information" ON)
        option(USE_STATIC_MSVC_RUNTIME_LIBRARY "Use the static MSVC runtime library" OFF)
        option(ENABLE_INSTALL "Generate installation target" OFF)
        
        add_definitions(-DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED)

        message("END: Configuring JoltPhysics library")

        add_subdirectory(${${lc_JoltPhysics}_SOURCE_DIR}/Build ${${lc_JoltPhysics}_BINARY_DIR})
    endif ()
endfunction ()