#ifndef PLATFORM_H
#define PLATFORM_H

#include <cstddef>
#include <string>
#include <string_view>

namespace utility
{

class Platform final
{
	public:
		enum class Architecture
		{
			BITS_32,
			BITS_64
		};

		static constexpr bool isLinux()
		{
			// These Unix implementations are similar enough for our purpose, so we don't
			// distinguish them further for now:

#if defined(__linux__) || defined(__FreeBSD__)
			return true;
#else
			return false;
#endif
		}

		static constexpr bool isWindows()
		{
#if defined(_WIN32)
			return true;
#else
			return false;
#endif
		}

		static constexpr bool isMac()
		{
#if defined(__APPLE__)
			return true;
#else
			return false;
#endif
		}

		// Compile-time filename suffix for executables on this platform (".exe" on Windows, else "").
		static constexpr std::string_view getExecutableExtension()
		{
			if constexpr (isWindows())
				return ".exe";
			else
				return "";
		}

		static std::string getName();

		static Architecture getArchitecture();
		static std::string getArchitectureName();
		static std::string getArchitectureName(Architecture architecture);
};

}

#endif // PLATFORM_H
