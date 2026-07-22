# Resolve whether the flag-gated C++20-modules build can actually run on this toolchain, and
# auto-fall back to the classic header build (with a warning) when it can't -- so
# `-D SOURCETRAIL_CXX_MODULES=ON` can never hard-fail a configure on a compiler/generator that
# doesn't support modules. See context/DESIGN_INDEXER_MODULARIZATION.md.
#
#   sourcetrail_resolve_cxx_modules(<out-var>)
#
# sets <out-var> to ON only when the user asked for modules AND CMake + the generator + the compiler
# can build a `FILE_SET CXX_MODULES` target.
function(sourcetrail_resolve_cxx_modules out_var)
	if(NOT SOURCETRAIL_CXX_MODULES)
		set(${out_var} OFF PARENT_SCOPE)
		return()
	endif()

	set(_reason "")
	if(CMAKE_VERSION VERSION_LESS 3.28)
		set(_reason "CMake ${CMAKE_VERSION} < 3.28 (needs FILE_SET CXX_MODULES support)")
	elseif(NOT CMAKE_GENERATOR MATCHES "Ninja|Visual Studio")
		set(_reason "generator '${CMAKE_GENERATOR}' has no C++ module scanning (needs Ninja or Visual Studio)")
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
		set(_reason "Clang ${CMAKE_CXX_COMPILER_VERSION} < 16")
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
		set(_reason "GCC ${CMAKE_CXX_COMPILER_VERSION} < 14")
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.36)
		# Floor only, and doubly optional: this branch is reached solely for a modules build
		# (SOURCETRAIL_CXX_MODULES=ON, guarded at the top of the function) and never hard-fails -- an
		# older toolset just warns and falls back to the classic build below, so a classic build on any
		# MSVC is unaffected. Named-module support is solid from 19.36 (VS 17.6). The Windows bring-up
		# pins the *newest* installed toolset via the windows-msvc-* CMake presets /
		# scripts/win/Init-ModulesEnv.ps1 (14.51.36231 on the reference machine, CMake version 19.51)
		# rather than raising this floor -- keeping a 19.44 box able to build classically.
		# See docs/technical_notes/cxx20-modules-migration/msvc-modules-handoff.typ (step W0).
		set(_reason "MSVC ${CMAKE_CXX_COMPILER_VERSION} < 19.36")
	endif()

	if(_reason)
		message(WARNING
			"SOURCETRAIL_CXX_MODULES=ON, but ${_reason}; falling back to the classic header build.")
		set(${out_var} OFF PARENT_SCOPE)
	else()
		set(${out_var} ON PARENT_SCOPE)
	endif()
endfunction()

# --- MSVC module frontier (the below-storage cut) --------------------------------------------------
# On MSVC the C++20-modules build stops below srctrl.storage: MSVC cannot round-trip a Qt QMetaType
# specialization through an IFC (a compiler defect, self-flagged via report-cpp-modules-problem -- see
# docs/technical_notes/cxx20-modules-migration/msvc-bringup-findings.md). srctrl.storage and the
# modules that import it (messaging, indexer, interprocess) therefore stay CLASSIC on Windows; the dual
# build supports a partial module frontier (classic and module TUs share ordinary mangling under the
# global-module attachment model). Everywhere else these build as modules, unchanged.
#
# These two filters are no-ops off MSVC, so callers wrap them around their normal lists unconditionally.
set(SOURCETRAIL_MSVC_CLASSIC_MODULE_RE  "^srctrl_(storage|messaging|indexer|interprocess)")
set(SOURCETRAIL_MSVC_CLASSIC_IMPORT_RE  "import[ \t]+srctrl\\.(storage|messaging|indexer|interprocess)")

# Drop the wrapper .cppm of the classic-on-MSVC modules from <list_var> (by basename).
function(sourcetrail_msvc_filter_module_units list_var)
	if(NOT MSVC)
		return()
	endif()
	set(_kept "")
	foreach(_cppm IN LISTS ${list_var})
		get_filename_component(_base "${_cppm}" NAME)
		if(_base MATCHES "${SOURCETRAIL_MSVC_CLASSIC_MODULE_RE}")
			continue()
		endif()
		list(APPEND _kept "${_cppm}")
	endforeach()
	set(${list_var} "${_kept}" PARENT_SCOPE)
endfunction()

# Drop importer TUs that `import` any classic-on-MSVC module from <list_var> -- those compile fully
# classic instead. Entries may be absolute or relative to CMAKE_CURRENT_SOURCE_DIR.
function(sourcetrail_msvc_filter_importers list_var)
	if(NOT MSVC)
		return()
	endif()
	set(_kept "")
	foreach(_tu IN LISTS ${list_var})
		set(_path "${_tu}")
		if(NOT IS_ABSOLUTE "${_path}")
			set(_path "${CMAKE_CURRENT_SOURCE_DIR}/${_tu}")
		endif()
		set(_drop FALSE)
		if(EXISTS "${_path}")
			file(STRINGS "${_path}" _hits REGEX "${SOURCETRAIL_MSVC_CLASSIC_IMPORT_RE}")
			if(_hits)
				set(_drop TRUE)
			endif()
		endif()
		if(_drop)
			continue()
		endif()
		list(APPEND _kept "${_tu}")
	endforeach()
	set(${list_var} "${_kept}" PARENT_SCOPE)
endfunction()
