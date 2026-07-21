// `srctrl.storage:types` partition -- the storage record POD structs (StorageNode/Edge/File/Error/
// SourceLocation/Symbol/... + the bookmark records). Header-only structs; module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <string>
#endif

// Non-modularized deps stay global-module: Id/types.h. The classification enums
// (NodeKind/AccessKind/... incl. NodeAttributeKind), Edge, FilePath, and the bookmark data types
// (BookmarkId, TimeStamp via :bookmark/:time) come from the imports below, NOT the GMF.
#include "types.h"
#include "Id.h"

export module srctrl.storage:types;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.file;   // FilePath
import srctrl.data;   // :types classification enums (incl. NodeAttributeKind) + :graph Edge + :bookmark

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
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

// close the purview-wide extern "C++" linkage block
}
