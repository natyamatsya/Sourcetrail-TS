# sqlpp23 is a header-only (INTERFACE) template library: no compiled artifacts,
# so we build the release tree only and treat everything as arch-independent.
set(VCPKG_BUILD_TYPE release)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO rbock/sqlpp23
    REF "${VERSION}"
    SHA512 6bb53a211b9345409183a4f1d91b2300a9ab8c34622b592e29f525e57c069ecd605182a6f2b779022e2371a363c5f4bf2da13f0e611ae636df4981573e6e6ea7
    HEAD_REF main
)

# Map port features -> upstream connector switches. All connectors are header-only
# wrappers; each pulls its client library in as a find_dependency() at consume time.
vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        sqlite3     BUILD_SQLITE3_CONNECTOR
        sqlcipher   BUILD_SQLCIPHER_CONNECTOR
        mysql       BUILD_MYSQL_CONNECTOR
        mariadb     BUILD_MARIADB_CONNECTOR
        postgresql  BUILD_POSTGRESQL_CONNECTOR
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        # C++20 modules are opt-in upstream; keep them off for a plain header install.
        -DBUILD_WITH_MODULES=OFF
        -DBUILD_TESTING=OFF
        # DEPENDENCY_CHECK=ON makes the generated config files find_dependency() their
        # client libs (SQLite3, PostgreSQL, ...) so consumers link them transitively.
        -DDEPENDENCY_CHECK=ON
)

vcpkg_cmake_install()

# Ship the C++20 module interface units alongside the headers. BUILD_WITH_MODULES stays OFF (a plain
# header install), but upstream does not precompile modules -- "your project compiles the module sources
# itself" (docs/modules.md) -- so a module-consuming build (Sourcetrail's, via
# cmake/SourcetrailSqlpp23Modules.cmake) needs the raw .cppm. Install them to the upstream-documented
# location <prefix>/modules/sqlpp23 so find_package consumers can locate them.
file(INSTALL "${SOURCE_PATH}/modules/"
     DESTINATION "${CURRENT_PACKAGES_DIR}/modules/sqlpp23"
     FILES_MATCHING PATTERN "*.cppm")

# Config package name is "Sqlpp23" (installed to <prefix>/lib/cmake/Sqlpp23).
vcpkg_cmake_config_fixup(PACKAGE_NAME Sqlpp23 CONFIG_PATH lib/cmake/Sqlpp23)

# config_fixup relocated the only thing under lib/ into share/, leaving lib/ empty.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")

# sqlpp23-ddl2cpp is a python3 code generator installed into bin. A header-only port
# must not ship files in bin, so relocate it under tools/ and drop the empty bin dirs.
if(EXISTS "${CURRENT_PACKAGES_DIR}/bin/sqlpp23-ddl2cpp")
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
    file(RENAME
        "${CURRENT_PACKAGES_DIR}/bin/sqlpp23-ddl2cpp"
        "${CURRENT_PACKAGES_DIR}/tools/${PORT}/sqlpp23-ddl2cpp")
endif()
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/bin"
    "${CURRENT_PACKAGES_DIR}/debug"
)

# The installed Sqlpp23Config.cmake defines an imported program target
# (sqlpp23::ddl2cpp) whose location is hard-coded as "<config_dir>/../../../bin/..".
# That relative depth assumes the config lives in lib/cmake/Sqlpp23; vcpkg relocates
# it to share/Sqlpp23 (one level shallower) AND we moved the script to tools/, so
# repoint it at the tool's real location relative to share/Sqlpp23.
vcpkg_replace_string(
    "${CURRENT_PACKAGES_DIR}/share/Sqlpp23/Sqlpp23Config.cmake"
    "\${CMAKE_CURRENT_LIST_DIR}/../../../bin/sqlpp23-ddl2cpp"
    "\${CMAKE_CURRENT_LIST_DIR}/../../tools/${PORT}/sqlpp23-ddl2cpp"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
