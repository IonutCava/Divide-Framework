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
    GIT_TAG         v5.3.0
    GIT_SHALLOW     TRUE
    #GIT_PROGRESS    TRUE
    EXCLUDE_FROM_ALL
)

message("BEGIN: Configuring JoltPhysics library")
message("Toggling SSE4.2 support:" ${SSE42_OPT})
message("Toggling AVX support:" ${AVX_OPT})
message("Toggling AVX2 support:" ${AVX2_OPT})
message("Toggling AVX512 support:" ${AVX512_OPT})
message("Toggling LZCNT support:" ${BMI2_OPT})
message("Toggling TZCNT support:" ${BMI2_OPT})
message("Toggling F16C support:" ${F16C_OPT})
message("Toggling FMADD support:" ${FMA4_OPT})

set(USE_SSE4_1 ON)
set(USE_SSE4_2 ${SSE42_OPT})
set(USE_AVX ${AVX_OPT})
set(USE_AVX2 ${AVX2_OPT})
set(USE_AVX512 ${AVX512_OPT})
set(USE_LZCNT  ${BMI2_OPT})
set(USE_TZCNT  ${BMI2_OPT})
set(USE_F16C ${F16C_OPT})
set(USE_FMADD  ${FMA4_OPT})
set(OBJECT_LAYER_BITS 32)
set(OVERRIDE_CXX_FLAGS  OFF)
set(CROSS_PLATFORM_DETERMINISTIC  ON)
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(ENABLE_INSTALL OFF)

set(USE_ASSERTS ON)
add_compile_definitions(JPH_ENABLE_ASSERTS)

set(DISABLE_CUSTOM_ALLOCATOR ON)
add_compile_definitions(JPH_DISABLE_CUSTOM_ALLOCATOR)

set(CMAKE_CXX_FLAGS_OLD "${CMAKE_CXX_FLAGS}")

if (MSVC_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd5045")
elseif(CLANG_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
elseif(GNU_COMPILER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
else()
    message(FATAL_ERROR "Unknown compiler type")
endif()

if (MSVC_COMPILER)
    # Spectre mitigation warning
    add_compile_options("/wd5045") 
endif()

message("END: Configuring JoltPhysics library")

FetchContent_GetProperties(JoltPhysics)
string(TOLOWER "JoltPhysics" lc_JoltPhysics)
if (NOT ${lc_JoltPhysics}_POPULATED)
    message("Fetching JoltPhysics library")
    FetchContent_MakeAvailable(JoltPhysics)
    add_subdirectory(${${lc_JoltPhysics}_SOURCE_DIR}/Build ${${lc_JoltPhysics}_BINARY_DIR})
endif()

include_directories(${JoltPhysics_SOURCE_DIR}/..)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}")
