#include "Platform.h"

namespace utility
{
using namespace std;
using namespace std::string_literals;

string Platform::getName()
{
	static_assert(Platform::isLinux() || Platform::isWindows() || Platform::isMac(), "Unknown platform!");
	static_assert((int)Platform::isLinux() + (int)Platform::isWindows() + (int)Platform::isMac() == 1, "Multiple platforms detected!");

	if constexpr (isLinux())
		return "Linux"s;
	else if constexpr (isWindows())
		return "Windows"s;
	else if constexpr (isMac())
		return "Mac"s;
}

Platform::Architecture Platform::getArchitecture()
{
	static_assert(sizeof(void*) == 4 || sizeof(void*) == 8, "Unknown architecture!");

	if constexpr (sizeof(void*) == 4)
		return Architecture::BITS_32;
	else if constexpr (sizeof(void*) == 8)
		return Architecture::BITS_64;
}

string Platform::getArchitectureName()
{
	return getArchitectureName(getArchitecture());
}

string Platform::getArchitectureName(Architecture architecture)
{
	switch (architecture)
	{
		case Architecture::BITS_32:
			return "32 Bit"s;
		case Architecture::BITS_64:
			return "64 Bit"s;
		default:
			return ""s;
	}
}

}
