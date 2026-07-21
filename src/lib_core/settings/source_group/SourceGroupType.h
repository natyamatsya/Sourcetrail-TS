#ifndef SOURCE_GROUP_TYPE_H
#define SOURCE_GROUP_TYPE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "language_package_flags.h"

#include <QMetaType>

#include <string>
#endif

SRCTRL_EXPORT enum class SourceGroupType
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

// Q_DECLARE_METATYPE is a macro and can't cross an import (srctrl.qt:meta precedent): classic
// includers keep it textual; the module purview strips it (module content never marshals this
// enum through QVariant itself).
#ifndef SRCTRL_MODULE_PURVIEW
Q_DECLARE_METATYPE(SourceGroupType)
#endif

SRCTRL_EXPORT std::string sourceGroupTypeToString(SourceGroupType v);
SRCTRL_EXPORT std::string sourceGroupTypeToProjectSetupString(SourceGroupType v);
SRCTRL_EXPORT SourceGroupType stringToSourceGroupType(const std::string& v);

#include "SourceGroupType.inl"

#endif	  // SOURCE_GROUP_TYPE_H
