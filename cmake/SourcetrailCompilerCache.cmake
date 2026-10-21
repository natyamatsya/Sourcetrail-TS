# Put a compiler cache in front of the compiler, if the machine has one.
#
# This is deliberately not a preset. A launcher named in a preset is a promise
# that a particular binary exists on whoever's machine reads it: CMake does not
# check, so a missing one configures cleanly and then fails on every single
# compile. Detection here fails the only way that is useful -- by saying, once,
# at configure time, that the build will not be cached.
#
# It also means every preset benefits, including a plain `cmake ..`, and that a
# contributor who uses ccache rather than sccion is not asked to care.

# AUTO uses a cache when one is installed. ON requires one, which is what CI
# wants when the point of the job is to measure the cache. OFF is how you get a
# cold build without uninstalling anything.
set(SOURCETRAIL_COMPILER_CACHE "AUTO" CACHE STRING
	"Use a compiler cache: AUTO (if one is installed), ON (require one), OFF")
set_property(CACHE SOURCETRAIL_COMPILER_CACHE PROPERTY STRINGS AUTO ON OFF)

# The order is a preference, not a ranking of quality: sccion is the one this
# project is set up for -- see .sccion.toml, which makes two checkouts share
# cache entries -- and the others are honoured because a contributor who already
# runs one should not have to install anything.
set(SOURCETRAIL_COMPILER_CACHE_CANDIDATES sccion sccache ccache)

# The cache that also takes rustc, as cargo's RUSTC_WRAPPER, when `cache` is
# one that can: sccion and sccache can, ccache cannot. Empty otherwise -- and
# when the environment already names a wrapper, which is then the one cargo
# should keep using.
function(_sourcetrail_rustc_wrapper_for out cache)
	set(wrapper "")
	get_filename_component(name "${cache}" NAME_WE)
	if(name STREQUAL "sccion" OR name STREQUAL "sccache")
		if(DEFINED ENV{RUSTC_WRAPPER} OR DEFINED ENV{CARGO_BUILD_RUSTC_WRAPPER})
			message(STATUS "Compiler cache: cargo keeps the RUSTC_WRAPPER the environment sets")
		else()
			set(wrapper "${cache}")
		endif()
	endif()
	set(${out} "${wrapper}" PARENT_SCOPE)
endfunction()

