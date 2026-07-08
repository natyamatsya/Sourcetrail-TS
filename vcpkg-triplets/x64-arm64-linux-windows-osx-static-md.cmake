# Define the target triplets:
if (LINUX)
	set(VCPKG_CMAKE_SYSTEM_NAME Linux)
	set(VCPKG_TARGET_ARCHITECTURE x64)
elseif (WIN32)
	# This is the default, but make it obvious that an unset system name means Windows.
	unset(VCPKG_CMAKE_SYSTEM_NAME)
	set(VCPKG_TARGET_ARCHITECTURE x64)
elseif (APPLE)
	set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
	set(VCPKG_TARGET_ARCHITECTURE arm64)
endif()

# Set static linking for the libraries:
set(VCPKG_LIBRARY_LINKAGE static)

# Set dynamic linking for the C runtime library (CRT):
set(VCPKG_CRT_LINKAGE dynamic)

# LLVM is huge; only build the release configuration (a debug LLVM roughly
# doubles a multi-hour build and tens of GB of libraries). Remove this if you
# need to debug into LLVM/Clang itself:
if ("${PORT}" STREQUAL "llvm")
	set(VCPKG_BUILD_TYPE release)
endif()

# Prevent Catch2 from intercepting JVM signals:
# https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md#catch_config_-customization-options-in-cmake
if ("${PORT}" STREQUAL "catch2")
	if (LINUX)
		set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCATCH_CONFIG_NO_POSIX_SIGNALS=ON")
	elseif (WIN32)
		set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCATCH_CONFIG_NO_WINDOWS_SEH=ON")
	elseif (APPLE)
		set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCATCH_CONFIG_NO_POSIX_SIGNALS=ON")
	endif()
endif()
