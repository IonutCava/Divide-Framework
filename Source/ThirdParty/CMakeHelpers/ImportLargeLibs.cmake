include(FetchContent)

set(CMAKE_CXX_FLAGS_OLD "${CMAKE_CXX_FLAGS}")

if (MSVC_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4312 /wd4477 /wd4996")
elseif(CLANG_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-field-initializers -Wno-error=missing-field-initializers -Wno-deprecated-declarations -Wno-return-type-c-linkage -Wno-int-to-pointer-cast -Wno-string-plus-int -Wno-nullability-completeness")
    if(APPLE)
         set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-vla-extension")
    else()
         set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-vla-cxx-extension")
    endif()
elseif(GNU_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -Wno-deprecated-copy -Wno-misleading-indentation -Wno-unused-but-set-variable")
else()
    message(FATAL_ERROR "Unknown compiler type")
endif()

#----------------------------------------------------------------------------- CEGUI ------------------------------------------------------------------
message("Fetching CEGUI Lib")

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
message("Fetching Jolt Physics Lib")
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY  https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG         v5.3.0
    GIT_SHALLOW     TRUE
    #GIT_PROGRESS    TRUE
    SOURCE_SUBDIR   "Build"
    EXCLUDE_FROM_ALL
)

message("BEGIN: Configuring JoltPhysics library")

set(OVERRIDE_CXX_FLAGS OFF)
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(ENABLE_INSTALL OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(PROFILER_IN_DISTRIBUTION OFF)

set(GENERATE_DEBUG_SYMBOLS $<IF:$<CONFIG:Release>:OFF,ON>)

list(APPEND EXTRA_DEFINITIONS JPH_OBJECT_STREAM)

set(DEBUG_RENDERER_IN_DISTRIBUTION ON)
list(APPEND EXTRA_DEFINITIONS JPH_DEBUG_RENDERER)

set(USE_ASSERTS ON)
list(APPEND EXTRA_DEFINITIONS JPH_ENABLE_ASSERTS)

set(DISABLE_CUSTOM_ALLOCATOR ON)
list(APPEND EXTRA_DEFINITIONS JPH_DISABLE_CUSTOM_ALLOCATOR)

set(CROSS_PLATFORM_DETERMINISTIC OFF)

if (MSVC_COMPILER)
    set(FLOATING_POINT_EXCEPTIONS_ENABLED ON)
    list(APPEND EXTRA_DEFINITIONS JPH_FLOATING_POINT_EXCEPTIONS_ENABLED)
endif()

message("Toggling SSE4.1 support:" ${SSE41_OPT})
set(USE_SSE4_1 ${SSE41_OPT})

message("Toggling SSE4.2 support:" ${SSE42_OPT})
set(USE_SSE4_2 ${SSE42_OPT})

message("Toggling AVX support:" ${AVX_OPT})
set(USE_AVX ${AVX_OPT})

message("Toggling AVX2 support:" ${AVX2_OPT})
set(USE_AVX2 ${AVX2_OPT})

message("Toggling LZCNT support:" ${LZCNT_OPT})
set(USE_LZCNT ${LZCNT_OPT})

message("Toggling TZCNT support:" ${BMI1_OPT})
set(USE_TZCNT ${BMI1_OPT})

message("Toggling F16C support:" ${F16C_OPT})
set(USE_F16C ${F16C_OPT})

message("Toggling FMADD support:" ${FMA_OPT})
set(USE_FMADD ${FMA_OPT})

if ( AVX512F_OPT AND AVX512VL_OPT AND AVX512DQ_OPT )
    message("Toggling AVX512 support: ON")
    set(USE_AVX512 ON)
else()
    message("Toggling AVX512 support: OFF")
    set(USE_AVX512 OFF)
endif()

set(CMAKE_CXX_FLAGS_OLD "${CMAKE_CXX_FLAGS}")

if (MSVC_COMPILER)
    add_compile_options("/wd5045") 
elseif(CLANG_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
elseif(GNU_COMPILER)
    # false positives with array-bounds in Float3::operator[] and Float4::operator[] in JoltPhysics 5.3.0
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter -Wno-array-bounds")
else()
    message(FATAL_ERROR "Unknown compiler type")
endif()

message("END: Configuring JoltPhysics library")

FetchContent_MakeAvailable(JoltPhysics)

include_directories(${JoltPhysics_SOURCE_DIR}/..)

#----------------------------------------------------------------------------- NRI Physics ------------------------------------------------------------------
message("Fetching NVIDIA NRI Lib")
option(NRI_STATIC_LIBRARY "" ON)
option(NRI_ENABLE_DEBUG_NAMES_AND_ANNOTATIONS "" ON)
option(NRI_ENABLE_VK_SUPPORT "" ON)
option(NRI_ENABLE_NONE_SUPPORT "" ON)
option(NRI_ENABLE_VALIDATION_SUPPORT "" ON)
option(NRI_ENABLE_IMGUI_EXTENSION "" ON)
option(NRI_ENABLE_FFX_SDK "" ON)
option(NRI_ENABLE_XESS_SDK "" ON)

if(WINDOWS_OS_BUILD)
    option(NRI_ENABLE_D3D12_SUPPORT "" ON)
    option(NRI_ENABLE_D3D11_SUPPORT "" ON)
    option(NRI_ENABLE_NVTX_SUPPORT "" ON)
elseif(MAC_OS_BUILD)
    option(NRI_ENABLE_METAL_SUPPORT "" ON)
else()
    option(NRI_ENABLE_XLIB_SUPPORT "" ON)
    if(WAYLAND_FOUND)
        option(NRI_ENABLE_WAYLAND_SUPPORT "" ON)
    endif()
endif()
FetchContent_Declare(
    nri
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NRI.git
    GIT_TAG        922588d8e1a01c0a4d61b1eb98506c02ffd0333f
    #GIT_PROGRESS   TRUE
    SYSTEM
)

FetchContent_MakeAvailable( nri )

set(NRI_TARGETS
    NRI
    NRI_NONE
    NRI_D3D11
    NRI_D3D12
    NRI_VK
    NRI_Validation
)

target_compile_options(NRI_Shared PRIVATE $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>: -Wno-missing-field-initializers -Wno-error=missing-field-initializers >)
foreach(nri_target IN LISTS NRI_TARGETS)
    if(TARGET ${nri_target})
        set_target_properties(${nri_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_compile_options(${nri_target} PRIVATE $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>: -Wno-missing-field-initializers -Wno-error=missing-field-initializers >)
    endif()
endforeach()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}")
