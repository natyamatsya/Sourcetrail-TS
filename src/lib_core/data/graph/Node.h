#ifndef NODE_H
#define NODE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <algorithm>
#include <functional>
#include <map>
#include <ostream>
#include <sstream>
#include <string>

#include "DefinitionKind.h"
#include "Edge.h"
#include "LogFacade.h"
#include "NameHierarchy.h"
#include "NodeModifier.h"
#include "NodeType.h"
#include "Token.h"
#include "TokenComponentAccess.h"
#include "TokenComponentConst.h"
#include "TokenComponentStatic.h"
#endif

SRCTRL_EXPORT class Node: public Token
{
public:
	Node(Id id, NodeType type, NameHierarchy nameHierarchy, DefinitionKind definitionKind);
	Node(const Node& other);
	~Node() override;

	NodeType getType() const;
	void setType(NodeType type);
	bool isType(NodeKindMask mask) const;

	// Orthogonal node modifiers (SW13): e.g. a Swift `actor` is a class node with
	// the actor modifier set.
	NodeModifierMask getModifiers() const;
	void setModifiers(NodeModifierMask modifiers);
	bool isActor() const;
	bool isDeprecated() const;

	std::string getName() const;
	std::string getFullName() const;
	const NameHierarchy& getNameHierarchy() const;

	bool isDefined() const;
	bool isImplicit() const;
	bool isExplicit() const;

	std::size_t getChildCount() const;
	void setChildCount(std::size_t childCount);

	std::size_t getEdgeCount() const;

	void addEdge(Edge* edge);
	void removeEdge(Edge* edge);

	Node* getParentNode() const;
	Node* getLastParentNode();
	Edge* getMemberEdge() const;
	bool isParentOf(const Node* node) const;

	Edge* findEdge(std::function<bool(Edge*)> func) const;
	Edge* findEdgeOfType(Edge::TypeMask mask) const;
	Edge* findEdgeOfType(Edge::TypeMask mask, std::function<bool(Edge*)> func) const;
	Node* findChildNode(std::function<bool(Node*)> func) const;

	void forEachEdge(std::function<void(Edge*)> func) const;
	void forEachEdgeOfType(Edge::TypeMask mask, std::function<void(Edge*)> func) const;
	void forEachChildNode(std::function<void(Node*)> func) const;
	void forEachNodeRecursive(std::function<void(const Node*)> func) const;

	// Token implementation.
	bool isNode() const override;
	bool isEdge() const override;

	// Logging.
	std::string getReadableTypeString() const override;
	std::string getAsString() const;

private:
	void operator=(const Node&);

	std::map<Id, Edge*> m_edges;

	NodeType m_type;
	const NameHierarchy m_nameHierarchy;
	DefinitionKind m_definitionKind;
	NodeModifierMask m_modifiers = NODE_MODIFIER_NONE;

	std::size_t m_childCount = 0;
};

SRCTRL_EXPORT std::ostream& operator<<(std::ostream& ostream, const Node& node);

// Node <-> Edge mutual dependency: Edge is complete (via our top #include above) and Node is complete
// (just defined), so this is the one point where BOTH .inl bodies can be pulled -- including Edge.inl,
// which Edge.h deliberately leaves to us (see the note there). In a module build the wrapper includes
// every class definition first and all .inls afterward, so guard these.
#ifndef SRCTRL_MODULE_PURVIEW
#include "Edge.inl"
#include "Node.inl"
#endif

#endif	  // NODE_H
