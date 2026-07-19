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
	CXX
};

SRCTRL_EXPORT std::string nameDelimiterTypeToString(NameDelimiterType delimiter);
SRCTRL_EXPORT NameDelimiterType stringToNameDelimiterType(const std::string& s);

SRCTRL_EXPORT NameDelimiterType detectDelimiterType(const std::string& name);

#include "NameDelimiterType.inl"

#endif	  // NAME_DELIMITER_TYPE_H
