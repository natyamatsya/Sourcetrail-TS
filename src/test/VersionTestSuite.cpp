#include "Catch2.hpp"

#include "Version.h"

TEST_CASE("version display string is the release version without a suffix")
{
	REQUIRE(Version(0, 1, 0).toDisplayString() == "0.1.0");
}

TEST_CASE("version display string appends a pre-release or build suffix")
{
	REQUIRE(Version(0, 1, 0, "-rc.1").toDisplayString() == "0.1.0-rc.1");
	REQUIRE(Version(1, 12, 3, "+gabc1234").toDisplayString() == "1.12.3+gabc1234");
}
