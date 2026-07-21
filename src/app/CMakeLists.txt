add_executable(Sourcetrail
	main.cpp
	# App-startup settings prefill: detects C++ header/framework paths (via lib_gui's path detectors) on
	# first run. Lives here, not in lib, so lib stays free of lib_gui -- it is only ever called from main().
	ApplicationSettingsPrefiller.cpp
	ApplicationSettingsPrefiller.h
	Sourcetrail.rc
)
setDefaultTargetOptions(Sourcetrail)

# main.cpp includes the now self-contained lib_cxx package headers (LanguagePackageCxx, the prebuild
# runners), which reach clang's Sema.h -- and Qt's `emit` macro breaks its `emit` member. The app
# target's own sources use no Qt keywords (the GUI lives in lib_gui), so just don't define emit
# (same as Sourcetrail_lib_cxx / Sourcetrail_test).
target_compile_definitions(Sourcetrail PRIVATE QT_NO_EMIT)

set_target_properties(Sourcetrail
	PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app/"
		PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/app/"
)

target_link_libraries(Sourcetrail
	PRIVATE
		Sourcetrail_lib_core
		# lib is the base layer now (links no lib_gui/lib_cxx), so the app names the packages it needs:
		# the GUI (brings res_gui transitively) and the Cxx language package + command codec.
		Sourcetrail_lib_gui
		$<$<BOOL:${BUILD_CXX_LANGUAGE_PACKAGE}>:Sourcetrail_lib_cxx>
)

# Module build: converted consumer TUs (SRCTRL_MODULE_BUILD is PER-SOURCE -- "this TU imports").
if(SOURCETRAIL_CXX_MODULES_ENABLED)
	set_source_files_properties(ApplicationSettingsPrefiller.cpp PROPERTIES CXX_SCAN_FOR_MODULES ON)
	set_property(SOURCE ApplicationSettingsPrefiller.cpp APPEND PROPERTY
		COMPILE_DEFINITIONS SRCTRL_MODULE_BUILD)
endif()
