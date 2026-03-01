file(GLOB_RECURSE Sourcetrail_lib_cxx_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(Sourcetrail_lib_cxx ${Sourcetrail_lib_cxx_SOURCES})
setDefaultTargetOptions(Sourcetrail_lib_cxx)

# Derive include directories automatically from the source tree.
set(_cxx_include_dirs "")
foreach(_f ${Sourcetrail_lib_cxx_SOURCES})
	get_filename_component(_d "${_f}" DIRECTORY)
	list(APPEND _cxx_include_dirs "${_d}")
endforeach()
list(REMOVE_DUPLICATES _cxx_include_dirs)

target_include_directories(Sourcetrail_lib_cxx
	PUBLIC
		${_cxx_include_dirs}
		.
)

target_link_libraries(Sourcetrail_lib_cxx
	PUBLIC
		Sourcetrail_lib
		External_lib_clang
)