function(sourcetrail_resolve_compiler_cache)
	if(SOURCETRAIL_COMPILER_CACHE STREQUAL "OFF" OR NOT SOURCETRAIL_COMPILER_CACHE)
		message(STATUS "Compiler cache: disabled (SOURCETRAIL_COMPILER_CACHE=OFF)")
		return()
	endif()

	# Someone who set the launcher by hand -- in a user preset, a toolchain file
	# or on the command line -- has said what they want, and it may well be a
	# wrapper this does not know about. But only if it is still there: a launcher
	# path outlives the checkout it pointed into, and CMake keeps it in the cache
	# of a build directory that then fails on every compile with a message about
	# a program nobody remembers configuring. A leftover is not a preference.
	set(existing "${CMAKE_CXX_COMPILER_LAUNCHER}")
	if(NOT existing)
		set(existing "${CMAKE_C_COMPILER_LAUNCHER}")
	endif()
	if(existing)
		list(GET existing 0 existing_exe) # it may carry arguments of its own
		set(existing_usable TRUE)
		if(IS_ABSOLUTE "${existing_exe}")
			if(NOT EXISTS "${existing_exe}")
				set(existing_usable FALSE)
			endif()
		else()
			find_program(SOURCETRAIL_EXISTING_LAUNCHER "${existing_exe}")
			if(NOT SOURCETRAIL_EXISTING_LAUNCHER)
				set(existing_usable FALSE)
			endif()
			set(existing_exe "${SOURCETRAIL_EXISTING_LAUNCHER}")
			unset(SOURCETRAIL_EXISTING_LAUNCHER CACHE)
		endif()

		if(existing_usable)
			message(STATUS "Compiler cache: using the launcher already set (${existing_exe})")
			_sourcetrail_rustc_wrapper_for(wrapper "${existing_exe}")
			set(SOURCETRAIL_RUSTC_WRAPPER "${wrapper}" PARENT_SCOPE)
			return()
		endif()
		message(WARNING
			"Compiler cache: the configured launcher '${existing_exe}' does not exist. "
			"Every compilation in this build directory would fail with it. Looking for one "
			"that is installed instead; configure with -D SOURCETRAIL_COMPILER_CACHE=OFF for "
			"no launcher at all.")
		# Replace it rather than shadow it, or the stale value stays in
		# CMakeCache.txt to mislead the next person who reads it.
		unset(CMAKE_C_COMPILER_LAUNCHER CACHE)
		unset(CMAKE_CXX_COMPILER_LAUNCHER CACHE)
	endif()

	find_program(SOURCETRAIL_COMPILER_CACHE_EXECUTABLE
		NAMES ${SOURCETRAIL_COMPILER_CACHE_CANDIDATES}
		DOC "Compiler cache to run compilations through")

	if(NOT SOURCETRAIL_COMPILER_CACHE_EXECUTABLE)
		string(JOIN ", " candidates ${SOURCETRAIL_COMPILER_CACHE_CANDIDATES})
		if(SOURCETRAIL_COMPILER_CACHE STREQUAL "AUTO")
			message(STATUS
				"Compiler cache: none found (looked for ${candidates}); compilations will not be cached")
			return()
		endif()
		message(FATAL_ERROR
			"SOURCETRAIL_COMPILER_CACHE=ON but no compiler cache was found on PATH "
			"(looked for ${candidates}). Install one, or configure with "
			"-D SOURCETRAIL_COMPILER_CACHE=AUTO to build uncached.")
	endif()

	set(CMAKE_C_COMPILER_LAUNCHER   "${SOURCETRAIL_COMPILER_CACHE_EXECUTABLE}" PARENT_SCOPE)
	set(CMAKE_CXX_COMPILER_LAUNCHER "${SOURCETRAIL_COMPILER_CACHE_EXECUTABLE}" PARENT_SCOPE)
	message(STATUS "Compiler cache: ${SOURCETRAIL_COMPILER_CACHE_EXECUTABLE}")
	_sourcetrail_rustc_wrapper_for(wrapper "${SOURCETRAIL_COMPILER_CACHE_EXECUTABLE}")
	set(SOURCETRAIL_RUSTC_WRAPPER "${wrapper}" PARENT_SCOPE)

	# MSVC's default /Zi writes debug info into one PDB shared by every
	# translation unit in a target. A compiler cache cannot serve an object whose
	# debug info lives in a file it did not write, so the whole Debug build
	# silently caches nothing -- sccion classifies exactly this as
	# `uncacheable:shared_pdb`, which is the fastest way to recognise it if it
	# ever comes back. /Z7 puts the debug info in the object, where a cache can
	# carry it. Same information, larger objects, no PDB.
	if(MSVC AND NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
		set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded" PARENT_SCOPE)
		message(STATUS
			"Compiler cache: using /Z7 (embedded debug info) so debug builds are cacheable; "
			"set CMAKE_MSVC_DEBUG_INFORMATION_FORMAT to override")
	endif()

	# The measured difference this file makes across two checkouts of this
	# repository was 288 s to 18 s, and it is one committed file. Worth a line
	# when it is missing, and silence when it is not.
	get_filename_component(cache_name "${SOURCETRAIL_COMPILER_CACHE_EXECUTABLE}" NAME_WE)
	if(cache_name STREQUAL "sccion" AND NOT EXISTS "${CMAKE_SOURCE_DIR}/.sccion.toml")
		message(STATUS
			"Compiler cache: no .sccion.toml in this checkout -- `sccion --init` writes one, "
			"and it is what lets two checkouts share cache entries")
	endif()
endfunction()

# Run the cargo builds of these Corrosion targets through the compiler cache as
# well, as RUSTC_WRAPPER, when it is one that takes rustc. Without it every
# checkout compiles its Rust dependencies from scratch: measured in a fresh
# worktree, the Rust, Swift and Zig builds were most of a 205 s build whose C++
# all came from the cache.
#
# What rustc caching covers: library crates (rlib, staticlib), compiled without
# incremental. cargo builds registry dependencies that way in every profile, and
# runs them with the same paths in every checkout, so those share between
# checkouts. A crate of this repository keys on its own checkout's path, and
# binaries, proc-macros and build scripts are linked by rustc and never cached.
function(sourcetrail_cache_rust_crates)
	if(NOT SOURCETRAIL_RUSTC_WRAPPER)
		return()
	endif()
	foreach(target IN LISTS ARGN)
		corrosion_set_env_vars(${target} "RUSTC_WRAPPER=${SOURCETRAIL_RUSTC_WRAPPER}")
	endforeach()
	list(JOIN ARGN ", " targets)
	message(STATUS "Compiler cache: cargo builds of ${targets} go through ${SOURCETRAIL_RUSTC_WRAPPER}")
endfunction()
