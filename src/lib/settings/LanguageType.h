#ifndef LANGUAGE_TYPE_H
#define LANGUAGE_TYPE_H

#include "SourceGroupType.h"

#include <QMetaType>

#include <string>

enum class LanguageType
{
	UNKNOWN,
	CXX,
	C,
	RUST,
	SWIFT,
	CUSTOM
};

Q_DECLARE_METATYPE(LanguageType)

std::string languageTypeToString(LanguageType t);

LanguageType getLanguageTypeForSourceGroupType(SourceGroupType t);

#endif
