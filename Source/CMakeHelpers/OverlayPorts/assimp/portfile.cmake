# Retain the packaging fixes from the pinned registry without following its HEAD.
vcpkg_download_distfile(BUILD_FIXES
    URLS https://raw.githubusercontent.com/microsoft/vcpkg/2cfff9c458d9dcf642e9fa09ba624f9931bb5358/ports/assimp/build_fixes.patch
    SHA512 374aa9e7b2206df0e2b9486b5d4a43f9d8d4966800b33a3ad4862d7d04f45a7a55c8d791eb24f5b7ad50260c2f82cf97942ed2fd7818e774b193b23d23b87d9e
    FILENAME assimp-6.0.5-build-fixes.patch
)

vcpkg_download_distfile(FIX_CPP17_HEADERS
    URLS https://github.com/assimp/assimp/commit/9a486a72e43f92222969bfac7054554f97dd253f.patch?full_index=1
    SHA512 209d79e2366567cc8090e06684ef2da795a317c81d17694590e95051a62123219ab9b700bd0640e5ae0dd0d8bb9d8c9377dadc9168f08968d3b970244dbdc353
    FILENAME assimp_pr_6627.patch
)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO assimp/assimp
    REF "v${VERSION}"
    SHA512 57326194bf3a8e2ae793c739878231067fdd6d031531e910d4d20fc8a673eacf48f75e11bd21ca2e6421682f78585d445bffb647bdcb52e01b2cd4d3ff0e2c62
    HEAD_REF master
    PATCHES "${BUILD_FIXES}" "${FIX_CPP17_HEADERS}"
)

file(REMOVE "${SOURCE_PATH}/cmake-modules/FindZLIB.cmake")
foreach(dependency clipper draco gtest poly2tri pugixml rapidjson stb unzip utf8cpp zip zlib)
    file(REMOVE_RECURSE "${SOURCE_PATH}/contrib/${dependency}")
endforeach()

# Clipper and poly2tri are used only by the excluded IFC/Blender importers;
# kuba/zip is used only by the excluded 3MF exporter.
foreach(dependency polyclipping poly2tri)
    vcpkg_replace_string("${SOURCE_PATH}/code/CMakeLists.txt"
        "find_package(${dependency} CONFIG REQUIRED)" "")
    vcpkg_replace_string("${SOURCE_PATH}/code/CMakeLists.txt"
        "${dependency}::${dependency}" "")
endforeach()
foreach(dependency zip polyclipping poly2tri)
    vcpkg_replace_string("${SOURCE_PATH}/cmake-modules/assimp-plain-config.cmake.in"
        "find_dependency(${dependency} CONFIG)" "")
endforeach()

set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS} -D_CRT_SECURE_NO_WARNINGS")
set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -D_CRT_SECURE_NO_WARNINGS")

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES draco ASSIMP_BUILD_DRACO
)

set(IMPORTER_OPTIONS "")
foreach(importer 3DS ASE FBX MD2 MD5 OBJ X COLLADA GLTF)
    list(APPEND IMPORTER_OPTIONS "-DASSIMP_BUILD_${importer}_IMPORTER=ON")
endforeach()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DASSIMP_BUILD_ZLIB=OFF
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
        -DASSIMP_NO_EXPORT=ON
        -DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF
        -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
        -DASSIMP_IGNORE_GIT_HASH=ON
        -DASSIMP_INSTALL_PDB=OFF
        ${IMPORTER_OPTIONS}
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/assimp")
vcpkg_copy_pdbs()

if(VCPKG_TARGET_IS_WINDOWS)
    foreach(version vc140 vc141 vc142 vc143 vc145)
        foreach(crt mt md)
            list(APPEND DBG_NAMES assimp-${version}-${crt}d)
            list(APPEND REL_NAMES assimp-${version}-${crt})
        endforeach()
    endforeach()
endif()

find_library(ASSIMP_REL NAMES assimp ${REL_NAMES} PATHS "${CURRENT_PACKAGES_DIR}/lib" NO_DEFAULT_PATH)
find_library(ASSIMP_DBG NAMES assimp assimpd ${DBG_NAMES} PATHS "${CURRENT_PACKAGES_DIR}/debug/lib" NO_DEFAULT_PATH)
if(ASSIMP_REL)
    get_filename_component(ASSIMP_NAME_REL "${ASSIMP_REL}" NAME_WLE)
    string(REGEX REPLACE "^lib(.*)" "\\1" ASSIMP_NAME_REL "${ASSIMP_NAME_REL}")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/assimp.pc" "-lassimp" "-l${ASSIMP_NAME_REL}" IGNORE_UNCHANGED)
endif()
if(ASSIMP_DBG)
    get_filename_component(ASSIMP_NAME_DBG "${ASSIMP_DBG}" NAME_WLE)
    string(REGEX REPLACE "^lib(.*)" "\\1" ASSIMP_NAME_DBG "${ASSIMP_NAME_DBG}")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/assimp.pc" "-lassimp" "-l${ASSIMP_NAME_DBG}")
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(ASSIMP_PC_REQUIRES "pugixml minizip")
    if("draco" IN_LIST FEATURES)
        string(APPEND ASSIMP_PC_REQUIRES " draco")
    endif()
    foreach(config "" "/debug")
        if(config STREQUAL "/debug" AND VCPKG_BUILD_TYPE)
            continue()
        endif()
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}${config}/lib/pkgconfig/assimp.pc"
            "Libs:" "Requires.private: ${ASSIMP_PC_REQUIRES}\nLibs:")
    endforeach()
endif()

vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/LICENSE"
        "${SOURCE_PATH}/contrib/earcut-hpp/LICENSE"
        "${SOURCE_PATH}/contrib/openddlparser/LICENSE"
        "${SOURCE_PATH}/contrib/Open3DGC/o3dgcCommon.h"
        "${SOURCE_PATH}/contrib/Open3DGC/o3dgcArithmeticCodec.cpp"
)
