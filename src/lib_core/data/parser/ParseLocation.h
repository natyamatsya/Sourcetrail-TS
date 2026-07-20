#ifndef PARSE_LOCATION_H
#define PARSE_LOCATION_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "FilePath.h"
#include "types.h"
#endif

SRCTRL_EXPORT enum class ParseLocationType
{
	TOKEN,
	SCOPE,
	SIGNATURE,
	QUALIFIER,
	LOCAL
};

SRCTRL_EXPORT struct ParseLocation
{
	ParseLocation();
	ParseLocation(Id fileId, size_t lineNumber, size_t columnNumber);
	ParseLocation(
		Id fileId,
		size_t startLineNumber,
		size_t startColumnNumber,
		size_t endLineNumber,
		size_t endColumnNumber);

	bool isValid() const;

	Id fileId;
	size_t startLineNumber;
	size_t startColumnNumber;
	size_t endLineNumber;
	size_t endColumnNumber;
};

#include "ParseLocation.inl"

#endif	  // PARSE_LOCATION_H
