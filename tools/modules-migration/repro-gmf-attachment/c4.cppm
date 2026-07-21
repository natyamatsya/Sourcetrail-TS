module;
#include <string>
export module c4;
import a;
import b2;
export inline std::string g(const std::string& x)
{
	return utility::trim(x);
}
