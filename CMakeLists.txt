cmake_minimum_required(VERSION 4.4.0)

# `import std` in the module build (experimental). CMake's import-std support must be gated *before*
# project() enables CXX (the toolchain probe needs both the experimental UUID and C++23 at detection
# time), so read the cached SOURCETRAIL_CXX_IMPORT_STD value directly here -- the option() call further
# down just formalizes it for the cache/UI. Default off; see context/DESIGN_INDEXER_MODULARIZATION.md
# for the toolchain caveats (on brew LLVM / macOS it currently also needs CMAKE_CXX_STDLIB_MODULES_JSON
# pointed at lib/c++/libc++.modules.json, and CMake's std-module archive doesn't link with macOS ld).
if(SOURCETRAIL_CXX_IMPORT_STD)
	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	# UUID is CMake-version-specific; this is the CxxImportStd gate for CMake 4.4.
	set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
endif()

include(CMakePrintHelpers)
include(cmake/Sourcetrail.cmake)

# Detect vcpkg build scenario here, so we don't depend on CMakePresets settings:
if (CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake$")
	message(STATUS "Vcpkg Build")
	set(isVcpkgBuild TRUE)
else()
	message(STATUS "System Build")
	set(isVcpkgBuild FALSE)
endif()

if (isVcpkgBuild)
	set(VCPKG_INSTALL_OPTIONS
		# Don't print package usages:
		--no-print-usage

		# Prevent rebuilding after a compiler update:		
		--x-abi-tools-use-exact-versions
		
		# Clean everything except sources:
		#--clean-downloads-after-build
		#--clean-packages-after-build
	)
endif()

# Make the build type available as a compiler variable:
if (isVcpkgBuild)
	add_compile_definitions(IS_VCPKG_BUILD=1)
else()
	add_compile_definitions(IS_VCPKG_BUILD=0)
endif()

# Project ----------------------------------------------------------------------

project(Sourcetrail VERSION 2026.2.11 HOMEPAGE_URL "http://sourcetrail.de")
cmake_print_variables(CMAKE_C_COMPILER)
cmake_print_variables(CMAKE_CXX_COMPILER)

# Configure which language(s)/package(s) should be supported:
option(BUILD_CXX_LANGUAGE_PACKAGE  "Add C/C++ support to the Sourcetrail indexer." OFF)
option(BUILD_RUST_LANGUAGE_PACKAGE "Add Rust support to the Sourcetrail indexer." OFF)
option(BUILD_SWIFT_LANGUAGE_PACKAGE "Add Swift support to the Sourcetrail indexer." OFF)
option(BUILD_ZIG_LANGUAGE_PACKAGE  "Add Zig support to the Sourcetrail indexer." OFF)
option(BUILD_UNIT_TESTS_PACKAGE    "Build the corresponding language unit tests." OFF)
# Dev/CI-only: run every SQL operation against Turso alongside SQLite and report
# divergences. Requires the Rust toolchain (builds the turso_shim crate). Off by
# default so normal builds are unaffected.
option(SOURCETRAIL_TURSO_COMPARE   "Run a Turso backend alongside SQLite and diff results." ON)
# Phase 2 prototype: assign element ids from an in-process counter instead of the
# INSERT+lastRowId() round-trip — the precondition for a non-blocking write path.
option(SOURCETRAIL_CLIENT_IDS      "Assign element ids in-process (no lastRowId round-trip)." OFF)
# Phase 4: Turso-only concurrent-writer inject path (multiple MVCC connections
# writing at once). Distinct from _COMPARE (which keeps SQLite as the single-
# writer source of truth). Both pull in the turso_shim + cppturso3 targets.
option(SOURCETRAIL_TURSO_CONCURRENT "Enable the Turso-only concurrent-writer storage path." OFF)

# The dual-build scaffolding header (SrctrlModule.h) is included by first-party headers in BOTH the
# header and the module build (it just defines SRCTRL_EXPORT), so make it globally available.
include_directories(${CMAKE_SOURCE_DIR}/src/scaffolding)

# Build the first-party C++ as C++20 modules instead of headers (experimental; see
# context/DESIGN_INDEXER_MODULARIZATION.md). Default OFF -- the classic header build is always
# available. When ON, a capability probe auto-falls back to the header build (with a warning) on a
# toolchain that can't build modules, so enabling this can never hard-fail a configure.
option(SOURCETRAIL_CXX_MODULES "Build first-party C++ as C++20 modules (experimental)." OFF)
# In the module build, `import std;` instead of #include-ing std headers. Implies SOURCETRAIL_CXX_MODULES.
# The pre-project() gate above wires CMake's experimental import-std support; here we just formalize the
# option and, when on, define SRCTRL_IMPORT_STD so the module wrappers switch to `import std;`.
option(SOURCETRAIL_CXX_IMPORT_STD "Module build: `import std;` instead of #include-ing std headers (experimental; needs toolchain import-std support). Implies SOURCETRAIL_CXX_MODULES." OFF)
if(SOURCETRAIL_CXX_IMPORT_STD AND NOT SOURCETRAIL_CXX_MODULES)
	message(STATUS "SOURCETRAIL_CXX_IMPORT_STD implies SOURCETRAIL_CXX_MODULES; enabling it.")
	set(SOURCETRAIL_CXX_MODULES ON)
endif()
include(cmake/SourcetrailCxxModules.cmake)
sourcetrail_resolve_cxx_modules(SOURCETRAIL_CXX_MODULES_ENABLED)
# Opt-in: consume sqlpp23 as a C++20 module (`import sqlpp23.core/sqlite3`) in the storage layer instead
# of #including <sqlpp23/...>. Spike-validated (context/DESIGN_STORAGE_MODULARIZATION.md §3); OFF by
# default because it needs the overlay port reinstalled to ship the module sources (or SQLPP23_MODULES_DIR
# set). Only meaningful with SOURCETRAIL_CXX_MODULES on.
option(SOURCETRAIL_SQLPP23_MODULES "Storage: consume sqlpp23 via `import` (experimental; needs the overlay port's module sources)." OFF)
include(cmake/SourcetrailSqlpp23Modules.cmake)
if(SOURCETRAIL_CXX_MODULES_ENABLED AND SOURCETRAIL_CXX_IMPORT_STD)
	# Module wrappers switch their std #includes to `import std;`; targets with module interfaces must
	# also opt in per-target with the CXX_MODULE_STD property (done in each converted library).
	add_compile_definitions(SRCTRL_IMPORT_STD)
endif()
# CMP0155 (NEW under the 4.4 baseline) makes CMake scan *every* C++20+ source for `import`
# dependencies -- REGARDLESS of SOURCETRAIL_CXX_MODULES, so this must be unconditional. But
# clang-scan-deps chokes on stdexec's preprocessor ("token is not a valid binary operator in a
# preprocessor subexpression"), which is included across lib/lib_cxx/lib_gui, breaking even the plain
# header build. So default module scanning OFF project-wide and turn it back ON per-file only where a
# source actually `import`s (CXX_SCAN_FOR_MODULES source property). FILE_SET CXX_MODULES interface
# units are always scanned/built regardless of this default.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(SOURCETRAIL_CXX_MODULES_ENABLED)
	message(STATUS "C++20 modules: ENABLED (building the Phase-0 module POC; see DESIGN_INDEXER_MODULARIZATION.md)")
	add_subdirectory(src/cxx_modules_poc)
endif()

configure_file(cmake/language_packages.h.in src/lib_core/language_packages.h)

# The Turso comparison backend swaps the storage layer's database types via
# aliases (StorageDb/StorageStmt/StorageQuery), which changes the ABI of the
# Sqlite* storage classes. Every target that includes the storage headers (lib,
# app, tests) must therefore agree on this define, so it is set globally here —
# before any target is created — rather than per-target.
if (SOURCETRAIL_TURSO_COMPARE)
	add_compile_definitions(SOURCETRAIL_TURSO_COMPARE)
endif()

if (SOURCETRAIL_TURSO_CONCURRENT)
	# Global: the concurrent-writer hooks live in PersistentStorage / TaskInjectStorage
	# / TaskFinishParsing (always-compiled TUs), so all TUs must agree on the define.
	add_compile_definitions(SOURCETRAIL_TURSO_CONCURRENT)
endif()

# Either turso mode needs the shim + wrapper targets.
if (SOURCETRAIL_TURSO_COMPARE OR SOURCETRAIL_TURSO_CONCURRENT)
	set(SOURCETRAIL_USE_TURSO ON)
endif()

# Optional Kùzu (LadybugDB) graph backend: mirror the node/edge graph into a
# native property graph for deep traversals / Cypher (see
# context/ROADMAP_ANALYSIS_ENGINES.md). Off by default. Enabling it forces
# BUILD_LADYBUG (the vendored engine) and sets a global define, since the mirror
# hook lives in PersistentStorage (an always-compiled TU) — every TU that sees
# the storage headers must agree on it.
option(SOURCETRAIL_USE_LADYBUG "Mirror the index into an optional Kùzu (LadybugDB) graph backend." OFF)
if (SOURCETRAIL_USE_LADYBUG)
	add_compile_definitions(SOURCETRAIL_USE_LADYBUG)
endif()

# SOURCETRAIL_CLIENT_IDS only changes the body of SqliteIndexStorage::insertElement()
# (the new members are always present, so no ABI change) — it is scoped to that
# single source file in src/lib_core/CMakeLists.txt, not defined globally.


# Make Ninja build verbose as well (https://github.com/ninja-build/ninja/issues/900):
if (CMAKE_VERBOSE_MAKEFILE)
	set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "Verbose Makefile" FORCE)
