#include "Catch2.hpp"

#include <memory>
#include <vector>

#include "BucketLayouter.h"
#include "DummyEdge.h"
#include "DummyNode.h"

namespace
{
std::shared_ptr<DummyNode> shown(Id id, const std::string& name)
{
	auto node = std::make_shared<DummyNode>(DummyNode::Type::DUMMY_DATA);
	node->tokenId = id;
	node->name = name;
	node->visible = true;
	node->size = Vec2i(100, 30);
	return node;
}

std::vector<std::string> names(const std::vector<std::shared_ptr<DummyNode>>& nodes)
{
	std::vector<std::string> result;
	for (const std::shared_ptr<DummyNode>& node: nodes)
	{
		result.push_back(node->name);
	}
	return result;
}
}	 // namespace

TEST_CASE("bucket layouter places an edge's ends by the top-level nodes they are drawn in")
{
	// Caller -> active.member -> Callee.member, and an edge between two unplaced nodes that waits
	// until one of them is placed (Other -> Caller).
	auto active = shown(Id(10), "Active");
	active->active = true;
	auto activeMember = shown(Id(11), "member");
	active->subNodes = {activeMember};

	auto caller = shown(Id(20), "Caller");

	auto callee = shown(Id(30), "Callee");
	auto hiddenTwin = shown(Id(31), "hidden twin");
	hiddenTwin->visible = false;	// shares the token id below, but is not drawn: skipped
	auto calleeMember = shown(Id(31), "callee member");
	callee->subNodes = {hiddenTwin, calleeMember};

	auto other = shown(Id(40), "Other");

	std::vector<std::shared_ptr<DummyNode>> nodes{other, caller, active, callee};
	const std::vector<std::shared_ptr<DummyEdge>> edges{
		std::make_shared<DummyEdge>(Id(40), Id(20), nullptr),
		std::make_shared<DummyEdge>(Id(20), Id(11), nullptr),
		std::make_shared<DummyEdge>(Id(11), Id(31), nullptr),
		std::make_shared<DummyEdge>(Id(99), Id(10), nullptr),	 // an end drawn nowhere
	};

	BucketLayouter layouter(Vec2i(1000, 1000));
	layouter.createBuckets(nodes, edges);

	// Columns left to right: Other, Caller, Active, Callee.
	REQUIRE(
		names(layouter.getSortedNodes()) ==
		std::vector<std::string>{"Other", "Caller", "Active", "Callee"});
	for (const std::shared_ptr<DummyEdge>& edge: edges)
	{
		REQUIRE(edge->layoutHorizontal);
	}
}

TEST_CASE("bucket layouter stacks vertical bundles above and below")
{
	auto active = shown(Id(10), "Active");
	active->active = true;
	auto base = shown(Id(20), "Base");
	base->bundleInfo.layoutVertical = true;
	base->bundleInfo.isReferenced = true;

	std::vector<std::shared_ptr<DummyNode>> nodes{active, base};
	const std::vector<std::shared_ptr<DummyEdge>> edges{
		std::make_shared<DummyEdge>(Id(10), Id(20), nullptr)};

	BucketLayouter layouter(Vec2i(1000, 1000));
	layouter.createBuckets(nodes, edges);

	// The referenced vertical node goes above (a row before) the active one.
	REQUIRE(names(layouter.getSortedNodes()) == std::vector<std::string>{"Base", "Active"});
	REQUIRE_FALSE(edges[0]->layoutHorizontal);
}
