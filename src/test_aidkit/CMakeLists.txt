add_executable(AidKit_test
	enum_class_test.cpp
	thread_shared_test.cpp
	
	qt/StringsTest.cpp
)

setDefaultTargetOptions(AidKit_test)

set_target_properties(AidKit_test
	PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test/"
		PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/test/"
)

target_link_libraries(AidKit_test
	PRIVATE
		AidKit_lib
		Catch2::Catch2WithMain
)

# In a module build, exercise the `import aidkit;` path: SRCTRL_MODULE_BUILD switches the enum_class
# and thread_shared tests from #include to import (StringsTest keeps #including, so the same run also
# covers a header consumer of the inlined Strings against the module).
# On MSVC the test drivers stay classic (they still link the module-built AidKit_lib): mixing Catch2's
# textual std with `import std` (via `import aidkit`) in one TU double-defines std on MSVC.
if(SOURCETRAIL_CXX_MODULES_ENABLED AND NOT MSVC)
	# Module scanning is off project-wide (stdexec); scan the files that import aidkit.
	# SRCTRL_MODULE_BUILD is per-source ("this TU imports") -- see the main targets.
	set_source_files_properties(enum_class_test.cpp thread_shared_test.cpp
		PROPERTIES CXX_SCAN_FOR_MODULES ON)
	set_property(SOURCE enum_class_test.cpp thread_shared_test.cpp APPEND PROPERTY
		COMPILE_DEFINITIONS SRCTRL_MODULE_BUILD)
endif()

if (WIN32)
	set_target_properties(AidKit_test
		PROPERTIES
			VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/test"
	)
endif ()

catch_discover_tests(AidKit_test)
