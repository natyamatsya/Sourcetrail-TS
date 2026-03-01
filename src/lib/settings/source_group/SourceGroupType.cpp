#include "SourceGroupType.h"

std::string sourceGroupTypeToString(SourceGroupType v)
{
	switch (v)
	{
#if BUILD_CXX_LANGUAGE_PACKAGE
	case SourceGroupType::C_EMPTY:
		return "C Source Group";
	case SourceGroupType::CXX_EMPTY:
		return "C++ Source Group";
	case SourceGroupType::CXX_CDB:
		return "C/C++ from Compilation Database";
	case SourceGroupType::CXX_CMAKE_FILE_API:
		return "C/C++ from CMake File API";
#endif	  // BUILD_CXX_LANGUAGE_PACKAGE
	case SourceGroupType::RUST_EMPTY:
		return "Rust Empty";
	case SourceGroupType::SWIFT_EMPTY:
		return "Swift Empty";
	case SourceGroupType::CUSTOM_COMMAND:
		return "Custom Command";
	case SourceGroupType::UNKNOWN:
		break;
	}
	return "";
}

std::string sourceGroupTypeToProjectSetupString(SourceGroupType v)
{
	switch (v)
	{
#if BUILD_CXX_LANGUAGE_PACKAGE
	case SourceGroupType::C_EMPTY:
		return "Empty C Source Group";
	case SourceGroupType::CXX_EMPTY:
		return "Empty C++ Source Group";
	case SourceGroupType::CXX_CDB:
		return "C/C++ from Compilation Database";
	case SourceGroupType::CXX_CMAKE_FILE_API:
		return "C/C++ from CMake File API";
#endif	  // BUILD_CXX_LANGUAGE_PACKAGE
	case SourceGroupType::RUST_EMPTY:
		return "Empty Rust Source Group";
	case SourceGroupType::SWIFT_EMPTY:
		return "Empty Swift Source Group";
	case SourceGroupType::CUSTOM_COMMAND:
		return "Custom Command Source Group";
	case SourceGroupType::UNKNOWN:
		break;
	}
	return "unknown";
}

SourceGroupType stringToSourceGroupType(const std::string& v)
{
#if BUILD_CXX_LANGUAGE_PACKAGE
	if (v == sourceGroupTypeToString(SourceGroupType::C_EMPTY))
		return SourceGroupType::C_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::CXX_EMPTY))
		return SourceGroupType::CXX_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::CXX_CDB))
		return SourceGroupType::CXX_CDB;
	if (v == sourceGroupTypeToString(SourceGroupType::CXX_CMAKE_FILE_API))
		return SourceGroupType::CXX_CMAKE_FILE_API;
#endif	  // BUILD_CXX_LANGUAGE_PACKAGE
	if (v == sourceGroupTypeToString(SourceGroupType::RUST_EMPTY))
		return SourceGroupType::RUST_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::SWIFT_EMPTY))
		return SourceGroupType::SWIFT_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::CUSTOM_COMMAND))
		return SourceGroupType::CUSTOM_COMMAND;

	return SourceGroupType::UNKNOWN;
}
