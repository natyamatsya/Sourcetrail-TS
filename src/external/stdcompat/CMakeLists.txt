cmake_minimum_required(VERSION 3.18)
project(stdcompat VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

# Global export set name
set(STDCOMPAT_EXPORT_SET stdcompatTargets)

# Add every component directory here
add_subdirectory(optional)
# stdext (non-standard extensions). stdext::expected is built directly on
# std::expected (C++23); it no longer depends on a stdcompat shim.
add_subdirectory(stdext)

# After all components are added, write the package files once
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/stdcompatConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

if (DB_INSTALL_LIB_STDCOMPAT_OPTIONAL OR DB_INSTALL_LIB_STDCOMPAT_STDEXT)
    install(
        EXPORT ${STDCOMPAT_EXPORT_SET}
        FILE stdcompatTargets.cmake
        NAMESPACE stdcompat::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stdcompat
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stdcompatConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/stdcompatConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stdcompat
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/stdcompatConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/stdcompatConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stdcompat
    )
endif()