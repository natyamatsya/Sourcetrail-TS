#include "NodeModifier.h"

std::string nodeModifierToString(NodeModifierMask modifiers)
{
	if (nodeModifierHas(modifiers, NODE_MODIFIER_ACTOR))
	{
		return "actor";
	}
	return "";
}
