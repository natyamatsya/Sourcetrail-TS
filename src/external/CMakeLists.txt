# Use quotes to indicate a version literal:
set(CppSQLite3_VERSION "\"3.2\"" PARENT_SCOPE)

add_library(External_lib_cppsqlite3
	sqlite/CppSQLite3.cpp
	sqlite/CppSQLite3.h
)

target_include_directories(External_lib_cppsqlite3 SYSTEM
	PUBLIC
		sqlite
)

target_link_libraries(External_lib_cppsqlite3
	PUBLIC
		External_lib_sqlite3
)

# Vendored stdcompat: header-only std backport/extension types.
# Provides the INTERFACE targets stdext::expected (C++23 std::expected extension
# with reference-value support) and stdcompat::optional (beman std::optional).
# Copied verbatim from inspiration/download; install() is gated off by default.
add_subdirectory(stdcompat)
