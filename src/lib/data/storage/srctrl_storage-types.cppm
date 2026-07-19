// `srctrl.storage:types` partition -- the storage record POD structs (StorageNode/Edge/File/Error/
// SourceLocation/Symbol/... + the bookmark records). Header-only structs; module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <string>
#endif

// Non-modularized deps stay global-module: Id/types.h, FilePath, and the not-yet-converted Bookmark
// data types (Bookmark/BookmarkCategory/TimeStamp -- none pull a modularized header, so GMF is safe).
// The classification enums (NodeKind/AccessKind/... incl. NodeAttributeKind) and Edge come from the
// import below, NOT the GMF.
#include "types.h"
#include "Id.h"
#include "FilePath.h"
#include "Bookmark.h"

export module srctrl.storage:types;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.data;   // :types classification enums (incl. NodeAttributeKind) + :graph Edge

#define SRCTRL_MODULE_PURVIEW
// All 15 records are independent PODs (none includes another storage type header), so order is free.
#include "StorageComponentAccess.h"
#include "StorageEdge.h"
#include "StorageElementComponent.h"
#include "StorageError.h"
#include "StorageFile.h"
#include "StorageLocalSymbol.h"
#include "StorageNode.h"
#include "StorageNodeAttribute.h"
#include "StorageOccurrence.h"
#include "StorageSourceLocation.h"
#include "StorageSymbol.h"
#include "StorageBookmarkCategory.h"
#include "StorageBookmark.h"
#include "StorageBookmarkedEdge.h"
#include "StorageBookmarkedNode.h"
