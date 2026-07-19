#ifndef ERROR_INFO_H
#define ERROR_INFO_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "StorageError.h"
#endif


SRCTRL_EXPORT struct ErrorInfo
{
	ErrorInfo()
		: id(0)
		, 
		 lineNumber(-1)
		, columnNumber(-1)
		, 
		 fatal(false)
		, indexed(false)
	{
	}

	ErrorInfo(
		Id id,
		std::string message,
		std::string filePath,
		std::size_t lineNumber,
		std::size_t columnNumber,
		std::string translationUnit,
		bool fatal,
		bool indexed)
		: id(id)
		, message(std::move(message))
		, filePath(std::move(filePath))
		, lineNumber(lineNumber)
		, columnNumber(columnNumber)
		, translationUnit(std::move(translationUnit))
		, fatal(fatal)
		, indexed(indexed)
	{
	}

	Id id;

	std::string message;

	std::string filePath;
	std::size_t lineNumber;
	std::size_t columnNumber;

	std::string translationUnit;
	bool fatal;
	bool indexed;
};

#endif	  // ERROR_INFO_H
