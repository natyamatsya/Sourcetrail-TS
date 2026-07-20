#ifndef EDGE_BOOKMARK_H
#define EDGE_BOOKMARK_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>

#include "Bookmark.h"
#endif

SRCTRL_EXPORT class EdgeBookmark: public Bookmark
{
public:
	EdgeBookmark(
		const BookmarkId bookmarkId,
		const std::string& name,
		const std::string& comment,
		const TimeStamp& timeStamp,
		const BookmarkCategory& category);

	void addEdgeId(const Id edgeId);
	void setEdgeIds(const std::vector<Id>& edgesIds);
	std::vector<Id> getEdgeIds() const;

	void setActiveNodeId(const Id activeNodeId);
	Id getActiveNodeId() const;

private:
	std::vector<Id> m_edgeIds;
	Id m_activeNodeId;
};

#include "EdgeBookmark.inl"

#endif	  // EDGE_BOOKMARK_H
