file(GLOB_RECURSE Sourcetrail_test_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# CXX-only suites: exclude them from the unconditional glob and add them back
# only when BUILD_CXX_LANGUAGE_PACKAGE is enabled.
list(FILTER Sourcetrail_test_SOURCES EXCLUDE REGEX ".*/CMakeFileAPIReaderTestSuite\\.cpp")
list(FILTER Sourcetrail_test_SOURCES EXCLUDE REGEX ".*/ClangCompatTestSuite\\.cpp")
list(FILTER Sourcetrail_test_SOURCES EXCLUDE REGEX ".*/CxxParserTestSuite\\.cpp")
list(FILTER Sourcetrail_test_SOURCES EXCLUDE REGEX ".*/CxxIncludeProcessingTestSuite\\.cpp")
list(FILTER Sourcetrail_test_SOURCES EXCLUDE REGEX ".*/CxxTypeNameTestSuite\\.cpp")
list(FILTER Sourcetrail_test_SOURCES EXCLUDE REGEX ".*/SourceGroupCxxTestSuite\\.cpp")
list(FILTER Sourcetrail_test_SOURCES EXCLUDE REGEX ".*/ToolChainTestSuite\\.cpp")

add_executable(Sourcetrail_test ${Sourcetrail_test_SOURCES})

# Same reason as Sourcetrail_lib_cxx: the CxxParser suite includes the frontend glue headers,
# whose inline bodies mix Qt-bearing and Sema-reaching includes; tests use no Qt keywords.
target_compile_definitions(Sourcetrail_test PRIVATE QT_NO_EMIT)

if (BUILD_CXX_LANGUAGE_PACKAGE)
	target_sources(Sourcetrail_test
		PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/CMakeFileAPIReaderTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/ClangCompatTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/CxxParserTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/CxxIncludeProcessingTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/CxxTypeNameTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/SourceGroupCxxTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/ToolChainTestSuite.cpp"
	)
endif()

setDefaultTargetOptions(Sourcetrail_test)

# SOURCETRAIL_TURSO_CONCURRENT is now a global define; no per-file scoping needed.

target_include_directories(Sourcetrail_test
	PRIVATE
		helper
)

set_target_properties(Sourcetrail_test
	PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test/"
		PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/test/"
)

target_link_libraries(Sourcetrail_test
	PRIVATE
		Sourcetrail_lib_core
		# lib no longer re-exports lib_gui; the Qt test suites (QtActions/QtResources/...) need it directly.
		Sourcetrail_lib_gui
		Sourcetrail_res_gui
		Catch2::Catch2
)

if (BUILD_RUST_LANGUAGE_PACKAGE)
	add_dependencies(Sourcetrail_test copy_rust_indexer)
endif()

if (BUILD_SWIFT_LANGUAGE_PACKAGE)
	add_dependencies(Sourcetrail_test copy_swift_indexer)
endif()

if (BUILD_CXX_LANGUAGE_PACKAGE)
	target_link_libraries(Sourcetrail_test
		PRIVATE
			Sourcetrail_lib_cxx
	)
endif()

if (WIN32)
	set_target_properties(Sourcetrail_test
		PROPERTIES
			VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/test"
	)
endif ()

if (UNIX)
	set(CATCH2_LOCALE_WRAPPER "${CMAKE_SOURCE_DIR}/cmake/run_with_c_locale.sh")
	file(CHMOD "${CATCH2_LOCALE_WRAPPER}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
	set_target_properties(Sourcetrail_test PROPERTIES
		CROSSCOMPILING_EMULATOR "${CATCH2_LOCALE_WRAPPER}"
	)
endif()

# Module build: converted test suites `import srctrl.*;` instead of #including the modularized
# headers (the includes sit under `#ifndef SRCTRL_MODULE_BUILD`). A suite is convertible when its
# remaining textual include closure contains no modularized header and no unguarded forward
# declaration of a module-attached type.
if(SOURCETRAIL_CXX_MODULES_ENABLED)
	# SRCTRL_MODULE_BUILD is PER-SOURCE, not target-wide: it means "this TU imports the module
	# graph". Headers guard modularized includes/fwd decls on it, so non-importing TUs in the same
	# target (including moc-generated ones, which can never import) must NOT define it -- they keep
	# the classic textual view. Module scanning is off project-wide (stdexec breaks
	# clang-scan-deps); scan only the files that import.
	set(_srctrl_importing_tus
		ConfigManagerTestSuite.cpp
		FilePathFilterTestSuite.cpp
		FileManagerTestSuite.cpp
		FilePathTestSuite.cpp
		FileSystemTestSuite.cpp
		MatrixDynamicBaseTestSuite.cpp
		GraphTestSuite.cpp
		HierarchyCacheTestSuite.cpp
		IpcSharedMemoryTestSuite.cpp
		LogManagerTestSuite.cpp
		LowMemoryStringMapTestSuite.cpp
		QtResourcesTestSuite.cpp
		SettingsTestSuite.cpp
		SourceLocationCollectionTestSuite.cpp
		TextAccessTestSuite.cpp
		UtilityEnumTestSuite.cpp
		UtilityStringTestSuite.cpp
		Vector2TestSuite.cpp
	)
	set_source_files_properties(${_srctrl_importing_tus} PROPERTIES CXX_SCAN_FOR_MODULES ON)
	set_property(SOURCE ${_srctrl_importing_tus} APPEND PROPERTY
		COMPILE_DEFINITIONS SRCTRL_MODULE_BUILD)
endif()

catch_discover_tests(Sourcetrail_test)
