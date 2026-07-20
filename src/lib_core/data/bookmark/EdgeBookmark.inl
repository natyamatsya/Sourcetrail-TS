// Inline implementations for EdgeBookmark.h. Included at the end of that header; not a standalone TU.

#pragma once

inline EdgeBookmark::EdgeBookmark(
	const BookmarkId bookmarkId,
	const std::string& name,
	const std::string& comment,
	const TimeStamp& timeStamp,
	const BookmarkCategory& category)
	: Bookmark(bookmarkId, name, comment, timeStamp, category)
{
}

inline void EdgeBookmark::addEdgeId(const Id edgeId)
{
	m_edgeIds.push_back(edgeId);
}

inline void EdgeBookmark::setEdgeIds(const std::vector<Id>& edgesIds)
{
	m_edgeIds = edgesIds;
}

inline std::vector<Id> EdgeBookmark::getEdgeIds() const
{
	return m_edgeIds;
}

inline void EdgeBookmark::setActiveNodeId(const Id activeNodeId)
{
	m_activeNodeId = activeNodeId;
}

inline Id EdgeBookmark::getActiveNodeId() const
{
	return m_activeNodeId;
}
