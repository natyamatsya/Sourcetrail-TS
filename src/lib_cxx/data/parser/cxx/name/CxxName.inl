// Inline implementations for CxxName.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityString.h"
#endif

inline std::string CxxNameParent::getTemplateSuffix(const std::vector<std::string>& elements)
{
	if (elements.size())
	{
		return '<' + utility::join(elements, ", ") + '>';
	}

	return "";
}
