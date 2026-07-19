add_executable(Sourcetrail
	main.cpp
	# App-startup settings prefill: detects C++ header/framework paths (via lib_gui's path detectors) on
	# first run. Lives here, not in lib, so lib stays free of lib_gui -- it is only ever called from main().
	ApplicationSettingsPrefiller.cpp
	ApplicationSettingsPrefiller.h
	Sourcetrail.rc
)
setDefaultTargetOptions(Sourcetrail)

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
