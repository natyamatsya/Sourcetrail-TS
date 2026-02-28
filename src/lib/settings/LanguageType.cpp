#include "LanguageType.h"

std::string languageTypeToString(LanguageType t)
{
	switch (t)
	{
#if BUILD_CXX_LANGUAGE_PACKAGE
	case LanguageType::C:
		return "C";
	case LanguageType::CXX:
		return "C++";
#endif	  // BUILD_CXX_LANGUAGE_PACKAGE
#if BUILD_RUST_LANGUAGE_PACKAGE
	case LanguageType::RUST:
		return "Rust";
#endif	  // BUILD_RUST_LANGUAGE_PACKAGE
#if BUILD_SWIFT_LANGUAGE_PACKAGE
	case LanguageType::SWIFT:
		return "Swift";
#endif	  // BUILD_SWIFT_LANGUAGE_PACKAGE
	case LanguageType::CUSTOM:
		return "Custom";
	case LanguageType::UNKNOWN:
		break;
	}
	return "unknown";
}

LanguageType getLanguageTypeForSourceGroupType(SourceGroupType t)
{
	switch (t)
	{
#if BUILD_CXX_LANGUAGE_PACKAGE
	case SourceGroupType::C_EMPTY:
		return LanguageType::C;
	case SourceGroupType::CXX_EMPTY:
		return LanguageType::CXX;
	case SourceGroupType::CXX_CDB:
	case SourceGroupType::CXX_CMAKE_FILE_API:
		return LanguageType::CXX;
#endif	  // BUILD_CXX_LANGUAGE_PACKAGE
#if BUILD_RUST_LANGUAGE_PACKAGE
	case SourceGroupType::RUST_EMPTY:
		return LanguageType::RUST;
#endif	  // BUILD_RUST_LANGUAGE_PACKAGE
#if BUILD_SWIFT_LANGUAGE_PACKAGE
	case SourceGroupType::SWIFT_EMPTY:
		return LanguageType::SWIFT;
#endif	  // BUILD_SWIFT_LANGUAGE_PACKAGE
	case SourceGroupType::CUSTOM_COMMAND:
		return LanguageType::CUSTOM;
	default:
		break;
	}

	return LanguageType::UNKNOWN;
}
