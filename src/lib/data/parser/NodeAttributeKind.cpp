#include "NodeAttributeKind.h"

namespace
{

const NodeAttributeKind NODE_ATTRIBUTE_KINDS[] = {
	NodeAttributeKind::NONE,
	NodeAttributeKind::AVAILABILITY,
	NodeAttributeKind::DEPRECATED,
	NodeAttributeKind::CFG,
	NodeAttributeKind::DOC_BRIEF
};

}

template <>
NodeAttributeKind intToEnum(int value)
{
	return lookupEnum(value, NODE_ATTRIBUTE_KINDS, NodeAttributeKind::NONE);
}

std::string nodeAttributeKindToString(NodeAttributeKind t)
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
