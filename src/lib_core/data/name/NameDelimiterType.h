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
	ABI
};

SRCTRL_EXPORT std::string nameDelimiterTypeToString(NameDelimiterType delimiter);
SRCTRL_EXPORT NameDelimiterType stringToNameDelimiterType(const std::string& s);

SRCTRL_EXPORT NameDelimiterType detectDelimiterType(const std::string& name);

#include "NameDelimiterType.inl"

#endif	  // NAME_DELIMITER_TYPE_H