endif()

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# set Standard build type to Release
set(CMAKE_BUILD_TYPE_INIT "Release")

#[[
#RPATH
if (UNIX)
	set(CMAKE_SKIP_BUILD_RPATH FALSE)
	set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
	set(CMAKE_INSTALL_RPATH "$ORIGIN/lib/:$$ORIGIN/lib/")
endif()
]]

# Specify the requested standards:
# https://cmake.org/cmake/help/latest/prop_tgt/C_STANDARD.html
# https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)

if (APPLE)
	# Sourcetrail_lib_core and Sourcetrail_lib_gui (plus lib_cxx/res_gui) form a declared
	# dependency cycle, so CMake repeats the static archives on the link line for
	# single-pass linkers. Apple's linker resolves archives in multiple passes and
	# instead warns about every repetition ("ignoring duplicate libraries") on each
	# executable link. Silence it where the flag exists (Xcode 15+); untangling the
	# library cycle itself is a larger layering refactor.
	include(CheckLinkerFlag)
	check_linker_flag(CXX "-Wl,-no_warn_duplicate_libraries" linkerSupportsNoWarnDuplicateLibraries)
	if (linkerSupportsNoWarnDuplicateLibraries)
		add_link_options("-Wl,-no_warn_duplicate_libraries")
	endif()
endif()

# Prepare Data ----------------------------------------------------------------

if (UNIX)
	configure_file(scripts/generate_coverage_report.sh generate_coverage_report.sh COPYONLY)
endif()

