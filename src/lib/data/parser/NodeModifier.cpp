#include "NodeModifier.h"

std::string nodeModifierToString(NodeModifierMask modifiers)
{
	std::string result;
	const auto add = [&result](const char* label) {
		if (!result.empty())
		{
			result += ' ';
		}
		result += label;
	};

	if (nodeModifierHas(modifiers, NODE_MODIFIER_NONISOLATED))
	{
		add("nonisolated");
	}
	if (nodeModifierHas(modifiers, NODE_MODIFIER_ASYNC))
	{
		add("async");
	}
	if (nodeModifierHas(modifiers, NODE_MODIFIER_ACTOR))
	{
		add("actor");
	}
	return result;
}
