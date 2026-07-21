module;
#include <string>
#include "s.h"
export module m:t;
export inline std::string viaCarrier(const std::string& x) { return utility::trim(x); }
