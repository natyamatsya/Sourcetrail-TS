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
			unset(SOURCETRAIL_EXISTING_LAUNCHER CACHE)
		endif()

		if(existing_usable)
			message(STATUS "Compiler cache: using the launcher already set (${existing_exe})")
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
