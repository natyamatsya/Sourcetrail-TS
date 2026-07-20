#ifndef SOURCE_GROUP_TYPE_H
#define SOURCE_GROUP_TYPE_H

#include "language_package_flags.h"

#include <QMetaType>

#include <string>

enum class SourceGroupType
{
	UNKNOWN,
	C_EMPTY,
	CXX_EMPTY,
	CXX_CDB,
	CXX_CMAKE_FILE_API,
	RUST_EMPTY,
	SWIFT_EMPTY,
	ZIG_EMPTY,
	CUSTOM_COMMAND
};

Q_DECLARE_METATYPE(SourceGroupType)

std::string sourceGroupTypeToString(SourceGroupType v);
std::string sourceGroupTypeToProjectSetupString(SourceGroupType v);
SourceGroupType stringToSourceGroupType(const std::string& v);

#endif	  // SOURCE_GROUP_TYPE_H
