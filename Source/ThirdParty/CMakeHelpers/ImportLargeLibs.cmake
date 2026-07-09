include(FetchContent)

set(CMAKE_CXX_FLAGS_OLD "${CMAKE_CXX_FLAGS}")

# Define suppression flags for third-party libs
set(THIRD_PARTY_SUPPRESS_FLAGS "")
if (MSVC_COMPILER)
    set(THIRD_PARTY_SUPPRESS_FLAGS "${THIRD_PARTY_SUPPRESS_FLAGS} /wd4312 /wd4477 /wd4996")
elseif(CLANG_COMPILER)
    set(THIRD_PARTY_SUPPRESS_FLAGS "${THIRD_PARTY_SUPPRESS_FLAGS} -Wno-missing-field-initializers -Wno-error=missing-field-initializers -Wno-deprecated-declarations -Wno-return-type-c-linkage -Wno-int-to-pointer-cast -Wno-string-plus-int -Wno-nullability-completeness")
    if(APPLE)
         set(THIRD_PARTY_SUPPRESS_FLAGS "${THIRD_PARTY_SUPPRESS_FLAGS} -Wno-vla-extension")
    else()
         set(THIRD_PARTY_SUPPRESS_FLAGS "${THIRD_PARTY_SUPPRESS_FLAGS} -Wno-vla-cxx-extension")
    endif()
elseif(GNU_COMPILER)
    set(THIRD_PARTY_SUPPRESS_FLAGS "${THIRD_PARTY_SUPPRESS_FLAGS} -Wno-deprecated-declarations -Wno-deprecated-copy -Wno-misleading-indentation -Wno-unused-but-set-variable")
else()
    message(FATAL_ERROR "Unknown compiler type")
endif()

#------------- CEGUI ------------------------------------------------------------------
message("Fetching CEGUI Lib")

# Apply suppression flags only for CEGUI
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}${THIRD_PARTY_SUPPRESS_FLAGS}")

FetchContent_Declare(
  Cegui
  GIT_REPOSITORY https://github.com/IonutCava/cegui.git
  GIT_TAG origin/v0-8
  SYSTEM
  EXCLUDE_FROM_ALL
)

set(CEGUI_BUILD_STATIC_CONFIGURATION TRUE CACHE BOOL "" FORCE)
set(CEGUI_IMAGE_CODEC STBImageCodec  CACHE BOOL "" FORCE)
set(CEGUI_IMAGE_CODEC_LIB "CEGUI${CEGUI_IMAGE_CODEC}"  CACHE BOOL "" FORCE)
set(CEGUI_BUILD_IMAGECODEC_STB TRUE CACHE BOOL "" FORCE)
set(CEGUI_XML_PARSER ExpatParser  CACHE BOOL "" FORCE)
set(CEGUI_XML_PARSER_LIB "CEGUI${CEGUI_XML_PARSER}")
set(CEGUI_BUILD_XMLPARSER_EXPAT TRUE CACHE BOOL "" FORCE)
set(CEGUI_BUILD_STATIC_FACTORY_MODULE TRUE CACHE BOOL "" FORCE)
set(CEGUI_HAS_STD11_REGEX TRUE CACHE BOOL "" FORCE)
set(CEGUI_SAMPLES_ENABLED OFF CACHE BOOL "" FORCE)
set(CEGUI_STRING_CLASS 1  CACHE BOOL "" FORCE)
set(CEGUI_BUILD_APPLICATION_TEMPLATES  OFF CACHE BOOL "" FORCE)
set(CEGUI_FONT_USE_GLYPH_PAGE_LOAD TRUE CACHE BOOL "" FORCE)
set(CEGUI_BUILD_SHARED_LIBS_WITH_STATIC_DEPENDENCIES TRUE CACHE BOOL "" FORCE)
set(CEGUI_USE_FRIBIDI OFF CACHE BOOL "" FORCE)
set(CEGUI_USE_MINIBIDI OFF CACHE BOOL "" FORCE)
set(CEGUI_USE_GLEW OFF CACHE BOOL "" FORCE)
set(CEGUI_USE_EPOXY OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_OPENGL OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_OPENGL3 OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_OPENGLES OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_OGRE OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_IRRLICHT OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_DIRECTFB OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_DIRECT3D11 OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_DIRECT3D10 OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_DIRECT3D9 OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_RENDERER_NULL OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_LUA_MODULE OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_LUA_GENERATOR OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_PYTHON_MODULES OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_SAFE_LUA_MODULE OFF CACHE BOOL "" FORCE)
set(CEGUI_BUILD_XMLPARSER_EXPAT TRUE CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_PythonInterp ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_PythonLibs ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Cegui)

set(CEGUI_LIBRARY_NAMES "CEGUIBase-0_Static;CEGUICommonDialogs-0_Static;CEGUICoreWindowRendererSet_Static;${CEGUI_IMAGE_CODEC_LIB}_Static;${CEGUI_XML_PARSER_LIB}_Static")

set(CEGUI_LIBRARIES "")

foreach(TARGET_LIB ${CEGUI_LIBRARY_NAMES})

    if (WIN32 AND (CMAKE_BUILD_TYPE MATCHES Debug))
        set(TARGET_LIB "${TARGET_LIB}_d")
    endif()

    list(APPEND CEGUI_LIBRARIES ${TARGET_LIB})
endforeach()

# Restore original flags for main project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}")

include_directories(
    "${cegui_SOURCE_DIR}/cegui/include"
    "${cegui_BINARY_DIR}/cegui/include"
)
link_directories("${cegui_BINARY_DIR}/lib" )

#------------- NRI ------------------------------------------------------------------
message("Fetching NVIDIA NRI Lib")

# Apply suppression flags only for NRI
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}${THIRD_PARTY_SUPPRESS_FLAGS}")

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
    option(NRI_ENABLE_AGILITY_SDK_SUPPORT "" ON)
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
    GIT_TAG        v180
    SYSTEM
)

FetchContent_MakeAvailable(nri)

set(NRI_TARGETS
    NRI
    NRI_Shared
    NRI_NONE
    NRI_D3D11
    NRI_D3D12
    NRI_VK
    NRI_Validation
    NRI_Shaders
)

foreach(nri_target IN LISTS NRI_TARGETS)
    if(TARGET ${nri_target})
        set_target_properties(${nri_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)

        if(CLANG_COMPILER)
            target_compile_options(${nri_target} PRIVATE
                -Wno-missing-field-initializers
                -Wno-nullability-completeness
            )
        endif()
    endif()
endforeach()

# Restore original flags for main project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}")
