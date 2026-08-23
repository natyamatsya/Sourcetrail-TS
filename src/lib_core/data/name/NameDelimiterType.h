#ifndef NAME_DELIMITER_TYPE_H
#define NAME_DELIMITER_TYPE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <array>
#include <string>
#endif

SRCTRL_EXPORT enum class NameDelimiterType
{
	UNKNOWN,
	FILE,
	CXX,
	CXX_MODULE,
	// The reserved namespace for contract atoms -- the nodes that stand for a
	// thing two languages agree about (a C ABI symbol) rather than for a
	// declaration in anybody's source. Reserved so that an ordinary declaration
	// can never be spelled with it, which is what lets two producers emit the
	// same atom name and have the existing serialized-name merge join them on
	// purpose. See context/DESIGN_XLANG_BOUNDARIES.md.
	ABI,
	// The reserved namespace for schema-mediated contract atoms: one declaration
	// in an interface schema (a FlatBuffers table) that a generator mirrors into
	// several languages. Keyed by (schema file, type), which is the only spelling
	// every generator agrees on -- namespaces and suffixes differ per language.
	SCHEMA,
	// The reserved namespace for channel-mediated contract atoms: an IPC channel
	// named by a string constant that several languages must spell identically.
	// Keyed by the literal itself, because the literal is the only thing the four
	// declarations have in common -- their own names differ per language
	// convention (s_memoryNamePrefix / MEM_PREFIX / mem_prefix / memoryNamePrefix).
	CHANNEL
};

SRCTRL_EXPORT std::string nameDelimiterTypeToString(NameDelimiterType delimiter);
SRCTRL_EXPORT NameDelimiterType stringToNameDelimiterType(const std::string& s);

SRCTRL_EXPORT NameDelimiterType detectDelimiterType(const std::string& name);

#include "NameDelimiterType.inl"

#endif	  // NAME_DELIMITER_TYPE_H
