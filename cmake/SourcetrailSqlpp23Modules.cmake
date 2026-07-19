# Compile sqlpp23's C++20 module interface units (core + sqlite3) into a target's module file set, so
# first-party module code can `import sqlpp23.core; import sqlpp23.sqlite3;` instead of #including
# <sqlpp23/...>. Upstream ships these as *sources* to be compiled by the consumer ("No compiled version
# of the modules will be installed" -- sqlpp23 docs/modules.md); the overlay port installs them to
# <prefix>/modules/sqlpp23 (see vcpkg-overlay-ports/sqlpp23/portfile.cmake).
#
# Spike-validated on clang-22 (context/DESIGN_STORAGE_MODULARIZATION.md §3): the modules compile; a
# ddl2cpp-generated schema + a real select().from() query build with `import sqlpp23.core` plus a GMF
# `#include <sqlpp23/core/name/create_name_tag.h>` (the tiny macro-only header, since macros don't cross
# import); and import/#include of the same sqlpp header coexist cleanly (unlike `import std`).

# SQLPP23_MODULES_DIR: override the module-sources location (e.g. point at the unpacked upstream source
# before the overlay port has been reinstalled with the modules-shipping patch).
set(SQLPP23_MODULES_DIR "" CACHE PATH "Directory containing sqlpp23.core.cppm / sqlpp23.sqlite3.cppm")

function(sourcetrail_add_sqlpp23_modules target)
	if(NOT TARGET sqlpp23::sqlite3)
		message(FATAL_ERROR
			"sourcetrail_add_sqlpp23_modules: sqlpp23::sqlite3 not found -- call find_package(Sqlpp23) first")
	endif()

	set(_mods "${SQLPP23_MODULES_DIR}")
	if(NOT _mods)
		# Derive <prefix>/modules/sqlpp23 from the sqlpp23 include dir (<prefix>/include).
		get_target_property(_inc sqlpp23::sqlite3 INTERFACE_INCLUDE_DIRECTORIES)
		list(GET _inc 0 _inc0)
		cmake_path(GET _inc0 PARENT_PATH _prefix)
		set(_mods "${_prefix}/modules/sqlpp23")
	endif()

	set(_core "${_mods}/sqlpp23.core.cppm")
	set(_sqlite "${_mods}/sqlpp23.sqlite3.cppm")
	if(NOT EXISTS "${_core}" OR NOT EXISTS "${_sqlite}")
		message(FATAL_ERROR
			"sourcetrail_add_sqlpp23_modules: sqlpp23 module sources not found under '${_mods}'. "
			"Reinstall the sqlpp23 overlay port (it now ships modules/), or set SQLPP23_MODULES_DIR.")
	endif()

	# The module GMFs #include <sqlpp23/...> and <sqlite3.h>; sqlpp23::sqlite3 carries both include dirs.
	target_sources(${target}
		PRIVATE
			FILE_SET sqlpp23_modules TYPE CXX_MODULES BASE_DIRS "${_mods}"
			FILES "${_core}" "${_sqlite}")
	target_link_libraries(${target} PRIVATE sqlpp23::sqlite3)
	message(STATUS "sqlpp23 C++20 modules wired into ${target} from ${_mods}")
endfunction()
