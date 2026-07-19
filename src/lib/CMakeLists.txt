# Sources: collected with GLOB_RECURSE + CONFIGURE_DEPENDS so CMake re-runs
# automatically when files are added or removed.
file(GLOB_RECURSE Sourcetrail_lib_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
# The agent_control prototype files are standalone references (see their
# README), not part of the build; they use POSIX fork()/wait() and do not
# compile on Windows.
list(FILTER Sourcetrail_lib_SOURCES EXCLUDE REGEX "component/controller/agent_control/prototype/")
# CXX settings sources are always included; lib_cxx itself is still optional.

# FlatBuffer schema files — included so IDEs show them in the project tree.
file(GLOB Sourcetrail_lib_FBS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/data/indexer/interprocess/schemas/*.fbs"
)

add_library(Sourcetrail_lib
	${CMAKE_CURRENT_BINARY_DIR}/language_packages.h
	${Sourcetrail_lib_SOURCES}
	${Sourcetrail_lib_FBS}
)
setDefaultTargetOptions(Sourcetrail_lib)

# Include directories: every subdirectory that contains at least one source file,
# derived automatically — no hand-maintained list needed.
file(GLOB_RECURSE _lib_all_sources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
set(_lib_include_dirs "")
foreach(_f ${_lib_all_sources})
	get_filename_component(_d "${_f}" DIRECTORY)
	list(APPEND _lib_include_dirs "${_d}")
endforeach()
list(REMOVE_DUPLICATES _lib_include_dirs)

target_include_directories(Sourcetrail_lib
	PUBLIC
		${_lib_include_dirs}
		.
		"${CMAKE_BINARY_DIR}/src/lib"
)

# Phase 2 of the C++20-modules migration (context/DESIGN_INDEXER_MODULARIZATION.md): the srctrl.utility
# module (currently just the :enums partition = utilityEnum.h). PUBLIC so other targets can import it.
# The .cppm are not GLOB'd, so they're built only when modules are enabled; the remaining lib .cpp
# files keep #including utilityEnum.h as ordinary TUs -- it's header-only, so they coexist with the
# module.
if(SOURCETRAIL_CXX_MODULES_ENABLED)
	target_sources(Sourcetrail_lib
		PUBLIC
			FILE_SET CXX_MODULES FILES
				utility/qt/srctrl_qt.cppm
				utility/qt/srctrl_qt-core.cppm
				utility/qt/srctrl_qt-meta.cppm
				utility/logging/srctrl_logging.cppm
				utility/srctrl_utility.cppm
				utility/srctrl_utility-enum.cppm
				utility/srctrl_utility-cache.cppm
				utility/srctrl_utility-types.cppm
				utility/srctrl_utility-string.cppm
				utility/srctrl_utility-math.cppm
				utility/srctrl_utility-time.cppm
				utility/file/srctrl_file.cppm
				data/srctrl_data.cppm
				data/srctrl_data-types.cppm
				data/srctrl_data-name.cppm
				data/srctrl_data-location.cppm
				data/srctrl_data-graph.cppm
				data/srctrl_data-search.cppm
				data/srctrl_data-tooltip.cppm
				data/storage/srctrl_storage.cppm
				data/storage/srctrl_storage-types.cppm
				data/storage/srctrl_storage-interface.cppm
				data/storage/srctrl_storage-error.cppm
				data/storage/srctrl_storage-access.cppm)
	# Workaround for LLVM #166068: in reduced-BMI mode (clang's default since 20) the replaceable global
	# operator new/delete declarations are pruned from the BMI, so a consumer that instantiates a module
	# type whose inline members allocate (e.g. FilePath -> std::filesystem::path -> std::__libcpp_allocate
	# -> __builtin_operator_new) crashes in CodeGen (EmitBuiltinNewDeleteCall, CGExprCXX.cpp:1384) because
	# the predeclared global operator is missing. There is no source-level injection that forces those decls
	# back in (the fix is in clang's ASTWriter, PR #167468); disable reduced BMI until our toolchain ships it.
	# PRIVATE: the flag only has to be on the BMI *producer* -- it makes this target's BMIs full, so any
	# consumer (here or in another target) reads the retained operator new/delete and never needs the flag.
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(Sourcetrail_lib PRIVATE -fno-modules-reduced-bmi)
	endif()
	# Module scanning of the regular sources is off by the project-wide default (stdexec breaks
	# clang-scan-deps -- see the top-level CMakeLists); the .cppm interface units are still scanned.
	if(SOURCETRAIL_CXX_IMPORT_STD)
		# srctrl.utility's wrapper `import std;`s under this option, so this target consumes the
		# toolchain std module.
		set_target_properties(Sourcetrail_lib PROPERTIES CXX_MODULE_STD ON)
	endif()
	# Opt-in: compile sqlpp23's core+sqlite3 module interface units into the lib and have the SQLite storage
	# impl `import sqlpp23.core/sqlite3` for the query DSL instead of #including it (SRCTRL_SQLPP23_MODULE
	# flips the #ifdef in the schema headers + impl TUs). The impl TUs become module importers, so they must
	# be scanned (the project-wide default is OFF -- see the top-level CMakeLists). BorrowedSqliteConnection.h
	# stays a textual #include (sqlpp23 internals the module doesn't export -- see
	# DESIGN_STORAGE_MODULARIZATION.md §3).
	if(SOURCETRAIL_SQLPP23_MODULES)
		sourcetrail_add_sqlpp23_modules(Sourcetrail_lib)
		target_compile_definitions(Sourcetrail_lib PRIVATE SRCTRL_SQLPP23_MODULE)
		set_source_files_properties(
			"${CMAKE_CURRENT_SOURCE_DIR}/data/storage/sqlite/SqliteStorage.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/data/storage/sqlite/SqliteIndexStorage.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/data/storage/sqlite/SqliteBookmarkStorage.cpp"
			PROPERTIES CXX_SCAN_FOR_MODULES ON)
	endif()
endif()

# CXX settings component sources — always compiled so the settings types are
# unconditionally available (matching the Rust/Swift stabilization approach).
file(GLOB_RECURSE _cxx_settings_sources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/settings/source_group/component/cxx/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/settings/source_group/component/cxx/*.h"
)
target_sources(Sourcetrail_lib PRIVATE ${_cxx_settings_sources})
target_include_directories(Sourcetrail_lib
	PUBLIC
		settings/source_group/component/cxx
		settings/source_group/type
)

# Phase 2 prototype: in-process element-id allocation. Scoped to the one file
# whose behaviour it changes (no ABI impact), so toggling it is a 1-TU rebuild.
if (SOURCETRAIL_CLIENT_IDS)
	set_source_files_properties(
		"${CMAKE_CURRENT_SOURCE_DIR}/data/storage/sqlite/SqliteIndexStorage.cpp"
		PROPERTIES COMPILE_DEFINITIONS SOURCETRAIL_CLIENT_IDS)
endif()

# SOURCETRAIL_TURSO_CONCURRENT is a global define (set in the top-level
# CMakeLists) because the concurrent-writer hooks live in always-compiled TUs.
# The turso targets are linked via SOURCETRAIL_USE_TURSO.

# FlatBuffer schema compilation ------------------------------------------------

set(FLATBUFFERS_SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/indexer/interprocess/schemas)
set(FLATBUFFERS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/flatbuffers)
file(MAKE_DIRECTORY ${FLATBUFFERS_GENERATED_DIR})

set(FLATBUFFERS_SCHEMAS
	${FLATBUFFERS_SCHEMA_DIR}/indexer_command.fbs
	${FLATBUFFERS_SCHEMA_DIR}/intermediate_storage.fbs
	${FLATBUFFERS_SCHEMA_DIR}/indexing_status.fbs
	${FLATBUFFERS_SCHEMA_DIR}/garbage_collector.fbs
)

set(FLATBUFFERS_GENERATED_HEADERS)
foreach(SCHEMA_FILE ${FLATBUFFERS_SCHEMAS})
	get_filename_component(SCHEMA_NAME ${SCHEMA_FILE} NAME_WE)
	set(GENERATED_HEADER ${FLATBUFFERS_GENERATED_DIR}/${SCHEMA_NAME}_generated.h)
	list(APPEND FLATBUFFERS_GENERATED_HEADERS ${GENERATED_HEADER})
	add_custom_command(
		OUTPUT ${GENERATED_HEADER}
		COMMAND flatbuffers::flatc --cpp -o ${FLATBUFFERS_GENERATED_DIR} ${SCHEMA_FILE}
		DEPENDS ${SCHEMA_FILE}
		COMMENT "Compiling FlatBuffer schema ${SCHEMA_NAME}.fbs"
	)
endforeach()

# Agent-UI control contracts (see component/controller/agent_control/schemas and
# context/DESIGN_AGENT_UI_CONTROL.md). Only generated + compiled when the opt-in
# flag is set; the AgentControlController.cpp stub keeps the class linkable otherwise.
# The SOURCETRAIL_AGENT_CONTROL option is declared in the top-level CMakeLists.txt
# (it also decides whether libipc is built with its async-receive layer).
if (SOURCETRAIL_AGENT_CONTROL)
	# App-side build identity stamped into AppInfo.build_id so the MCP bridge can
	# detect an app<->bridge thoth-ipc drift (mirrors the bridge's build.rs stamp:
	# git:<hash>+thoth-ipc:<version>). Scoped as a 1-TU define on the controller,
	# like SqliteIndexStorage.cpp above, so re-stamping is a single recompile.
	execute_process(
		COMMAND git -C ${CMAKE_SOURCE_DIR} rev-parse --short HEAD
		OUTPUT_VARIABLE _agent_git OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if (NOT _agent_git)
		set(_agent_git "unknown")
	endif()
	file(STRINGS "${CMAKE_SOURCE_DIR}/submodules/thoth-ipc/rust/thoth-ipc/Cargo.toml"
		_agent_thoth_line REGEX "^version")
	string(REGEX MATCH "\"([^\"]+)\"" _ "${_agent_thoth_line}")
	set(_agent_thoth "${CMAKE_MATCH_1}")
	if (NOT _agent_thoth)
		set(_agent_thoth "unknown")
	endif()
	set(AGENT_APP_BUILD_ID "git:${_agent_git}+thoth-ipc:${_agent_thoth}")
	message(STATUS "Agent app build id: ${AGENT_APP_BUILD_ID}")
	set_source_files_properties(
		"${CMAKE_CURRENT_SOURCE_DIR}/component/controller/agent_control/AgentControlController.cpp"
		PROPERTIES COMPILE_DEFINITIONS "AGENT_APP_BUILD_ID=\"${AGENT_APP_BUILD_ID}\"")

	set(AGENT_SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/component/controller/agent_control/schemas)
	set(AGENT_SCHEMAS
		${AGENT_SCHEMA_DIR}/agent_common.fbs
		${AGENT_SCHEMA_DIR}/agent_command.fbs
		${AGENT_SCHEMA_DIR}/agent_event.fbs
		${AGENT_SCHEMA_DIR}/agent_state.fbs
		${AGENT_SCHEMA_DIR}/agent_frame.fbs
		${AGENT_SCHEMA_DIR}/agent_snapshot.fbs
	)
	foreach(SCHEMA_FILE ${AGENT_SCHEMAS})
		get_filename_component(SCHEMA_NAME ${SCHEMA_FILE} NAME_WE)
		set(GENERATED_HEADER ${FLATBUFFERS_GENERATED_DIR}/${SCHEMA_NAME}_generated.h)
		list(APPEND FLATBUFFERS_GENERATED_HEADERS ${GENERATED_HEADER})
		add_custom_command(
			OUTPUT ${GENERATED_HEADER}
			COMMAND flatbuffers::flatc --cpp -I ${AGENT_SCHEMA_DIR} -o ${FLATBUFFERS_GENERATED_DIR} ${SCHEMA_FILE}
			DEPENDS ${SCHEMA_FILE}
			COMMENT "Compiling agent FlatBuffer schema ${SCHEMA_NAME}.fbs"
		)
	endforeach()
	target_compile_definitions(Sourcetrail_lib PUBLIC SOURCETRAIL_AGENT_CONTROL)
endif()

add_custom_target(flatbuffers_schemas DEPENDS ${FLATBUFFERS_GENERATED_HEADERS})
add_dependencies(Sourcetrail_lib flatbuffers_schemas)
# lib_gui (QtUiSnapshot) also includes the generated agent headers, so it must wait
# for schema generation too (src/lib_gui is add_subdirectory'd before src/lib).
if (TARGET Sourcetrail_lib_gui)
	add_dependencies(Sourcetrail_lib_gui flatbuffers_schemas)
endif()

target_include_directories(Sourcetrail_lib PUBLIC ${FLATBUFFERS_GENERATED_DIR})

# ------------------------------------------------------------------------------

target_link_libraries(Sourcetrail_lib
	PUBLIC
		AidKit_lib

		# `lib` no longer links `lib_cxx`: the language-specific IndexerCommand (de)serialization moved behind
		# the type-erased IndexerCommandCodecRegistry, so the concrete Cxx command is injected at runtime by
		# lib_cxx (see registerCxxIndexerCommandCodec). Executables that use the Cxx package link it directly.
		# `lib_gui` is still linked here (a few lib TUs reach for utilityApp.h / QtResources.h that really
		# belong in a shared base layer); breaking that edge -- the last inter-target cycle -- is what a future
		# lib_base split unblocks, and with it the C++20-modules target graph.
		Sourcetrail_lib_gui

		External_lib_tomlplusplus
		External_lib_nlohmann_json
		External_lib_cppsqlite3
			$<$<BOOL:${SOURCETRAIL_USE_TURSO}>:External_lib_cppturso3>
			$<$<BOOL:${SOURCETRAIL_USE_LADYBUG}>:External_lib_cppladybug>
		External_lib_glaze
		External_lib_flatbuffers
		External_lib_ipc
		External_lib_json_query

		# Concurrency: senders/receivers + std::expected error channel.
		STDEXEC::stdexec
		stdext::expected
		stdcompat::optional
	PRIVATE
		# Type-safe SQL DSL for the storage-layer migration. PRIVATE: only this
		# lib's own sources include the sqlpp23 headers; SqliteStorage.h keeps the
		# connection forward-declared. See context/DESIGN_SQLPP23_MIGRATION.md.
		sqlpp23::sqlite3
)

if (UNIX)
	target_link_libraries(Sourcetrail_lib
		PUBLIC
			${CMAKE_DL_LIBS}
			Threads::Threads
	)
endif()
