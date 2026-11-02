# Copyright (c) 2026 DIVIDE-Studio
vcpkg_download_distfile(ARCHIVE
    URLS "https://deb.debian.org/debian/pool/main/b/bzip2/bzip2_${VERSION}.orig.tar.gz"
    FILENAME "bzip2-${VERSION}.tar.gz"
    SHA512 083f5e675d73f3233c7930ebe20425a533feedeaaa9d8cc86831312a6581cefbe6ed0d08d2fa89be81082f2a5abdabca8b3c080bf97218a1bd59dc118a30b9f3
)

# Keep build logic and relative patch resolution at the pinned upstream port.
set(_divide_overlay_port_dir "${CURRENT_PORT_DIR}")
set(CURRENT_PORT_DIR "${VCPKG_ROOT_DIR}/ports/bzip2")
include("${CURRENT_PORT_DIR}/portfile.cmake")
set(CURRENT_PORT_DIR "${_divide_overlay_port_dir}")
