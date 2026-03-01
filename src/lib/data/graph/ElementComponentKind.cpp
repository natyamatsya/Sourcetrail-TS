#include "ElementComponentKind.h"

template <>
ElementComponentKind intToEnum(int value)
{
	using enum ElementComponentKind;
	switch (static_cast<ElementComponentKind>(value))
	{
		case ELEMENT_COMPONENT_IS_AMBIGUOUS:
			return ELEMENT_COMPONENT_IS_AMBIGUOUS;
		default:
			break;
	}
	return ELEMENT_COMPONENT_NONE;
}
