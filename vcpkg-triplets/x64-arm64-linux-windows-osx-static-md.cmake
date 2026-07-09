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

	# Align the deployment target with the application build. Without this vcpkg
	# defaults to the SDK's version while CMake defaults to the HOST's version, so
	# every link step warns "object file was built for newer macOS than being
	# linked" (the SDK usually runs ahead of the OS, e.g. 26.4 vs 26.0). Honor an
	# explicit MACOSX_DEPLOYMENT_TARGET (which CMake honors natively on its side),
	# otherwise pin to the host's major version -- the same value CMake picks.
	# Note: brew-provided Qt/LLVM target the host version anyway, so an older
	# floor here would not make the resulting binaries any more portable.
	if (DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
		set(VCPKG_OSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}")
	else()
		execute_process(
			COMMAND sw_vers -productVersion
			OUTPUT_VARIABLE hostOsVersion
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
		string(REGEX MATCH "^[0-9]+" hostOsMajorVersion "${hostOsVersion}")
		set(VCPKG_OSX_DEPLOYMENT_TARGET "${hostOsMajorVersion}.0")
	endif()
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
