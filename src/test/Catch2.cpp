#include "Catch2.hpp"

// Check that the correct switches are defined, so Catch2 is not intercepting JVM signals:

#if IS_VCPKG_BUILD
	#if defined(__linux__) || defined(__FreeBSD__)
		#if !defined(CATCH_CONFIG_NO_POSIX_SIGNALS)
			#error CATCH_CONFIG_NO_POSIX_SIGNALS is not defined!
		#endif
	#elif defined(_WIN32)
		#if !defined(CATCH_CONFIG_NO_WINDOWS_SEH)
			#error CATCH_CONFIG_NO_WINDOWS_SEH is not defined!
		#endif
	#endif
#endif
