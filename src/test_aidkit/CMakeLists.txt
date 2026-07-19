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
if(SOURCETRAIL_CXX_MODULES_ENABLED)
	target_compile_definitions(AidKit_test PRIVATE SRCTRL_MODULE_BUILD)
endif()

if (WIN32)
	set_target_properties(AidKit_test
		PROPERTIES
			VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/test"
	)
endif ()

catch_discover_tests(AidKit_test)
