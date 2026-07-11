# Sources: collected with GLOB_RECURSE + CONFIGURE_DEPENDS so CMake re-runs
# automatically when files are added or removed.
file(GLOB_RECURSE Sourcetrail_lib_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
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

add_custom_target(flatbuffers_schemas DEPENDS ${FLATBUFFERS_GENERATED_HEADERS})
add_dependencies(Sourcetrail_lib flatbuffers_schemas)

target_include_directories(Sourcetrail_lib PUBLIC ${FLATBUFFERS_GENERATED_DIR})

# ------------------------------------------------------------------------------

target_link_libraries(Sourcetrail_lib
	PUBLIC
		AidKit_lib

		Sourcetrail_lib_gui

		$<$<BOOL:${BUILD_CXX_LANGUAGE_PACKAGE}>:Sourcetrail_lib_cxx>

		External_lib_tinyxml
		External_lib_tomlplusplus
		External_lib_nlohmann_json
		External_lib_cppsqlite3
			$<$<BOOL:${SOURCETRAIL_USE_TURSO}>:External_lib_cppturso3>
			$<$<BOOL:${SOURCETRAIL_USE_LADYBUG}>:External_lib_cppladybug>
		External_lib_cli11
		External_lib_flatbuffers
		External_lib_ipc
		External_lib_json_query

		# Concurrency: senders/receivers + std::expected error channel.
		STDEXEC::stdexec
		stdext::expected
		stdcompat::optional
)

if (UNIX)
	target_link_libraries(Sourcetrail_lib
		PUBLIC
			${CMAKE_DL_LIBS}
			Threads::Threads
	)
endif()
