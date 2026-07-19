// Inline implementations for NodeModifier.h (included at the end of that header). An out-of-line
// definition of an exported free function does not resolve for module importers, so it lives here as
// `inline`.

#pragma once

inline std::string nodeModifierToString(NodeModifierMask modifiers)
{
	std::string result;
	const auto add = [&result](const char* label) {
		if (!result.empty())
		{
			result += ' ';
		}
		result += label;
	};

	if (nodeModifierHas(modifiers, NODE_MODIFIER_EXPORTED))
	{
		add("exported");
	}
	if (nodeModifierHas(modifiers, NODE_MODIFIER_DEPRECATED))
	{
		add("deprecated");
	}
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
