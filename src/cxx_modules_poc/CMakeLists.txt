# C++20-modules proof-of-concept -- Phase 0 of context/DESIGN_INDEXER_MODULARIZATION.md.
#
# Added only when SOURCETRAIL_CXX_MODULES_ENABLED (so the header build is completely untouched). It
# proves, on the real toolchain, that the three pieces of the migration work end to end before any
# production code is converted:
#   1. the SrctrlModule.h dual-build scaffolding (SRCTRL_EXPORT / SRCTRL_MODULE_PURVIEW),
#   2. the fmt-style header + module-wrapper split (srctrl_ping.h + srctrl_ping.cppm), and
#   3. the CMake `FILE_SET CXX_MODULES` module-scanning plumbing.
add_executable(srctrl_modules_poc ping_main.cpp)
target_compile_features(srctrl_modules_poc PRIVATE cxx_std_23)
target_include_directories(srctrl_modules_poc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(srctrl_modules_poc PRIVATE SRCTRL_MODULE_BUILD)
# Module scanning is off project-wide (stdexec); this consumer imports, so scan it.
set_source_files_properties(ping_main.cpp PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_sources(srctrl_modules_poc
	PRIVATE
		FILE_SET CXX_MODULES FILES srctrl_ping.cppm)
