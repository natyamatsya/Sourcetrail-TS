// `srctrl.indexer` -- the language-agnostic indexer framework: the type-erased IndexerCommand
// value (+ its payload concept and type enum), the IndexerBase interface with its
// std::expected-based error channel, the Indexer<T> template every language indexer derives from,
// the ParserClientImpl recording sink, IndexerComposite, and the command providers
// (IndexerCommandProvider + Memory/Combined), plus the language-package registry
// (LanguagePackage / LanguagePackageManager) that instantiates the per-language indexers. A standalone module (no partitions)
// ABOVE srctrl.data and srctrl.storage -- the framework spans the parser-facing interface
// (ParserClient) and the storage sink (IntermediateStorage), and srctrl.storage already imports
// srctrl.data, so this cannot live inside either. Module build only.
//
// Deliberately NOT here: TaskBuildIndex / InterprocessIndexer (they drag
// messaging, scheduling, and the interprocess layer -- later slices), and IndexerStateInfo /
// utilityExpected (GMF-safe plain headers, shared with srctrl.cxx's GMFs).

module;

#ifndef SRCTRL_IMPORT_STD
#include <concepts>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#endif

// The P2988 optional<T&> shim (IndexerCommand::target), like srctrl.cxx:name's GMF.
#include <stdcompat/optional>

// Non-modularized GMF deps: the generated language-package flags, Id/types, the logging macros,
// and two GMF-safe plain headers (IndexerStateInfo -- a 10-line struct; utilityExpected --
// std-only expected helpers, also in srctrl.cxx's GMFs).
#include "language_package_flags.h"
#include "types.h"
#include "logging.h"
#include "IndexerStateInfo.h"
#include "utilityExpected.h"

export module srctrl.indexer;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utility::append (:containers)
import srctrl.file;      // FilePath (IndexerCommand's source file)
import srctrl.data;      // ParserClient (:parser), ParseLocation (:location), Edge/NodeKind
                         // (:graph), the classification enums (:types)
import srctrl.storage;   // IntermediateStorage (the index result / recording sink)
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
// Class definitions in dependency order, then the inline bodies (each header's own
// bottom-include is purview-guarded).
#include "IndexerCommandType.h"
#include "IndexerCommand.h"
#include "IndexerBase.h"
#include "ParserClientImpl.h"
#include "Indexer.h"
#include "IndexerComposite.h"
#include "IndexerCommandProvider.h"
#include "MemoryIndexerCommandProvider.h"
#include "CombinedIndexerCommandProvider.h"
#include "LanguagePackage.h"
#include "LanguagePackageManager.h"

#include "IndexerCommandType.inl"
#include "ParserClientImpl.inl"
#include "IndexerComposite.inl"
#include "IndexerCommandProvider.inl"
#include "MemoryIndexerCommandProvider.inl"
#include "CombinedIndexerCommandProvider.inl"
#include "LanguagePackageManager.inl"
