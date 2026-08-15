#ifndef LANGUAGE_MASK_H
#define LANGUAGE_MASK_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#endif

// Which languages produced a node, as a bitmask (StorageNode::languages).
//
// A *mask* rather than a single value, because the interesting nodes are the
// ones with more than one bit: the merge ORs the bits of every producer that
// recorded the same serialized name, so a node two languages both claim says so
// in its own row. That is either a real boundary (see
// context/DESIGN_XLANG_BOUNDARIES.md), a file — which every indexer names the
// same way on purpose — or an accidental name collision, and until X1 measures
// them we cannot tell those apart. This column is what makes the question
// answerable at all.
//
// Deliberately not `LanguageType` (settings/LanguageType.h): that is a
// project-level choice of what to index, this is a fact about a row, and the
// storage layer does not depend on the settings layer.
//
// Persisted to the SQLite DB and the IPC wire as the raw int, so values are
// append-only — never renumber.
SRCTRL_EXPORT enum LanguageBit
{
	LANGUAGE_NONE = 0,
	LANGUAGE_CXX = 1 << 0,
	LANGUAGE_RUST = 1 << 1,
	LANGUAGE_SWIFT = 1 << 2,
	LANGUAGE_ZIG = 1 << 3,
};

SRCTRL_EXPORT using LanguageMask = int;

SRCTRL_EXPORT inline bool languageMaskHas(LanguageMask languages, LanguageBit bit)
{
	return (languages & bit) != 0;
}

// Whether more than one language claims this node — the boundary predicate.
// `x & (x - 1)` clears the lowest set bit, so a non-zero result means at least
// two bits were set.
SRCTRL_EXPORT inline bool languageMaskIsShared(LanguageMask languages)
{
	return (languages & (languages - 1)) != 0;
}

// Space-joined labels for the set languages ("cxx", "cxx rust"), for readable
// node descriptions and tooltips. Empty when none.
SRCTRL_EXPORT std::string languageMaskToString(LanguageMask languages);

// The same set spelled for a reader rather than for a log line: "C++",
// "C++ + Swift". Used for group titles and anywhere a person sees the mask.
SRCTRL_EXPORT std::string languageMaskToDisplayString(LanguageMask languages);

#include "LanguageMask.inl"

#endif	  // LANGUAGE_MASK_H
