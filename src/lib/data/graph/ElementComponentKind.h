#ifndef ELEMENT_COMPONENT_KIND_H
#define ELEMENT_COMPONENT_KIND_H

#include "utilityEnum.h"

enum class ElementComponentKind
{
	ELEMENT_COMPONENT_NONE = 0,
	ELEMENT_COMPONENT_IS_AMBIGUOUS = 1
};

template <>
ElementComponentKind intToEnum(int value);

#endif	  // ELEMENT_COMPONENT_KIND_H
