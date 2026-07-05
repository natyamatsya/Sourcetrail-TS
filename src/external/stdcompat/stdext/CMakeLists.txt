# ------------------------------------------------------------------
# INTERFACE target (header only) -- the non-standard stdext extension.
# stdext::expected adds reference value support (expected<T&, E>) on top of
# std::expected; it is kept a separate target/component so the extension is an
# explicit opt-in for consumers that link it.
# ------------------------------------------------------------------
add_library(stdext_expected INTERFACE)
add_library(stdext::expected ALIAS stdext_expected)

# stdext::expected is implemented directly on top of std::expected (C++23).
target_compile_features(stdext_expected INTERFACE cxx_std_23)

# Public include path for consumers *after installation*
target_include_directories(stdext_expected
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if (DB_INSTALL_LIB_STDCOMPAT_STDEXT)
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
        TARGETS stdext_expected
        EXPORT ${STDCOMPAT_EXPORT_SET}
    )
endif()
