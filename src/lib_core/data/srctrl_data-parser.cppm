// `srctrl.data:parser` partition -- the parser-facing recording interface (ParserClient, the
// abstract seam every language indexer records through) and the Parser base class the language
// parsers derive from. Module build only.
//
// Pure interface: every parameter type is already modularized (NameHierarchy via :name,
// ParseLocation via :location, the classification enums via :types, FilePath via srctrl.file).

module;

#ifndef SRCTRL_IMPORT_STD
#include <string>
#endif

#include "types.h"

export module srctrl.data:parser;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.file;      // FilePath (recordFile / recordError)
import :types;           // AccessKind / DefinitionKind / SymbolKind / ReferenceKind / NodeAttributeKind / NodeModifier
import :name;            // NameHierarchy
import :location;        // ParseLocation

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "ParserClient.h"
#include "Parser.h"
#include "Parser.inl"

// close the purview-wide extern "C++" linkage block
}
