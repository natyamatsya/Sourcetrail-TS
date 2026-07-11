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

# Turso comparison backend (dev/CI only). Mirrors CppSQLite3 over the firewalled
# tsq_* C API exported by the turso_shim Rust static lib. Only built when the
# option is on; links turso_shim (Corrosion imported target) plus cppsqlite3 for
# the shared CppSQLite3Exception type and header.
if (SOURCETRAIL_USE_TURSO)
	add_library(External_lib_cppturso3
		turso/CppTurso3.cpp
		turso/CppTurso3.h
		turso/turso_shim.h
	)

	target_include_directories(External_lib_cppturso3 SYSTEM
		PUBLIC
			turso
	)

	target_link_libraries(External_lib_cppturso3
		PUBLIC
			turso_shim
			External_lib_cppsqlite3
	)
endif()

# Vendored stdcompat: header-only std backport/extension types.
# Provides the INTERFACE targets stdext::expected (C++23 std::expected extension
# with reference-value support) and stdcompat::optional (beman std::optional).
# Copied verbatim from inspiration/download; install() is gated off by default.
add_subdirectory(stdcompat)

# Optional Kùzu (LadybugDB) graph backend adapter. An exception-free
# (noexcept), expected-returning wrapper over the Kùzu C++ API; the lbug headers
# stay behind a pimpl so consumers only include LadybugConnection.h. Only built
# when the backend is enabled. External_lib_ladybug (the vendored engine) is
# PRIVATE — needed solely by the .cpp.
if (SOURCETRAIL_USE_LADYBUG)
	add_library(External_lib_cppladybug
		ladybug/LadybugConnection.cpp
		ladybug/LadybugConnection.h
	)

	target_include_directories(External_lib_cppladybug SYSTEM
		PUBLIC
			ladybug
	)

	target_link_libraries(External_lib_cppladybug
		PUBLIC
			stdext::expected
		PRIVATE
			External_lib_ladybug
	)
endif()
