// Inline implementations for NodeAttributeKind.h (included at the end of that header).
// `intToEnum` / `lookupEnum` come from utilityEnum (srctrl.utility) -- via #include in the header build,
// via `import srctrl.utility;` in the module build.

#pragma once

template <>
inline NodeAttributeKind intToEnum(int value)
{
	static const NodeAttributeKind NODE_ATTRIBUTE_KINDS[] = {
		NodeAttributeKind::NONE,
		NodeAttributeKind::AVAILABILITY,
		NodeAttributeKind::DEPRECATED,
		NodeAttributeKind::CFG,
		NodeAttributeKind::DOC_BRIEF
	};

	return lookupEnum(value, NODE_ATTRIBUTE_KINDS, NodeAttributeKind::NONE);
}

inline std::string nodeAttributeKindToString(NodeAttributeKind t)
{
	switch (t)
	{
	case NodeAttributeKind::NONE:
		return "";
	case NodeAttributeKind::AVAILABILITY:
		return "available";
	case NodeAttributeKind::DEPRECATED:
		return "deprecated";
	case NodeAttributeKind::CFG:
		return "cfg";
	case NodeAttributeKind::DOC_BRIEF:
		return "doc";
	}
	return "";
}