get_property(isMultiConfigGenerator GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (isMultiConfigGenerator)
	message(FATAL_ERROR "Multi-configuration generators are not supported!")
	# TODO: Test/Support multi-configutration generators
	# foreach(CONFIGURATION_TYPE ${CMAKE_CONFIGURATION_TYPES})
	# 	file(COPY "bin/app/data/"  DESTINATION "${CONFIGURATION_TYPE}/app/data/")
	# 	file(COPY "bin/app/user/"  DESTINATION "${CONFIGURATION_TYPE}/app/user/")
	# 	file(COPY "bin/test/data/" DESTINATION "${CONFIGURATION_TYPE}/test/data/")
	# endforeach()
else()
	set(appDataSourceDir "${CMAKE_SOURCE_DIR}/bin/app/")
	set(appDataTargetDir "${CMAKE_BINARY_DIR}/app/")
	
	message(STATUS "Copying: ${appDataSourceDir} -> ${appDataTargetDir}")
	file(COPY "${appDataSourceDir}" DESTINATION "${appDataTargetDir}")

	if (BUILD_UNIT_TESTS_PACKAGE)
		# The test data contains symlinked files and directories which are used from 'FilePathTestSuite' 
		# and 'FileSystemTestSuite'. CMake, as of this writing (2025-03-07), doesn't copy symlinks 
		# correctly (https://gitlab.kitware.com/cmake/cmake/-/issues/14609) or rather it depends on the 
		# underlying platform.
		
		set(testDataSourceDir "${CMAKE_SOURCE_DIR}/bin/test/data/")
		set(testDataTargetParentDir "${CMAKE_BINARY_DIR}/test/")
		set(testDataTargetDir "${testDataTargetParentDir}data") # Without a '/' at the end!
		
		if (UNIX)
			# Under Linux the symlinks are copied correctly, so copy the data:
			message(STATUS "Copying: ${testDataSourceDir} -> ${testDataTargetDir}")
			file(COPY "${testDataSourceDir}" DESTINATION "${testDataTargetDir}")
		else()
			# Under Windows the symlinks are *not* copied correctly (results in invalid files)
			# and creating symbolic links requires Developer Mode or an elevated process. A
			# directory junction behaves like a directory symlink for our purposes and needs
			# no privileges, so the Windows build relies on junctions exclusively (see also
			# init_repository.py).
			# Note: Temporary test files will show up in version control!
			message(STATUS "Linking (junction): ${testDataSourceDir} -> ${testDataTargetDir}")
			file(MAKE_DIRECTORY "${testDataTargetParentDir}")
			if (NOT EXISTS "${testDataTargetDir}")
				string(REGEX REPLACE "/+$" "" testDataSourceDirNoSlash "${testDataSourceDir}")
				file(TO_NATIVE_PATH "${testDataSourceDirNoSlash}" nativeTestDataSourceDir)
				file(TO_NATIVE_PATH "${testDataTargetDir}" nativeTestDataTargetDir)
				execute_process(
					COMMAND cmd /c mklink /J "${nativeTestDataTargetDir}" "${nativeTestDataSourceDir}"
					RESULT_VARIABLE createJunctionResult
					OUTPUT_QUIET
				)
				if (NOT createJunctionResult EQUAL 0)
					message(FATAL_ERROR "Could not create the test data junction: ${createJunctionResult}")
				endif()
			endif()
		endif()
	endif()
endif()

# FlatBuffers ------------------------------------------------------------------

find_package(flatbuffers CONFIG REQUIRED)
message(STATUS "Found FlatBuffers ${flatbuffers_VERSION}")

add_library(External_lib_flatbuffers INTERFACE)
target_link_libraries(External_lib_flatbuffers INTERFACE flatbuffers::flatbuffers)

# stdexec (senders/receivers) — found here, before thoth-ipc, so libipc's own
# find_package(stdexec) inside add_subdirectory reuses the same (fully-defined)
# export set. Finding it in both scopes otherwise trips stdexec's config
# re-entrancy check ("some but not all targets already defined").
find_package(stdexec CONFIG REQUIRED)
message(STATUS "Found stdexec (provides STDEXEC::stdexec)")

# Agent-control channel — declared here (before thoth-ipc) because it decides
# whether libipc is built with its optional async-receive (senders/receivers)
# layer below. Consumed again in src/lib_core for the schema/compile-definition wiring.
option(SOURCETRAIL_AGENT_CONTROL "Build the agent-UI control channel (thoth-ipc + FlatBuffers)" ON)

# thoth-ipc ----------------------------------------------------------------------
# Built from the vendored git submodule (submodules/thoth-ipc) rather than a
# second FetchContent clone: the submodule commit is the single source of truth
# and the pin. `include(FetchContent)` is retained for the blocks below.

include(FetchContent)

set(THOTH_IPC_DIR "${CMAKE_SOURCE_DIR}/submodules/thoth-ipc/cpp/thoth-ipc")
if (NOT EXISTS "${THOTH_IPC_DIR}/CMakeLists.txt")
	message(FATAL_ERROR
		"thoth-ipc submodule is not checked out at ${THOTH_IPC_DIR}.\n"
		"Run: git submodule update --init --recursive submodules/thoth-ipc")
endif()

set(THOTH_IPC_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(THOTH_IPC_BUILD_DEMOS      OFF CACHE BOOL "" FORCE)
set(THOTH_IPC_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(THOTH_IPC_BUILD_PROTO      OFF CACHE BOOL "" FORCE)
# Layer 2 async_recv (senders/receivers) — only when the agent-control channel
# needs it. It implies THOTH_IPC_NOTIFY_FD, which is POSIX-only for now (the Windows
# named-event backend is not yet implemented upstream), so gate it off Windows;
# the client falls back to a blocking reader thread there. thoth-ipc resolves its
# stdexec dependency via find_package first (reusing our vcpkg stdexec).
if (SOURCETRAIL_AGENT_CONTROL AND NOT WIN32)
	set(THOTH_IPC_STDEXEC ON CACHE BOOL "" FORCE)
endif()

# Module build: also build thoth-ipc's opt-in named module (target thoth_ipc_module,
# sqlpp23-style re-export of the header surface) so srctrl.interprocess can
# `import thoth.ipc;` instead of GMF-including the thoth headers. Same entities either
# way (the interface unit declares them in its own global module fragment), so the
# classic OFF build is unaffected.
if (SOURCETRAIL_CXX_MODULES_ENABLED)
	set(THOTH_IPC_BUILD_MODULES ON CACHE BOOL "" FORCE)
else()
	set(THOTH_IPC_BUILD_MODULES OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory("${THOTH_IPC_DIR}" "${CMAKE_BINARY_DIR}/_deps/thoth-ipc-build")
message(STATUS "Built thoth-ipc from submodule submodules/thoth-ipc")

add_library(External_lib_ipc INTERFACE)
# The C++ library target is `thoth_ipc` (underscore): cpp/thoth-ipc/src has its
# own project(thoth_ipc), so add_library(${PROJECT_NAME}) names it thoth_ipc and
# produces libthoth_ipc.a — the rename of the old `ipc` target.
target_link_libraries(External_lib_ipc INTERFACE thoth_ipc)
target_include_directories(External_lib_ipc SYSTEM INTERFACE
	"${THOTH_IPC_DIR}/include"
)

# qt-json-query ----------------------------------------------------------------

# Suppress qt-json-query's own tests. BUILD_TESTING is not defined yet at this
# point (include(CTest) further down defines it, defaulting to ON), so "saving"
# it would restore an empty value into the cache -- which include(CTest) then
# treats as OFF, silently disabling enable_testing() and leaving ctest with no
# tests. Force it OFF only for the FetchContent scope and drop it from the cache
# afterwards so include(CTest) can establish its normal default.
set(BUILD_TESTING         OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKING    OFF CACHE BOOL "" FORCE)
set(JSON_QUERY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JSON_QUERY_BUILD_PERF     OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	qt-json-query
	SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/qt-json-query"
)
FetchContent_MakeAvailable(qt-json-query)
unset(BUILD_TESTING CACHE)
message(STATUS "Integrated qt-json-query from submodules/")

add_library(External_lib_json_query INTERFACE)
target_link_libraries(External_lib_json_query INTERFACE json_query)

# Qt ---------------------------------------------------------------------------

set(QT_NO_PRIVATE_MODULE_WARNING ON)

set(QT_MIN_VERSION 6.8.3)
# set (QT_MAX_VERSION_HEX 0x060802)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED 
	COMPONENTS
		Network 
		# PrintSupport 
		Svg
		Widgets 
)
message(STATUS "Found Qt ${Qt6_VERSION}")

add_library(External_lib_qt INTERFACE)

# Enable warnings for deprecated APIs:
target_compile_definitions(External_lib_qt
	INTERFACE
		# QT_DISABLE_DEPRECATED_UP_TO=${QT_MAX_VERSION_HEX}
		# QT_ENABLE_STRICT_MODE_UP_TO=${QT_MAX_VERSION_HEX}
		QT_NO_FOREACH
		QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
		QT_NO_QASCONST
		QT_USE_NODISCARD_FILE_OPEN
		QT_NO_QSNPRINTF
		
		# Prevent creating QPixmap inadvertently with 'const char []':
		QT_NO_IMAGEFORMAT_XPM
)

target_link_libraries(External_lib_qt
	INTERFACE
		Qt6::Network
		# Qt6::PrintSupport
		Qt6::Svg
		Qt6::Widgets
)

# SQLite3 ----------------------------------------------------------------------

if (isVcpkgBuild)
	find_package(unofficial-sqlite3 CONFIG REQUIRED)
	if (NOT DEFINED SQLite3_VERSION)
		# Use quotes to indicate a version literal:
		set(SQLite3_VERSION "\"3.50.4\"")
	endif()
	add_library(External_lib_sqlite3 ALIAS unofficial::sqlite3::sqlite3)
else()
	find_package(SQLite3 REQUIRED)
	add_library(External_lib_sqlite3 ALIAS SQLite::SQLite3)
endif()
message(STATUS "Found SQLite3 ${SQLite3_VERSION}")

# sqlpp23 (type-safe SQL DSL) ---------------------------------------------------
# Vendored via the vcpkg overlay port (vcpkg-overlay-ports/sqlpp23). Used by the
# storage layer's incremental migration off raw SQL; see
# context/DESIGN_SQLPP23_MIGRATION.md. Header-only; the SQLite3 component pulls the
# connector. Requires Clang >= 20.1 / GCC >= 14.2 / MSVC >= 19.44 (C++23).
find_package(Sqlpp23 CONFIG REQUIRED COMPONENTS SQLite3)

# toml++ ------------------------------------------------------------------------------

find_package(tomlplusplus CONFIG REQUIRED)
message(STATUS "Found toml++ ${tomlplusplus_VERSION}")

add_library(External_lib_tomlplusplus INTERFACE)
target_include_directories(External_lib_tomlplusplus SYSTEM INTERFACE
	"${tomlplusplus_DIR}/../../include"
)
target_compile_definitions(External_lib_tomlplusplus INTERFACE TOML_HEADER_ONLY=1)
target_compile_features(External_lib_tomlplusplus INTERFACE cxx_std_17)

# nlohmann-json -----------------------------------------------------------------------

find_package(nlohmann_json CONFIG REQUIRED)
message(STATUS "Found nlohmann_json ${nlohmann_json_VERSION}")

# stdexec (senders/receivers) is found earlier, before the thoth-ipc block.

add_library(External_lib_nlohmann_json INTERFACE)
target_link_libraries(External_lib_nlohmann_json INTERFACE nlohmann_json::nlohmann_json)
target_compile_features(External_lib_nlohmann_json INTERFACE cxx_std_17)

# glaze (compile-time reflection + JSON) --------------------------------------------

find_package(glaze CONFIG REQUIRED)
message(STATUS "Found glaze ${glaze_VERSION}")

add_library(External_lib_glaze INTERFACE)
target_link_libraries(External_lib_glaze INTERFACE glaze::glaze)

# Rust crates via Corrosion ----------------------------------------------------
# Fetched here (before src/external and src/lib_core) so the imported turso_shim
# static-lib target exists when those subdirectories link against it. The Rust
# indexer crate is imported later in its own section; only the shared Corrosion
# fetch and the turso_shim import need to happen this early.

if (BUILD_RUST_LANGUAGE_PACKAGE OR SOURCETRAIL_TURSO_COMPARE)
	FetchContent_Declare(
		corrosion
		GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
		GIT_TAG        v0.5
		GIT_SUBMODULES ""
	)
	FetchContent_MakeAvailable(corrosion)
endif()

if (SOURCETRAIL_USE_TURSO)
	corrosion_import_crate(
		MANIFEST_PATH "${CMAKE_SOURCE_DIR}/src/turso_shim/Cargo.toml"
		CRATES turso_shim
	)
	message(STATUS "Turso enabled (compare=${SOURCETRAIL_TURSO_COMPARE} concurrent=${SOURCETRAIL_TURSO_CONCURRENT}) — building turso_shim via Corrosion")
endif()

# LadybugDB (Kùzu) — embedded graph database ------------------------------------
# Optional dependency for a future graph storage/traversal backend (see
# context/ROADMAP_ANALYSIS_ENGINES.md). Off by default: enabling it pulls in the
# full Kùzu build (bundled third_party + system OpenSSL 3). Built static-only,
# without the shell/tests/language-APIs, and exposed as External_lib_ladybug.

option(BUILD_LADYBUG "Build the LadybugDB (Kùzu) embedded graph database dependency." OFF)

if (BUILD_LADYBUG OR SOURCETRAIL_USE_LADYBUG)
	set(BUILD_SHELL              OFF CACHE BOOL "" FORCE)
	set(BUILD_TESTS             OFF CACHE BOOL "" FORCE)
	set(BUILD_BENCHMARK        OFF CACHE BOOL "" FORCE)
	set(BUILD_SINGLE_FILE_HEADER OFF CACHE BOOL "" FORCE)
	set(BUILD_SHARED_LBUG      OFF CACHE BOOL "" FORCE)
	set(BUILD_STATIC_LBUG       ON CACHE BOOL "" FORCE)
	set(AUTO_UPDATE_GRAMMAR    OFF CACHE BOOL "" FORCE)

	add_subdirectory(submodules/ladybug EXCLUDE_FROM_ALL)

	add_library(External_lib_ladybug INTERFACE)
	target_link_libraries(External_lib_ladybug INTERFACE lbug)
	target_include_directories(External_lib_ladybug SYSTEM INTERFACE
		"${CMAKE_SOURCE_DIR}/submodules/ladybug/src/include"
		"${CMAKE_BINARY_DIR}/submodules/ladybug/src/include")

	message(STATUS "LadybugDB (Kùzu) enabled — building the lbug static library")
endif()

# KDDockWidgets — advanced docking framework (multi-monitor detach/reattach) ---
# Built in-tree from the vendored submodule (submodules/kddockwidgets, pinned to
# v2.4.0) so it links against the exact same Homebrew Qt as the application. A
# vcpkg port can't do that here — Qt is external to vcpkg — and mixing a second
# Qt copy would break the ABI. QtWidgets frontend only; static, to avoid shipping
# a separate runtime library. Exposed as External_lib_kddockwidgets.
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/submodules/kddockwidgets/CMakeLists.txt")
	message(FATAL_ERROR
		"kddockwidgets submodule is not checked out at submodules/kddockwidgets.\n"
		"Run: git submodule update --init submodules/kddockwidgets")
endif()

set(KDDockWidgets_QT6            ON        CACHE BOOL   "" FORCE)
set(KDDockWidgets_FRONTENDS      qtwidgets CACHE STRING "" FORCE)
set(KDDockWidgets_STATIC         ON        CACHE BOOL   "" FORCE)
set(KDDockWidgets_EXAMPLES       OFF       CACHE BOOL   "" FORCE)
set(KDDockWidgets_TESTS          OFF       CACHE BOOL   "" FORCE)
set(KDDockWidgets_DOCS           OFF       CACHE BOOL   "" FORCE)
set(KDDockWidgets_NO_SPDLOG      ON        CACHE BOOL   "" FORCE)
set(KDDockWidgets_DEVELOPER_MODE OFF       CACHE BOOL   "" FORCE)
# Upstream spells this option 'KDockWidgets_...' (missing D) — match it verbatim.
# Puts <kddockwidgets/MainWindow.h> / <DockWidget.h> on the include path.
set(KDockWidgets_PRETTY_QTWIDGETS_HEADERS ON CACHE BOOL "" FORCE)

add_subdirectory(submodules/kddockwidgets EXCLUDE_FROM_ALL)

# KDAB::kddockwidgets is an in-build ALIAS (src/CMakeLists.txt); it already
# propagates its public fwd_headers include dir, so no manual include wiring.
add_library(External_lib_kddockwidgets INTERFACE)
target_link_libraries(External_lib_kddockwidgets INTERFACE KDAB::kddockwidgets)

message(STATUS "Built KDDockWidgets (QtWidgets, static) from submodule submodules/kddockwidgets")

# AidKit  -----------------------------------------------------------------------------------

add_subdirectory(src/lib_aidkit)

# External Lib CppSQlite3  ------------------------------------------------------------------

add_subdirectory(src/external)
# message(STATUS "Using CppSQLite3 ${CppSQLite3_VERSION}")

# Lib Gui ----------------------------------------------------------------------

# Generated into lib's binary dir (on lib's include path): its only consumer is setupApp, which lives in
# the base lib now. Keeping it out of lib_gui lets the headless indexer's bootstrap compile without lib_gui.
configure_file(cmake/productVersion.h.in src/lib_core/productVersion.h)

add_subdirectory(src/resources)
add_subdirectory(src/lib_gui)

# Lib --------------------------------------------------------------------------

if (UNIX)
	find_package(Threads REQUIRED)
endif()

add_subdirectory(src/lib_core)

#
# Resolve cyclic dependencies between Sourcetrail_lib_core and Sourcetrail_lib_gui:
#

set_target_properties(Sourcetrail_lib_core
	PROPERTIES
		LINK_INTERFACE_MULTIPLICITY 3
)
set_target_properties(Sourcetrail_lib_gui
	PROPERTIES
		LINK_INTERFACE_MULTIPLICITY 3
)

# Lib Cxx ----------------------------------------------------------------------

if (BUILD_CXX_LANGUAGE_PACKAGE)

	# Integrate LLVM/LibTooling. 
	# For further details see https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project

	set(CLANG_MIN_VERSION 18)
	set(CLANG_MAX_VERSION 23)
	
	if (DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
		set(savedMsvcRuntimeLibrary "${CMAKE_MSVC_RUNTIME_LIBRARY}")
	endif()

	find_package(Clang CONFIG REQUIRED) # Minimum version and/or version range xx...<yy doesn't work.
	message(STATUS "Found LLVM ${LLVM_VERSION}: ${LLVM_INCLUDE_DIRS}")
	checkVersionRange("Clang" ${LLVM_VERSION} ${CLANG_MIN_VERSION} ${CLANG_MAX_VERSION})

	# LLVMConfig.cmake leaks the CRT selection of LLVM's own build into the including
	# scope via set(CMAKE_MSVC_RUNTIME_LIBRARY ...), which would silently switch every
	# target defined after this point to LLVM's CRT. Restore our own selection:
	if (DEFINED savedMsvcRuntimeLibrary)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "${savedMsvcRuntimeLibrary}")
	else()
		unset(CMAKE_MSVC_RUNTIME_LIBRARY)
	endif()

	if (WIN32 AND TARGET LLVMDebugInfoPDB)
		# The prebuilt LLVM archives (clang+llvm-*-x86_64-pc-windows-msvc.tar.xz) hard-code
		# the absolute diaguids.lib path of the Visual Studio installation LLVM was built
		# with (e.g. ".../2022/Enterprise/DIA SDK/..."). Remap it to the active Visual
		# Studio installation (VSINSTALLDIR is set by vcvars64.bat):
		get_target_property(llvmDebugInfoPdbLibraries LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
		set(remappedLibraries "")
		foreach(library IN LISTS llvmDebugInfoPdbLibraries)
			if (library MATCHES "DIA SDK.*diaguids\\.lib$" AND NOT EXISTS "${library}")
				file(TO_CMAKE_PATH "$ENV{VSINSTALLDIR}" vsInstallDir)
				set(localDiaGuidsLib "${vsInstallDir}/DIA SDK/lib/amd64/diaguids.lib")
				if (EXISTS "${localDiaGuidsLib}")
					message(STATUS "Remapping diaguids.lib: ${library} -> ${localDiaGuidsLib}")
					set(library "${localDiaGuidsLib}")
				else()
					message(WARNING "diaguids.lib not found at '${localDiaGuidsLib}' - is VSINSTALLDIR set (vcvars64.bat)?")
				endif()
			endif()
			list(APPEND remappedLibraries "${library}")
		endforeach()
		set_target_properties(LLVMDebugInfoPDB PROPERTIES INTERFACE_LINK_LIBRARIES "${remappedLibraries}")
	endif()

	add_library(External_lib_clang INTERFACE)
	
	separate_arguments(llvmDefinitions NATIVE_COMMAND ${LLVM_DEFINITIONS})
	target_compile_definitions(External_lib_clang
		INTERFACE
			${llvmDefinitions}
	)
	target_include_directories(External_lib_clang SYSTEM
		INTERFACE
			${LLVM_INCLUDE_DIRS}
			${CLANG_INCLUDE_DIRS}
	)
	
	# 'LLVM_LINK_LLVM_DYLIB' is set when LLVM/Clang have been build into seperate single libraries.
	# Apparently this means for LLVM 'libLLVM' and for Clang 'libclang-cpp'.
	# For further details see https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project
	
	if (LLVM_LINK_LLVM_DYLIB) # Should be CLANG_LINK_CLANG_DYLIB in future LLVM releases?
		set(llvmLibraries LLVM)
		set(clangLibraries clang-cpp)
	else()
		llvm_map_components_to_libnames(llvmLibraries
			${LLVM_TARGETS_TO_BUILD} support core libdriver passes option
		)
		# LibTooling libraries:
		set(clangLibraries
			clangASTMatchers
			clangFrontend
			clangSerialization
			clangDriver
			clangTooling
			clangParse
			clangSema
			clangStaticAnalyzerFrontend
			clangStaticAnalyzerCheckers
			clangStaticAnalyzerCore
			clangAnalysis
			clangRewriteFrontend
			clangEdit
			clangAST
			clangLex
			clangBasic
		)
	endif()
		
	target_link_libraries(External_lib_clang
		INTERFACE
			${llvmLibraries}
			${clangLibraries}
	)
	
	# Copy required headers:
	if (isVcpkgBuild)
		set(headerSourceDir "${LLVM_INSTALL_PREFIX}/lib/clang/${LLVM_VERSION_MAJOR}/include")
	else()
		set(headerSourceDir "${LLVM_INSTALL_PREFIX}/lib/clang/${LLVM_VERSION_MAJOR}/include")
	endif()
	set(headerTargetDir "${CMAKE_BINARY_DIR}/app/data/cxx/include/")

	message(STATUS "Copying LLVM header: ${headerSourceDir} -> ${headerTargetDir}")
	file(COPY "${headerSourceDir}" DESTINATION "${headerTargetDir}")
	
	add_subdirectory(src/lib_cxx)
else()
	message(STATUS "Building the C++ indexer is disabled. You can enable it by setting 'BUILD_CXX_LANGUAGE_PACKAGE' to 'ON'.")
endif()


# Indexer App ------------------------------------------------------------------

add_subdirectory(src/indexer)

# Rust Indexer -----------------------------------------------------------------

if (BUILD_RUST_LANGUAGE_PACKAGE)
	# Corrosion is fetched earlier (shared with SOURCETRAIL_TURSO_COMPARE).
	corrosion_import_crate(
		MANIFEST_PATH "${CMAKE_SOURCE_DIR}/src/rust_indexer/Cargo.toml"
		CRATES sourcetrail-rust-indexer
	)

	# The crate's build.rs needs flatc, which is not on PATH (especially on
	# Windows), so point it at the vcpkg-provided host tool:
	corrosion_set_env_vars(sourcetrail_rust_indexer "FLATC=$<TARGET_FILE:flatbuffers::flatc>")

	# Copy the Rust indexer binary to app/ so AppPath::getRustIndexerFilePath() finds it.
	# Corrosion produces an IMPORTED target, so we use a custom target with add_dependencies.
	add_custom_target(copy_rust_indexer ALL
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:sourcetrail_rust_indexer>
			"${CMAKE_BINARY_DIR}/app/$<TARGET_FILE_NAME:sourcetrail_rust_indexer>"
		COMMENT "Copying sourcetrail_rust_indexer to app/"
		DEPENDS sourcetrail_rust_indexer
	)

	message(STATUS "Rust language package enabled — building sourcetrail_rust_indexer via Corrosion")
endif()

# Swift Indexer ----------------------------------------------------------------

if (BUILD_SWIFT_LANGUAGE_PACKAGE)
	find_program(SWIFT_EXECUTABLE swift)
	if (NOT SWIFT_EXECUTABLE)
		message(FATAL_ERROR "Swift language package enabled but no 'swift' executable was found in PATH.")
	endif()

	set(SWIFT_INDEXER_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/src/swift_indexer")
	set(SWIFT_INDEXER_BUILD_DIR "${CMAKE_BINARY_DIR}/swift_indexer")
	set(SWIFT_INDEXER_BINARY_NAME "sourcetrail_swift_indexer${CMAKE_EXECUTABLE_SUFFIX}")
	set(SWIFT_INDEXER_BINARY_PATH "${SWIFT_INDEXER_BUILD_DIR}/release/${SWIFT_INDEXER_BINARY_NAME}")
	set(SWIFT_INDEXER_APP_BINARY_PATH "${CMAKE_BINARY_DIR}/app/${SWIFT_INDEXER_BINARY_NAME}")
	set(SWIFT_INDEXER_SCHEMA_DIR "${CMAKE_SOURCE_DIR}/abi-schemas/ipc-indexer")
	set(SWIFT_INDEXER_GENERATED_DIR "${SWIFT_INDEXER_BUILD_DIR}/generated")
	set(
		SWIFT_INDEXER_PACKAGE_GENERATED_DIR
		"${SWIFT_INDEXER_PACKAGE_DIR}/Sources/SourcetrailSwiftIndexerCore/GeneratedFromCMake"
	)

	set(SWIFT_INDEXER_SCHEMAS
		"${SWIFT_INDEXER_SCHEMA_DIR}/indexer_command.fbs"
		"${SWIFT_INDEXER_SCHEMA_DIR}/intermediate_storage.fbs"
		"${SWIFT_INDEXER_SCHEMA_DIR}/indexing_status.fbs"
		"${SWIFT_INDEXER_SCHEMA_DIR}/garbage_collector.fbs"
	)

	set(SWIFT_INDEXER_GENERATED_SOURCES)
	foreach(SCHEMA_FILE ${SWIFT_INDEXER_SCHEMAS})
		get_filename_component(SCHEMA_NAME ${SCHEMA_FILE} NAME_WE)
		set(GENERATED_SWIFT "${SWIFT_INDEXER_GENERATED_DIR}/${SCHEMA_NAME}_generated.swift")
		list(APPEND SWIFT_INDEXER_GENERATED_SOURCES "${GENERATED_SWIFT}")
		add_custom_command(
			OUTPUT "${GENERATED_SWIFT}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${SWIFT_INDEXER_GENERATED_DIR}"
			COMMAND flatbuffers::flatc --swift --gen-all --gen-object-api -o "${SWIFT_INDEXER_GENERATED_DIR}" "${SCHEMA_FILE}"
			DEPENDS "${SCHEMA_FILE}"
			COMMENT "Compiling Swift FlatBuffer schema ${SCHEMA_NAME}.fbs"
			VERBATIM
		)
	endforeach()

	add_custom_command(
		OUTPUT "${SWIFT_INDEXER_PACKAGE_GENERATED_DIR}"
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${SWIFT_INDEXER_PACKAGE_GENERATED_DIR}"
		COMMAND ${CMAKE_COMMAND} -E create_symlink "${SWIFT_INDEXER_GENERATED_DIR}" "${SWIFT_INDEXER_PACKAGE_GENERATED_DIR}"
		DEPENDS ${SWIFT_INDEXER_GENERATED_SOURCES}
		COMMENT "Linking Swift generated schemas into package sources"
		VERBATIM
	)

	file(GLOB_RECURSE SWIFT_INDEXER_SOURCES CONFIGURE_DEPENDS
		"${SWIFT_INDEXER_PACKAGE_DIR}/Package.swift"
		"${SWIFT_INDEXER_PACKAGE_DIR}/Sources/*.swift"
	)

	add_custom_command(
		OUTPUT "${SWIFT_INDEXER_BINARY_PATH}"
		COMMAND ${SWIFT_EXECUTABLE} build
			--configuration release
			--package-path "${SWIFT_INDEXER_PACKAGE_DIR}"
			--build-path "${SWIFT_INDEXER_BUILD_DIR}"
			--product sourcetrail_swift_indexer
		DEPENDS
			${SWIFT_INDEXER_SOURCES}
			${SWIFT_INDEXER_GENERATED_SOURCES}
			"${SWIFT_INDEXER_PACKAGE_GENERATED_DIR}"
		COMMENT "Building sourcetrail_swift_indexer via Swift Package Manager"
		VERBATIM
	)

	add_custom_target(sourcetrail_swift_indexer DEPENDS "${SWIFT_INDEXER_BINARY_PATH}")

	add_custom_target(copy_swift_indexer ALL
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${SWIFT_INDEXER_BINARY_PATH}"
			"${SWIFT_INDEXER_APP_BINARY_PATH}"
		COMMENT "Copying sourcetrail_swift_indexer to app/"
		DEPENDS sourcetrail_swift_indexer
	)

	message(STATUS "Swift language package enabled — building sourcetrail_swift_indexer via Swift Package Manager")
endif()

# Zig Indexer ------------------------------------------------------------------

if (BUILD_ZIG_LANGUAGE_PACKAGE)
	find_program(ZIG_EXECUTABLE zig)
	if (NOT ZIG_EXECUTABLE)
		message(FATAL_ERROR "Zig language package enabled but no 'zig' executable was found in PATH.")
	endif()
	find_program(FLATCC_EXECUTABLE flatcc)
	if (NOT FLATCC_EXECUTABLE)
		message(FATAL_ERROR "Zig language package enabled but no 'flatcc' executable was found in PATH (brew install flatcc).")
	endif()
	# flatcc install prefix = parent of the dir holding the flatcc binary
	# (so <prefix>/include/flatcc and <prefix>/lib/libflatccrt.a resolve).
	get_filename_component(FLATCC_BIN_DIR "${FLATCC_EXECUTABLE}" DIRECTORY)
	get_filename_component(FLATCC_PREFIX "${FLATCC_BIN_DIR}" DIRECTORY)

	set(ZIG_INDEXER_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/src/zig_indexer")
	set(ZIG_INDEXER_BUILD_DIR "${CMAKE_BINARY_DIR}/zig_indexer")
	set(ZIG_INDEXER_BINARY_NAME "sourcetrail_zig_indexer${CMAKE_EXECUTABLE_SUFFIX}")
	set(ZIG_INDEXER_BINARY_PATH "${ZIG_INDEXER_BUILD_DIR}/bin/${ZIG_INDEXER_BINARY_NAME}")
	set(ZIG_INDEXER_APP_BINARY_PATH "${CMAKE_BINARY_DIR}/app/${ZIG_INDEXER_BINARY_NAME}")
	# The Zig indexer generates flatcc bindings from the shared IPC ABI schemas
	# (see abi-schemas/README.md) — the same .fbs the C++ side compiles with flatc.
	set(ZIG_INDEXER_SCHEMA_DIR "${CMAKE_SOURCE_DIR}/abi-schemas/ipc-indexer")

	# Sources that should retrigger a rebuild (the thoth-ipc Zig port + the
	# indexer package + the shared schemas).
	file(GLOB_RECURSE ZIG_INDEXER_SOURCES CONFIGURE_DEPENDS
		"${ZIG_INDEXER_PACKAGE_DIR}/build.zig"
		"${ZIG_INDEXER_PACKAGE_DIR}/build.zig.zon"
		"${ZIG_INDEXER_PACKAGE_DIR}/src/*.zig"
		"${ZIG_INDEXER_PACKAGE_DIR}/tools/*.sh"
		"${ZIG_INDEXER_SCHEMA_DIR}/*.fbs"
		"${CMAKE_SOURCE_DIR}/submodules/thoth-ipc/zig/thoth-ipc/src/*.zig"
	)

	add_custom_command(
		OUTPUT "${ZIG_INDEXER_BINARY_PATH}"
		COMMAND ${ZIG_EXECUTABLE} build
			--build-file "${ZIG_INDEXER_PACKAGE_DIR}/build.zig"
			--cache-dir "${ZIG_INDEXER_BUILD_DIR}/.zig-cache"
			--prefix "${ZIG_INDEXER_BUILD_DIR}"
			-Doptimize=ReleaseSafe
			"-Dschema-dir=${ZIG_INDEXER_SCHEMA_DIR}"
			"-Dflatcc-prefix=${FLATCC_PREFIX}"
			install
		WORKING_DIRECTORY "${ZIG_INDEXER_PACKAGE_DIR}"
		DEPENDS ${ZIG_INDEXER_SOURCES}
		COMMENT "Building sourcetrail_zig_indexer via zig build"
		VERBATIM
	)

	add_custom_target(sourcetrail_zig_indexer DEPENDS "${ZIG_INDEXER_BINARY_PATH}")

	add_custom_target(copy_zig_indexer ALL
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${ZIG_INDEXER_BINARY_PATH}"
			"${ZIG_INDEXER_APP_BINARY_PATH}"
		COMMENT "Copying sourcetrail_zig_indexer to app/"
		DEPENDS sourcetrail_zig_indexer
	)

	message(STATUS "Zig language package enabled — building sourcetrail_zig_indexer via zig build (flatcc: ${FLATCC_PREFIX})")
endif()

# App --------------------------------------------------------------------------

configure_file(src/app/Sourcetrail.ico app/Sourcetrail.ico COPYONLY)
if (UNIX)
	configure_file(scripts/Sourcetrail.sh      app/Sourcetrail.sh COPYONLY)
	configure_file(scripts/resetPreferences.sh app/resetPreferences.sh COPYONLY)
	configure_file(scripts/_gdbinit            app/.gdbinit COPYONLY)
endif()
add_subdirectory(src/app)

# Test ----------------------------------------------------------------------

set(CATCH2_MIN_VERSION 3.7)
set(CATCH2_MAX_VERSION 3.13)

if (BUILD_UNIT_TESTS_PACKAGE)
	include(CTest)

	find_package(Catch2 CONFIG REQUIRED)
	message(STATUS "Found Catch2 ${Catch2_VERSION}")
	checkVersionRange("Catch2" ${Catch2_VERSION} ${CATCH2_MIN_VERSION} ${CATCH2_MAX_VERSION})
	include(Catch)
	add_subdirectory(src/test_aidkit)
	add_subdirectory(src/test)
else()
	message(STATUS "Building the tests is disabled. You can enable them by setting 'BUILD_UNIT_TESTS_PACKAGE' to 'ON'.")
endif()

# Create license information:
include(cmake/licenses.cmake)
configureLicenseFile(src/lib_gui/licenses.h)

# Some sanity checks:
# Note: We doing these checks last so the messages are at the end of a possible long output from cmake.

if (NOT BUILD_CXX_LANGUAGE_PACKAGE)
	message(WARNING "Building ONLY the GUI, because all language indexers are disabled!")
endif()

# prohibit in-source-builds
if (${CMAKE_BINARY_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
	message(FATAL_ERROR "In-source-builds are not supported!")
endif()

if (BUILD_PYTHON_LANGUAGE_PACKAGE)
	message(FATAL_ERROR "Python support has been removed!")
	# See:
	# - https://github.com/petermost/Sourcetrail/issues/34
	# - https://github.com/CoatiSoftware/SourcetrailPythonIndexer
endif()

# Enable this code to find out *all* imported targets:
# get_property(importedTargets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY IMPORTED_TARGETS)
# message(STATUS "Imported targets: ${importedTargets}")

# Installing/Packaging ---------------------------------------------------------

install(DIRECTORY "${CMAKE_BINARY_DIR}/app/" DESTINATION "${PROJECT_NAME}/app/" USE_SOURCE_PERMISSIONS)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

set(CPACK_PACKAGE_VERSION ${CMAKE_PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Source code explorer")
set(CPACK_PACKAGE_DESCRIPTION "Free and open-source cross-platform source explorer that helps you get productive on unfamiliar source code.")

set(CPACK_PACKAGE_CONTACT "P. Most <pmost@pera-software.com>")
set(CPACK_PACKAGE_VENDOR "PERA Software Solutions GmbH")
set(CPACK_PACKAGE_HOMEPAGE_URL ${PROJECT_HOMEPAGE_URL})
set(CPACK_PACKAGE_ICON "${CMAKE_BINARY_DIR}/app/Sourcetrail.ico")

if (isVcpkgBuild)
	set(CPACK_GENERATOR ZIP)
else()
	if (UNIX)
		set(CPACK_GENERATOR DEB)
		set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
		set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/")
	else()
		set(CPACK_GENERATOR ZIP)
	endif()
endif()

include(CPack)
