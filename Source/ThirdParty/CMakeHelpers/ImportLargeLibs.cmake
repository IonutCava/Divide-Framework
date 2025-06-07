include(FetchContent)

#----------------------------------------------------------------------------- CEGUI ------------------------------------------------------------------

set(CMAKE_CXX_FLAGS_OLD "${CMAKE_CXX_FLAGS}")

if (MSVC_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4312 /wd4477 /wd4996")
elseif(CLANG_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -Wno-return-type-c-linkage -Wno-int-to-pointer-cast -Wno-string-plus-int")
    if(APPLE)
         set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-vla-extension")
    else()
         set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-vla-cxx-extension")
    endif()
elseif(GNU_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-field-initializers -Wno-deprecated-declarations -Wno-deprecated-copy -Wno-misleading-indentation -Wno-unused-but-set-variable")
else()
    message(FATAL_ERROR "Unknown compiler type")
endif()


FetchContent_Declare(
  Cegui
  GIT_REPOSITORY https://github.com/IonutCava/cegui.git
  GIT_TAG origin/v0-8
  #GIT_PROGRESS   TRUE
  #SYSTEM
  EXCLUDE_FROM_ALL
)

set(CEGUI_BUILD_STATIC_CONFIGURATION TRUE)
set(CEGUI_IMAGE_CODEC STBImageCodec)
set(CEGUI_IMAGE_CODEC_LIB "CEGUI${CEGUI_IMAGE_CODEC}")
set(CEGUI_BUILD_IMAGECODEC_STB TRUE)
set(CEGUI_XML_PARSER ExpatParser)
set(CEGUI_XML_PARSER_LIB "CEGUI${CEGUI_XML_PARSER}")
set(CEGUI_BUILD_XMLPARSER_EXPAT TRUE)
set(CEGUI_BUILD_STATIC_FACTORY_MODULE TRUE)
set(CEGUI_HAS_STD11_REGEX TRUE)
set(CEGUI_SAMPLES_ENABLED FALSE)
set(CEGUI_STRING_CLASS 1)
set(CEGUI_BUILD_APPLICATION_TEMPLATES FALSE)
set(CEGUI_FONT_USE_GLYPH_PAGE_LOAD TRUE)
set(CEGUI_BUILD_SHARED_LIBS_WITH_STATIC_DEPENDENCIES TRUE)
set(CEGUI_USE_FRIBIDI FALSE)
set(CEGUI_USE_MINIBIDI FALSE)
set(CEGUI_USE_GLEW FALSE)
set(CEGUI_USE_EPOXY FALSE)
set(CEGUI_BUILD_RENDERER_OPENGL FALSE)
set(CEGUI_BUILD_RENDERER_OPENGL3 FALSE)
set(CEGUI_BUILD_RENDERER_OPENGLES FALSE)
set(CEGUI_BUILD_RENDERER_OGRE FALSE)
set(CEGUI_BUILD_RENDERER_IRRLICHT FALSE)
set(CEGUI_BUILD_RENDERER_DIRECTFB FALSE)
set(CEGUI_BUILD_RENDERER_DIRECT3D11 FALSE)
set(CEGUI_BUILD_RENDERER_DIRECT3D10 FALSE)
set(CEGUI_BUILD_RENDERER_DIRECT3D9 FALSE)
set(CEGUI_BUILD_RENDERER_NULL FALSE)
set(CEGUI_BUILD_LUA_MODULE FALSE)
set(CEGUI_BUILD_LUA_GENERATOR FALSE)
set(CEGUI_BUILD_PYTHON_MODULES FALSE)
set(CEGUI_BUILD_SAFE_LUA_MODULE FALSE)
set(CEGUI_BUILD_XMLPARSER_EXPAT TRUE)

FetchContent_MakeAvailable(Cegui)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}")

set(CEGUI_LIBRARY_NAMES "CEGUIBase-0_Static;CEGUICommonDialogs-0_Static;CEGUICoreWindowRendererSet_Static;${CEGUI_IMAGE_CODEC_LIB}_Static;${CEGUI_XML_PARSER_LIB}_Static")

set(CEGUI_LIBRARIES "")

foreach(TARGET_LIB ${CEGUI_LIBRARY_NAMES})

    if (WIN32 AND (CMAKE_BUILD_TYPE MATCHES Debug))
        set(TARGET_LIB "${TARGET_LIB}_d")
    endif()

    list(APPEND CEGUI_LIBRARIES ${TARGET_LIB})
endforeach()

include_directories(
    "${cegui_SOURCE_DIR}/cegui/include"
    "${cegui_BINARY_DIR}/cegui/include"
)
link_directories("${cegui_BINARY_DIR}/lib" )


#----------------------------------------------------------------------------- JOLT Physics ------------------------------------------------------------------
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY  https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG         master
    GIT_SHALLOW     TRUE
    #GIT_PROGRESS    TRUE
)

FetchContent_GetProperties(JoltPhysics)
string(TOLOWER "JoltPhysics" lc_JoltPhysics)
if (NOT ${lc_JoltPhysics}_POPULATED)
    FetchContent_Populate(JoltPhysics)
    message("BEGIN: Configuring JoltPhysics library")

    option(USE_SSE4_1 "Enable SSE4.1" ON)

    option(USE_SSE4_2 "Enable SSE4.2" ${SSE42_OPT})
    message("Toggling SSE4.2 support:" ${SSE42_OPT})

    option(USE_AVX "Enable AVX" ${AVX_OPT})
    message("Toggling AVX support:" ${AVX_OPT})

    option(USE_AVX2 "Enable AVX2" ${AVX2_OPT})
    message("Toggling AVX2 support:" ${AVX2_OPT})
        
    option(USE_AVX512 "Enable AVX512" ${AVX512_OPT})
    message("Toggling AVX512 support:" ${AVX512_OPT})

    option(USE_LZCNT "Enable LZCNT" ${BMI2_OPT})
    message("Toggling LZCNT support:" ${BMI2_OPT})

    option(USE_TZCNT "Enable TZCNT" ${BMI2_OPT})
    message("Toggling TZCNT support:" ${BMI2_OPT})

    option(USE_F16C "Enable F16C" ${F16C_OPT})
    message("Toggling F16C support:" ${F16C_OPT})

    option(USE_FMADD "Enable FMADD" ${FMA4_OPT})
    message("Toggling FMADD support:" ${FMA4_OPT})

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
    if (MSVC_COMPILER)
        # Spectre mitigation warning
        add_compile_options("/wd5045") 
    endif()

    add_subdirectory(${${lc_JoltPhysics}_SOURCE_DIR}/Build ${${lc_JoltPhysics}_BINARY_DIR})
endif ()

include_directories(${JoltPhysics_SOURCE_DIR}/..)
