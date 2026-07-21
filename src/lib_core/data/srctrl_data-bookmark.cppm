// `srctrl.data:bookmark` partition -- the bookmark data types (Bookmark, BookmarkCategory,
// EdgeBookmark, NodeBookmark). Module build only.
//
// Converted so the storage partitions stop GMF-including Bookmark.h: its textual TimeStamp.h
// pull put a global-module TimeStamp into their BMIs, which clashes with the
// `srctrl.utility:time`-attached TimeStamp once both are loaded in one importer.

module;

#ifndef SRCTRL_IMPORT_STD
#include <cstddef>
#include <string>
#include <vector>
#endif

// Non-modularized GMF dep: Id (types.h). TimeStamp comes from the import below, NOT the GMF.
#include "types.h"

export module srctrl.data:bookmark;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // TimeStamp (the :time partition)

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// Base types before the deriving bookmarks (their guarded #includes are skipped in the purview).
#include "BookmarkCategory.h"
#include "Bookmark.h"
#include "EdgeBookmark.h"
#include "NodeBookmark.h"

// close the purview-wide extern "C++" linkage block
}
