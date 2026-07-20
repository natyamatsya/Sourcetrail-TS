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
#include "ParserClient.h"
#include "Parser.h"
#include "Parser.inl"
