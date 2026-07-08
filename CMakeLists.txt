cmake_minimum_required(VERSION 3.27)

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
option(BUILD_UNIT_TESTS_PACKAGE    "Build the corresponding language unit tests." OFF)

configure_file(cmake/language_packages.h.in src/lib/language_packages.h)


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
			# Under Windows the symlinks are *not* copied correctly (results in invalid files), so
			# symlink the data.
			# Note: Temporary test files will show up in version control!
			message(STATUS "Linking: ${testDataSourceDir} -> ${testDataTargetDir}")
			file(MAKE_DIRECTORY "${testDataTargetParentDir}")
			if (NOT EXISTS "${testDataTargetDir}")
				file(CREATE_LINK "${testDataSourceDir}" "${testDataTargetDir}" SYMBOLIC RESULT createLinkResult)
				if (NOT createLinkResult EQUAL 0)
					# Symbolic links require Developer Mode or an elevated process, so
					# fall back to a directory junction which needs no privileges:
					string(REGEX REPLACE "/+$" "" testDataSourceDirNoSlash "${testDataSourceDir}")
					file(TO_NATIVE_PATH "${testDataSourceDirNoSlash}" nativeTestDataSourceDir)
					file(TO_NATIVE_PATH "${testDataTargetDir}" nativeTestDataTargetDir)
					execute_process(
						COMMAND cmd /c mklink /J "${nativeTestDataTargetDir}" "${nativeTestDataSourceDir}"
						RESULT_VARIABLE createJunctionResult
						OUTPUT_QUIET
					)
					if (NOT createJunctionResult EQUAL 0)
						message(FATAL_ERROR "Could not link the test data: ${createLinkResult}")
					endif()
				endif()
			endif()
		endif()
	endif()
endif()

# CLI11 -------------------------------------------------------------------------

find_package(CLI11 CONFIG REQUIRED)
message(STATUS "Found CLI11 ${CLI11_VERSION}")

add_library(External_lib_cli11 INTERFACE)
target_link_libraries(External_lib_cli11 INTERFACE CLI11::CLI11)

# FlatBuffers ------------------------------------------------------------------

find_package(flatbuffers CONFIG REQUIRED)
message(STATUS "Found FlatBuffers ${flatbuffers_VERSION}")

add_library(External_lib_flatbuffers INTERFACE)
target_link_libraries(External_lib_flatbuffers INTERFACE flatbuffers::flatbuffers)

# thoth-ipc ----------------------------------------------------------------------

include(FetchContent)
FetchContent_Declare(
	thoth-ipc
	GIT_REPOSITORY  https://github.com/natyamatsya/thoth-ipc.git
	GIT_TAG         master
	GIT_SUBMODULES  ""
	SOURCE_SUBDIR   cpp/libipc
)
set(LIBIPC_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(LIBIPC_BUILD_DEMOS      OFF CACHE BOOL "" FORCE)
set(LIBIPC_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LIBIPC_BUILD_PROTO      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(thoth-ipc)
message(STATUS "Fetched thoth-ipc (libipc)")

add_library(External_lib_ipc INTERFACE)
target_link_libraries(External_lib_ipc INTERFACE ipc)
target_include_directories(External_lib_ipc SYSTEM INTERFACE
	"${thoth-ipc_SOURCE_DIR}/cpp/libipc/include"
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

# TinyXML ----------------------------------------------------------------------

if (isVcpkgBuild)
	find_package(tinyxml CONFIG REQUIRED)
	if (NOT DEFINED tinyxml_VERSION)
		# Use quotes to indicate a version literal:
		set(tinyxml_VERSION "\"2.6.2\"")
	endif()
	add_library(External_lib_tinyxml ALIAS unofficial-tinyxml::unofficial-tinyxml)
else()
	if (UNIX)
		find_package(PkgConfig REQUIRED)
	endif()
	pkg_check_modules(tinyxml tinyxml IMPORTED_TARGET REQUIRED)
	add_library(External_lib_tinyxml ALIAS PkgConfig::tinyxml)
endif()
message(STATUS "Found tinyxml ${tinyxml_VERSION}")

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

# stdexec (senders/receivers) ---------------------------------------------------------

find_package(stdexec CONFIG REQUIRED)
message(STATUS "Found stdexec (provides STDEXEC::stdexec)")

add_library(External_lib_nlohmann_json INTERFACE)
target_link_libraries(External_lib_nlohmann_json INTERFACE nlohmann_json::nlohmann_json)
target_compile_features(External_lib_nlohmann_json INTERFACE cxx_std_17)

# AidKit  -----------------------------------------------------------------------------------

add_subdirectory(src/lib_aidkit)

# External Lib CppSQlite3  ------------------------------------------------------------------

add_subdirectory(src/external)
# message(STATUS "Using CppSQLite3 ${CppSQLite3_VERSION}")

# Lib Gui ----------------------------------------------------------------------

configure_file(cmake/productVersion.h.in src/lib_gui/productVersion.h)

add_subdirectory(src/resources)
add_subdirectory(src/lib_gui)

# Lib --------------------------------------------------------------------------

if (UNIX)
	find_package(Threads REQUIRED)
endif()

add_subdirectory(src/lib)

#
# Resolve cyclic dependencies between Sourcetrail_lib and Sourcetrail_lib_gui:
#

set_target_properties(Sourcetrail_lib
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
	
	find_package(Clang CONFIG REQUIRED) # Minimum version and/or version range xx...<yy doesn't work.
	message(STATUS "Found LLVM ${LLVM_VERSION}: ${LLVM_INCLUDE_DIRS}")
	checkVersionRange("Clang" ${LLVM_VERSION} ${CLANG_MIN_VERSION} ${CLANG_MAX_VERSION})

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
	FetchContent_Declare(
		corrosion
		GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
		GIT_TAG        v0.5
		GIT_SUBMODULES ""
	)
	FetchContent_MakeAvailable(corrosion)

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
	set(SWIFT_INDEXER_SCHEMA_DIR "${CMAKE_SOURCE_DIR}/src/lib/data/indexer/interprocess/schemas")
	set(SWIFT_INDEXER_GENERATED_DIR "${SWIFT_INDEXER_BUILD_DIR}/generated")
	set(
		SWIFT_INDEXER_PACKAGE_GENERATED_DIR
		"${SWIFT_INDEXER_PACKAGE_DIR}/Sources/SourcetrailSwiftIndexer/GeneratedFromCMake"
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
			COMMAND flatbuffers::flatc --swift --gen-all -o "${SWIFT_INDEXER_GENERATED_DIR}" "${SCHEMA_FILE}"
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
