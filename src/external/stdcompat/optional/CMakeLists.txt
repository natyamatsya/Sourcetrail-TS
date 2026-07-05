# ------------------------------------------------------------------
# INTERFACE target (header only)
# ------------------------------------------------------------------
add_library(stdcompat_optional INTERFACE)
add_library(stdcompat::optional ALIAS stdcompat_optional)

# beman::optional uses concepts and <ranges>, so consumers need C++20.
target_compile_features(stdcompat_optional INTERFACE cxx_std_20)

# Public include path for consumers *after installation*
target_include_directories(stdcompat_optional
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Private include path for beman::optional while building this project.
# The vendored headers include each other as <beman/optional/...>, so the
# 3rdparty directory itself is the include root.
target_include_directories(stdcompat_optional
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rdparty>
)

if (DB_INSTALL_LIB_STDCOMPAT_OPTIONAL)
    # ------------------------------------------------------------------
    # Install headers
    # ------------------------------------------------------------------
    install(
        DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # ------------------------------------------------------------------
    # Export this target so the root CMakeLists can pack it later
    # ------------------------------------------------------------------
    install(
        TARGETS stdcompat_optional
        EXPORT ${STDCOMPAT_EXPORT_SET}
    )
endif()
