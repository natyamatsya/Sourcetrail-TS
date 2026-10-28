// Inline implementations for CxxName.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <algorithm>
#include <iterator>

#include "utilityString.h"
#endif

inline std::string CxxNameParent::getTemplateSuffix(const std::vector<std::string>& elements)
{
	if (elements.size())
	{
		// An empty pack is an empty element: `tuple<>`, and `f<int>` for f<int, Ts...> with no Ts.
		std::vector<std::string> nonEmptyElements;
		std::ranges::copy_if(
			elements, std::back_inserter(nonEmptyElements), [](const std::string& element) {
				return !element.empty();
			});
		return '<' + utility::join(nonEmptyElements, ", ") + '>';
	}

	return "";
}
