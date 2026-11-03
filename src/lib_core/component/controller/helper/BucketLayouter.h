#ifndef BUCKET_LAYOUTER_H
#define BUCKET_LAYOUTER_H

#include <map>
#include <unordered_map>

#include "Vector2.h"
#include "types.h"

#include "DummyNode.h"

struct DummyEdge;

class Bucket
{
public:
	Bucket();
	Bucket(int i, int j);

	int getWidth() const;
	int getHeight() const;

	bool hasNode(std::shared_ptr<DummyNode> node) const;
	void addNode(std::shared_ptr<DummyNode> node);
	const DummyNode::BundledNodesSet& getNodes() const;

	void preLayout(Vec2i viewSize, bool addVerticalSplit, bool forceVerticalSplit);
	void layout(int x, int y, int width, int height);

	const std::vector<int> getColWidths() const;
	int getMiddleGapX() const;

	int i;
	int j;

private:
	int m_width;
	int m_height;

	DummyNode::BundledNodesSet m_nodes;

	std::vector<int> m_colWidths;
};


class BucketLayouter
{
public:
	BucketLayouter(Vec2i viewSize);
	void createBuckets(
		std::vector<std::shared_ptr<DummyNode>>& nodes,
		const std::vector<std::shared_ptr<DummyEdge>>& edges);
	void layoutBuckets(bool addVerticalSplit);

	std::vector<std::shared_ptr<DummyNode>> getSortedNodes();

private:
	// Maps each visible node's token id to the top-level node it is drawn in; the first one in
	// depth-first order wins when several nodes share a token id.
	static void addTopMostDummyNodesRecursive(
		const std::vector<std::shared_ptr<DummyNode>>& nodes,
		const std::shared_ptr<DummyNode>& top,
		std::unordered_map<Id, std::shared_ptr<DummyNode>>& topMostNodes);

	Bucket* getBucket(int i, int j);
	Bucket* getBucket(const std::shared_ptr<DummyNode>& node) const;
	void addNode(Bucket& bucket, const std::shared_ptr<DummyNode>& node);

	Vec2i m_viewSize;
	std::map<int, std::map<int, Bucket>> m_buckets;

	int m_i1 = 0;
	int m_j1 = 0;
	int m_i2 = 0;
	int m_j2 = 0;

	DummyNode* m_activeParentNode = nullptr;

	// The bucket each node was added to (a node goes into one bucket only).
	std::unordered_map<const DummyNode*, Bucket*> m_nodeBuckets;
};

#endif	  // BUCKET_LAYOUTER_H
