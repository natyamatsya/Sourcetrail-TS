#ifndef NODE_BOOKMARK_H
#define NODE_BOOKMARK_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>

#include "Bookmark.h"
#endif

SRCTRL_EXPORT class NodeBookmark: public Bookmark
{
public:
	NodeBookmark(
		const BookmarkId bookmarkId,
		const std::string& name,
		const std::string& comment,
		const TimeStamp& timeStamp,
		const BookmarkCategory& category);

	void addNodeId(const Id nodeId);
	void setNodeIds(const std::vector<Id>& nodeIds);
	std::vector<Id> getNodeIds() const;

private:
	std::vector<Id> m_nodeIds;
};

#include "NodeBookmark.inl"

#endif	  // NODE_BOOKMARK_H
