// Inline implementations for LanguageType.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

inline std::string languageTypeToString(LanguageType t)
{
	switch (t)
	{
	case LanguageType::C:
		return "C";
	case LanguageType::CXX:
		return "C++";
	case LanguageType::RUST:
		return "Rust";
	case LanguageType::SWIFT:
		return "Swift";
	case LanguageType::ZIG:
		return "Zig";
	case LanguageType::CUSTOM:
		return "Custom";
	case LanguageType::UNKNOWN:
		break;
	}
	return "unknown";
}

inline LanguageType getLanguageTypeForSourceGroupType(SourceGroupType t)
{
	switch (t)
	{
	case SourceGroupType::C_EMPTY:
		return LanguageType::C;
	case SourceGroupType::CXX_EMPTY:
		return LanguageType::CXX;
	case SourceGroupType::CXX_CDB:
	case SourceGroupType::CXX_CMAKE_FILE_API:
		return LanguageType::CXX;
	case SourceGroupType::RUST_EMPTY:
		return LanguageType::RUST;
	case SourceGroupType::SWIFT_EMPTY:
		return LanguageType::SWIFT;
	case SourceGroupType::ZIG_EMPTY:
		return LanguageType::ZIG;
	case SourceGroupType::CUSTOM_COMMAND:
		return LanguageType::CUSTOM;
	default:
		break;
	}

	return LanguageType::UNKNOWN;
}
