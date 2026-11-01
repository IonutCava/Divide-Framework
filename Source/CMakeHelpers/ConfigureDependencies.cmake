# Manifest installation happens inside project(), before Source is configured.
list(REMOVE_ITEM VCPKG_MANIFEST_FEATURES tests)
if(BUILD_TESTING_INTERNAL)
    list(APPEND VCPKG_MANIFEST_FEATURES tests)
endif()
