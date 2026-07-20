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
		Sourcetrail_lib_core
		External_lib_clang
)

# Phase 3 of the C++20-modules migration (context/DESIGN_INDEXER_MODULARIZATION.md): the srctrl.cxx
# module. Same dual-build mechanics as Sourcetrail_lib_core -- the .cppm are not GLOB'd, so they're
# built only when modules are enabled; the .cpp files keep #including the headers as ordinary TUs.
if(SOURCETRAIL_CXX_MODULES_ENABLED)
	target_sources(Sourcetrail_lib_cxx
		PUBLIC
			FILE_SET CXX_MODULES FILES
				srctrl_cxx.cppm
				data/parser/cxx/name/srctrl_cxx-name.cppm
				data/parser/cxx/srctrl_cxx-context.cppm
				data/parser/cxx/srctrl_cxx-tooling.cppm
				data/parser/cxx/srctrl_cxx-parser.cppm)
	# Same LLVM #166068 reduced-BMI workaround as lib_core (see its CMakeLists for the full story).
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(Sourcetrail_lib_cxx PRIVATE -fno-modules-reduced-bmi)
	endif()
	if(SOURCETRAIL_CXX_IMPORT_STD)
		set_target_properties(Sourcetrail_lib_cxx PROPERTIES CXX_MODULE_STD ON)
	endif()
endif()

# The C++20 standard library module (std.cppm) ships next to the toolchain's libc++ at
# <prefix>/share/libc++/v1/. The module prebuild builds its BMI on demand so `import std;` resolves.
# Bake in the LLVM install prefix we link against, so the runner compiles the std.cppm that matches
# the in-process libclang's libc++ exactly.
target_compile_definitions(Sourcetrail_lib_cxx
	PRIVATE
		SOURCETRAIL_LLVM_INSTALL_PREFIX="${LLVM_INSTALL_PREFIX}"
)
