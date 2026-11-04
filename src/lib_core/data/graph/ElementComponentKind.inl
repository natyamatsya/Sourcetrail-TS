// Inline implementations for ElementComponentKind.h (included at the end of that header).
// `intToEnum` / `lookupEnum` come from utilityEnum (srctrl.utility) -- via #include in the header
// build, via `import srctrl.utility;` in the module build.

#pragma once

template <>
inline ElementComponentKind intToEnum(int value)
{
	using enum ElementComponentKind;
	switch (static_cast<ElementComponentKind>(value))
	{
		case ELEMENT_COMPONENT_IS_AMBIGUOUS:
			return ELEMENT_COMPONENT_IS_AMBIGUOUS;
		case ELEMENT_COMPONENT_CALL_DISPATCHES:
			return ELEMENT_COMPONENT_CALL_DISPATCHES;
		case ELEMENT_COMPONENT_CALL_STATIC:
			return ELEMENT_COMPONENT_CALL_STATIC;
		case ELEMENT_COMPONENT_STATIC_DISPATCH_VIA:
			return ELEMENT_COMPONENT_STATIC_DISPATCH_VIA;
		default:
			break;
	}
	return ELEMENT_COMPONENT_NONE;
}
