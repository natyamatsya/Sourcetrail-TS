#ifndef STORAGE_BOOKMARKED_EDGE_H
#define STORAGE_BOOKMARKED_EDGE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "Bookmark.h"
#endif

SRCTRL_EXPORT struct StorageBookmarkedEdgeData
{
	StorageBookmarkedEdgeData()
		: bookmarkId(BookmarkId::NONE)
		, edgeType(0)
		, sourceNodeActive(false)
	{
	}

	StorageBookmarkedEdgeData(
		BookmarkId bookmarkId,
		const std::string& serializedSourceNodeName,
		const std::string& serializedTargetNodeName,
		int edgeType,
		bool sourceNodeActive)
		: bookmarkId(bookmarkId)
		, serializedSourceNodeName(serializedSourceNodeName)
		, serializedTargetNodeName(serializedTargetNodeName)
		, edgeType(edgeType)
		, sourceNodeActive(sourceNodeActive)
	{
	}

	BookmarkId bookmarkId;
	std::string serializedSourceNodeName;
	std::string serializedTargetNodeName;
	int edgeType;
	bool sourceNodeActive;
};

SRCTRL_EXPORT struct StorageBookmarkedEdge: public StorageBookmarkedEdgeData
{
	StorageBookmarkedEdge():  id(0) {}

	StorageBookmarkedEdge(Id id, const StorageBookmarkedEdgeData& data)
		: StorageBookmarkedEdgeData(data), id(id)
	{
	}

	StorageBookmarkedEdge(
		Id id,
		BookmarkId bookmarkId,
		const std::string& serializedSourceNodeName,
		const std::string& serializedTargetNodeName,
		int edgeType,
		bool sourceNodeActive)
		: StorageBookmarkedEdgeData(
			  bookmarkId, serializedSourceNodeName, serializedTargetNodeName, edgeType, sourceNodeActive)
		, id(id)
	{
	}

	Id id;
};

#endif	  // STORAGE_BOOKMARKED_EDGE_H
