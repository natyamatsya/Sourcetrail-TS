#include "CxxName.h"

#include "utilityString.h"

std::string CxxNameParent::getTemplateSuffix(const std::vector<std::string>& elements)
{
	if (elements.size())
	{
		return '<' + utility::join(elements, ", ") + '>';
	}

	return "";
}
