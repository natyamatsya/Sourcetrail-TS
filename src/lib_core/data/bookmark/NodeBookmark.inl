// Inline implementations for NodeBookmark.h. Included at the end of that header; not a standalone TU.

#pragma once

inline NodeBookmark::NodeBookmark(
	const BookmarkId bookmarkId,
	const std::string& name,
	const std::string& comment,
	const TimeStamp& timeStamp,
	const BookmarkCategory& category)
	: Bookmark(bookmarkId, name, comment, timeStamp, category)
{
}

inline void NodeBookmark::addNodeId(const Id nodeId)
{
	m_nodeIds.push_back(nodeId);
}

inline void NodeBookmark::setNodeIds(const std::vector<Id>& nodeIds)
{
	m_nodeIds = nodeIds;
}

inline std::vector<Id> NodeBookmark::getNodeIds() const
{
	return m_nodeIds;
}
