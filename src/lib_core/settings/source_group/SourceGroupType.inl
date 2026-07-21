// Inline implementations for SourceGroupType.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

inline std::string sourceGroupTypeToString(SourceGroupType v)
{
	switch (v)
	{
	case SourceGroupType::C_EMPTY:
		return "C Source Group";
	case SourceGroupType::CXX_EMPTY:
		return "C++ Source Group";
	case SourceGroupType::CXX_CDB:
		return "C/C++ from Compilation Database";
	case SourceGroupType::CXX_CMAKE_FILE_API:
		return "C/C++ from CMake File API";
	case SourceGroupType::RUST_EMPTY:
		return "Rust Empty";
	case SourceGroupType::SWIFT_EMPTY:
		return "Swift Empty";
	case SourceGroupType::ZIG_EMPTY:
		return "Zig Empty";
	case SourceGroupType::CUSTOM_COMMAND:
		// This is the serialization string stored in project files; upstream projects
		// use exactly this value, so it must not change. (That it matches the
		// project-setup string below is coincidence, not a requirement.)
		return "Custom Command Source Group";
	case SourceGroupType::UNKNOWN:
		break;
	}
	return "";
}

inline std::string sourceGroupTypeToProjectSetupString(SourceGroupType v)
{
	switch (v)
	{
	case SourceGroupType::C_EMPTY:
		return "Empty C Source Group";
	case SourceGroupType::CXX_EMPTY:
		return "Empty C++ Source Group";
	case SourceGroupType::CXX_CDB:
		return "C/C++ from Compilation Database";
	case SourceGroupType::CXX_CMAKE_FILE_API:
		return "C/C++ from CMake File API";
	case SourceGroupType::RUST_EMPTY:
		return "Empty Rust Source Group";
	case SourceGroupType::SWIFT_EMPTY:
		return "Empty Swift Source Group";
	case SourceGroupType::ZIG_EMPTY:
		return "Empty Zig Source Group";
	case SourceGroupType::CUSTOM_COMMAND:
		return "Custom Command Source Group";
	case SourceGroupType::UNKNOWN:
		break;
	}
	return "unknown";
}

inline SourceGroupType stringToSourceGroupType(const std::string& v)
{
	if (v == sourceGroupTypeToString(SourceGroupType::C_EMPTY))
		return SourceGroupType::C_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::CXX_EMPTY))
		return SourceGroupType::CXX_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::CXX_CDB))
		return SourceGroupType::CXX_CDB;
	if (v == sourceGroupTypeToString(SourceGroupType::CXX_CMAKE_FILE_API))
		return SourceGroupType::CXX_CMAKE_FILE_API;
	if (v == sourceGroupTypeToString(SourceGroupType::RUST_EMPTY))
		return SourceGroupType::RUST_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::SWIFT_EMPTY))
		return SourceGroupType::SWIFT_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::ZIG_EMPTY))
		return SourceGroupType::ZIG_EMPTY;
	if (v == sourceGroupTypeToString(SourceGroupType::CUSTOM_COMMAND))
		return SourceGroupType::CUSTOM_COMMAND;

	// Read-aliases for serialization strings that shipped briefly on this fork
	// (changed in 9f096c3fb6, reverted): projects saved in that window still load.
	if (v == "Custom Command")
		return SourceGroupType::CUSTOM_COMMAND;
	if (v == "Rust Source Group")
		return SourceGroupType::RUST_EMPTY;

	return SourceGroupType::UNKNOWN;
}
