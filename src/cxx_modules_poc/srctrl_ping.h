#ifndef SRCTRL_PING_H
#define SRCTRL_PING_H

// POC leaf for the dual-build scaffolding. In a header build this is included directly; in a module
// build it is included in srctrl_ping.cppm's purview, where SRCTRL_EXPORT becomes `export`.

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW	// in a module build these are hoisted into the wrapper's GMF
	#include <string>
#endif

SRCTRL_EXPORT inline std::string srctrl_ping()
{
	return "pong";
}

#endif	  // SRCTRL_PING_H
