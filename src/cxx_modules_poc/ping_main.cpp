// Consumer for the srctrl.ping POC: `import` in a module build, `#include` otherwise. This is the
// shape every future consumer of a converted module uses.

#ifdef SRCTRL_MODULE_BUILD
import srctrl.ping;
#else
	#include "srctrl_ping.h"
#endif

#include <cstdio>

int main()
{
	std::printf("srctrl_ping() = %s\n", srctrl_ping().c_str());
	return srctrl_ping() == "pong" ? 0 : 1;
}
