#include "Catch2.hpp"

#include <memory>

#include "DisplayedGraph.h"
#include "DummyNode.h"
#include "GroupType.h"
#include "NodeType.h"

namespace
{
std::shared_ptr<DummyNode> shown(DummyNode::Type type, Id id, const std::string& name)
{
	auto node = std::make_shared<DummyNode>(type);
	node->tokenId = id;
	node->name = name;
	node->visible = true;
	return node;
}
}	 // namespace

TEST_CASE("displayed graph lists what the view draws, members inside their node")
{
	using enum DummyNode::Type;
	auto player = shown(DUMMY_DATA, Id(10), "Player");
	player->active = true;
	auto members = shown(DUMMY_ACCESS, Id(0), "public");
	members->subNodes = {shown(DUMMY_DATA, Id(11), "Turn"), shown(DUMMY_DATA, Id(12), "getName")};
	members->subNodes[1]->visible = false;	// collapsed away
	auto toggle = shown(DUMMY_EXPAND_TOGGLE, Id(0), "");
	toggle->invisibleSubNodeCount = 5;
	player->subNodes = {members, toggle};

	auto callers = shown(DUMMY_BUNDLE, Id(900), "Referencing Symbols");
	callers->bundledNodeCount = 12;
	callers->bundledNodeType = NodeType(NODE_FUNCTION);

	auto language = shown(DUMMY_GROUP, Id(901), "C++");
	language->groupType = GroupType::LANGUAGE;
	language->subNodes = {shown(DUMMY_DATA, Id(20), "main")};

	auto hidden = shown(DUMMY_DATA, Id(30), "gone");
	hidden->visible = false;

	DisplayedGraph::instance().set(TabIds::app(), {player, callers, language, hidden});
	const std::vector<DisplayedGraphNode> nodes = DisplayedGraph::instance().get(TabIds::app());
	REQUIRE(nodes.size() == 5);

	REQUIRE(nodes[0].name == "Player");
	REQUIRE(nodes[0].active);
	REQUIRE(nodes[0].expandable);
	REQUIRE_FALSE(nodes[0].expanded);
	REQUIRE(nodes[0].hiddenMembers == 5);
	REQUIRE(nodes[1].name == "Turn");
	REQUIRE(nodes[1].parent == 0);	// drawn inside Player; getName is not drawn

	REQUIRE(nodes[2].kind == DisplayedGraphNode::Kind::Bundle);
	REQUIRE(nodes[2].id == Id(900));
	REQUIRE(nodes[2].bundled == 12);
	REQUIRE(nodes[2].splittable);
	REQUIRE(nodes[2].splitRemovesOthers);
	REQUIRE(nodes[2].splitToList);

	REQUIRE(nodes[3].kind == DisplayedGraphNode::Kind::Group);
	REQUIRE(nodes[3].group == "language");
	REQUIRE_FALSE(nodes[3].splittable);	// language groups are inert
	REQUIRE(nodes[4].name == "main");
	REQUIRE(nodes[4].parent == 3);

	DisplayedGraph::instance().clear(TabIds::app());
	REQUIRE(DisplayedGraph::instance().get(TabIds::app()).empty());
}
