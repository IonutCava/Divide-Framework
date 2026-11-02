# Copyright (c) 2026 DIVIDE-Studio
string(REPLACE "." "-" VERSION_HYPHEN "${VERSION}")
vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/freetype/freetype/archive/VER-${VERSION_HYPHEN}.tar.gz"
    FILENAME "freetype-freetype-VER-${VERSION_HYPHEN}.tar.gz"
    SHA512 c3b6b0cc4b428c9c647ab2148386901dfd315273b68051940e8fea6010d46fdd2913467c3ef58be0d499b8e2ef5a0f1a4cc5e739756155587f4f7dff08ef9695
)

# Keep build logic and relative patch resolution at the pinned upstream port.
set(_divide_overlay_port_dir "${CURRENT_PORT_DIR}")
set(CURRENT_PORT_DIR "${VCPKG_ROOT_DIR}/ports/freetype")
include("${CURRENT_PORT_DIR}/portfile.cmake")
set(CURRENT_PORT_DIR "${_divide_overlay_port_dir}")
