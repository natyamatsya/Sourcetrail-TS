// Consumer for the srctrl.ping POC: `import` in a module build, `#include` otherwise. This is the
// shape every future consumer of a converted module uses.

// Textual std includes come BEFORE the import (playbook rule 1: include-before-import). <string> is
// textual because a non-member operator like `std::string == const char*` is NOT visible through
// `import srctrl.ping` alone -- the wrapper's <string> lives in its global-module fragment and is not
// exported, so on MSVC the comparison below fails to find operator== without this textual include.
#include <cstdio>
#include <string>

#ifdef SRCTRL_MODULE_BUILD
import srctrl.ping;
#else
	#include "srctrl_ping.h"
#endif

int main()
{
	std::printf("srctrl_ping() = %s\n", srctrl_ping().c_str());
	return srctrl_ping() == "pong" ? 0 : 1;
}
