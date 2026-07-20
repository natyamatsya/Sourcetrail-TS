// Inline implementations for ParseLocation.h. Included at the end of that header; not a standalone TU.

#pragma once

inline ParseLocation::ParseLocation()
	: fileId(0), startLineNumber(0), startColumnNumber(0), endLineNumber(0), endColumnNumber(0)
{
}

inline ParseLocation::ParseLocation(Id fileId, size_t lineNumber, size_t columnNumber)
	: fileId(fileId)
	, startLineNumber(lineNumber)
	, startColumnNumber(columnNumber)
	, endLineNumber(lineNumber)
	, endColumnNumber(columnNumber)
{
}

inline ParseLocation::ParseLocation(
	Id fileId,
	size_t startLineNumber,
	size_t startColumnNumber,
	size_t endLineNumber,
	size_t endColumnNumber)
	: fileId(fileId)
	, startLineNumber(startLineNumber)
	, startColumnNumber(startColumnNumber)
	, endLineNumber(endLineNumber)
	, endColumnNumber(endColumnNumber)
{
}

inline bool ParseLocation::isValid() const
{
	return fileId != 0;
}
