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
				data/parser/cxx/srctrl_cxx-parser.cppm
				data/parser/cxx/srctrl_cxx-visitor.cppm
				data/parser/cxx/srctrl_cxx-frontend.cppm
				srctrl_cxx-package.cppm)
	# Same LLVM #166068 reduced-BMI workaround as lib_core (see its CMakeLists for the full story).
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(Sourcetrail_lib_cxx PRIVATE -fno-modules-reduced-bmi)
		# NOTE: -Wdecls-in-multiple-modules was briefly enabled here to surface GM/named-module
		# dual attachments (the srctrl.messaging park). Post-pivot (SRCTRL_EXPORT =
		# export extern "C++") ALL first-party entities are global-module by design, so the
		# warning only reports our intended architecture (~150 hits/build) -- removed.
	endif()
	if(SOURCETRAIL_CXX_IMPORT_STD)
		set_target_properties(Sourcetrail_lib_cxx PROPERTIES CXX_MODULE_STD ON)
	endif()

	# Converted consumer TUs (batch 10): SRCTRL_MODULE_BUILD is PER-SOURCE ("this TU imports");
	# non-importing TUs keep the classic textual view. Scan only the importing files.
	set(_srctrl_importing_tus
		"${CMAKE_CURRENT_SOURCE_DIR}/data/indexer/CxxIndexerCommandProvider.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/project/CxxModulePrebuilder.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/project/SourceGroupCxxCMakeFileAPI.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/project/SourceGroupCxxCdb.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/project/SourceGroupCxxEmpty.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/project/SourceGroupFactoryModuleCxx.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/project/utilitySourceGroupCxx.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/IncludeProcessing.cpp"
	)
	set_source_files_properties(${_srctrl_importing_tus} PROPERTIES CXX_SCAN_FOR_MODULES ON)
	set_property(SOURCE ${_srctrl_importing_tus} APPEND PROPERTY
		COMPILE_DEFINITIONS SRCTRL_MODULE_BUILD)
endif()

# The C++20 standard library module (std.cppm) ships next to the toolchain's libc++ at
# <prefix>/share/libc++/v1/. The module prebuild builds its BMI on demand so `import std;` resolves.
# Bake in the LLVM install prefix we link against, so the runner compiles the std.cppm that matches
# the in-process libclang's libc++ exactly.
# Qt's `emit` macro breaks clang's Sema.h (which has an emit() member) -- with the frontend
# glue headers now self-contained (inline bodies pull Qt-bearing settings/paths headers AND
# Sema-reaching clang headers into any including TU), the per-TU push_macro sandwiches can't
# cover it anymore. lib_cxx is non-GUI and uses no Qt keywords, so just don't define emit.
target_compile_definitions(Sourcetrail_lib_cxx PRIVATE QT_NO_EMIT)

target_compile_definitions(Sourcetrail_lib_cxx
	PRIVATE
		SOURCETRAIL_LLVM_INSTALL_PREFIX="${LLVM_INSTALL_PREFIX}"
)
