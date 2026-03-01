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

add_executable(Sourcetrail_test ${Sourcetrail_test_SOURCES})

if (BUILD_CXX_LANGUAGE_PACKAGE)
	target_sources(Sourcetrail_test
		PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/CMakeFileAPIReaderTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/ClangCompatTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/CxxParserTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/CxxIncludeProcessingTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/CxxTypeNameTestSuite.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/SourceGroupCxxTestSuite.cpp"
	)
endif()

setDefaultTargetOptions(Sourcetrail_test)

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
		Sourcetrail_lib
		Sourcetrail_res_gui
		Catch2::Catch2
)

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
catch_discover_tests(Sourcetrail_test)
