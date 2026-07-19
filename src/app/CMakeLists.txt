add_executable(Sourcetrail
	main.cpp
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
		Sourcetrail_lib
		# lib no longer re-exports lib_cxx; link it directly for the Cxx package + command codec.
		$<$<BOOL:${BUILD_CXX_LANGUAGE_PACKAGE}>:Sourcetrail_lib_cxx>
)
