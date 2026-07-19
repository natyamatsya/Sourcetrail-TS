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
