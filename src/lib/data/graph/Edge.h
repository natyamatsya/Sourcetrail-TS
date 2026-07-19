#ifndef EDGE_H
#define EDGE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <ostream>
#include <sstream>
#include <string>

#include "LogFacade.h"
#include "Token.h"
#include "TokenComponentBundledEdges.h"
#include "utilityEnum.h"
#include "utilityString.h"
#endif

// A module-type forward declaration must carry SRCTRL_EXPORT.
SRCTRL_EXPORT class Node;

SRCTRL_EXPORT class Edge: public Token
{
public:
	typedef int TypeMask;
	enum EdgeType : TypeMask
	{
		EDGE_UNDEFINED = 0,

		EDGE_MEMBER = 1 << 0,
		EDGE_TYPE_USAGE = 1 << 1,
		EDGE_USAGE = 1 << 2,
		EDGE_CALL = 1 << 3,
		EDGE_INHERITANCE = 1 << 4,
		EDGE_OVERRIDE = 1 << 5,
		EDGE_TYPE_ARGUMENT = 1 << 6,
		EDGE_TEMPLATE_SPECIALIZATION = 1 << 7,
		EDGE_INCLUDE = 1 << 8,
		EDGE_IMPORT = 1 << 9,
		EDGE_BUNDLED_EDGES = 1 << 10,
		EDGE_MACRO_USAGE = 1 << 11,
		EDGE_ANNOTATION_USAGE = 1 << 12,
	};
	static constexpr EdgeType EDGE_TYPE_MAX_VALUE = EDGE_ANNOTATION_USAGE;


	static const TypeMask LAYOUT_VERTICAL = EDGE_INHERITANCE | EDGE_OVERRIDE |
		EDGE_TEMPLATE_SPECIALIZATION;

	Edge(Id id, EdgeType type, Node* from, Node* to);
	Edge(const Edge& other, Node* from, Node* to);
	~Edge() override;

	EdgeType getType() const;
	bool isType(TypeMask mask) const;

	Node* getFrom() const;
	Node* getTo() const;

	std::string getName() const;

	// Token implementation
	bool isNode() const override;
	bool isEdge() const override;

	static std::string getUnderscoredTypeString(EdgeType type);
	static std::string getReadableTypeString(EdgeType type);
	static EdgeType getTypeForReadableTypeString(const std::string& str);

	// Logging.
	std::string getReadableTypeString() const override;
	std::string getAsString() const;

private:
	const EdgeType m_type;

	Node* const m_from;
	Node* const m_to;
};

// An explicit specialization of the imported `intToEnum` template (declared+defined inline in the
// .inl). Explicit specializations aren't separately `export`ed -- they're found via the primary.
template <>
Edge::EdgeType intToEnum(int value);

SRCTRL_EXPORT std::ostream& operator<<(std::ostream& ostream, const Edge& edge);

// Edge's inline bodies dereference Node (m_from->addEdge(), getFrom()->getFullName(), ...), so the
// complete Node type must precede Edge.inl. Node <-> Edge is a mutual dependency: Node's class body in
// turn needs Edge complete (Edge::TypeMask appears in its signatures), so Node.h #includes us at its
// top. Pull Node.h in here (after our class, which needs only a Node forward declaration) -- once both
// classes are complete Node.h includes BOTH Edge.inl and Node.inl. We deliberately do NOT include
// Edge.inl ourselves: on a direct <Node.h> entry the nested re-include of us is guard-skipped, so Node
// would still be incomplete were we to pull Edge.inl here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "Node.h"
#endif

#endif	  // EDGE_H
