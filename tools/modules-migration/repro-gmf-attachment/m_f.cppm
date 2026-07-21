module;
#include <string>
export module m:f;
import a;
import :t;
export inline std::string viaFrontend(const std::string& x) { return utility::trim(x); }
