#ifndef LANGUAGE_TYPE_H
#define LANGUAGE_TYPE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupType.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <QMetaType>

#include <string>
#endif

SRCTRL_EXPORT enum class LanguageType
{
	UNKNOWN,
	CXX,
	C,
	RUST,
	SWIFT,
	ZIG,
	CUSTOM
};

// Macro, can't cross an import (srctrl.qt:meta precedent): classic includers keep it textual.
#ifndef SRCTRL_MODULE_PURVIEW
Q_DECLARE_METATYPE(LanguageType)
#endif

SRCTRL_EXPORT std::string languageTypeToString(LanguageType t);

SRCTRL_EXPORT LanguageType getLanguageTypeForSourceGroupType(SourceGroupType t);

#include "LanguageType.inl"

#endif
