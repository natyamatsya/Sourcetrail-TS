#include "SourceGroupFields.h"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <ranges>

#include "FilePath.h"
#include "SourceGroupSettings.h"
#include "SourceGroupSettingsWithCStandard.h"
#include "SourceGroupSettingsWithCargoOptions.h"
#include "SourceGroupSettingsWithCppStandard.h"
#include "SourceGroupSettingsWithCustomCommand.h"
#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"
#include "SourceGroupSettingsWithCxxCdbPath.h"
#include "SourceGroupSettingsWithCxxPathsAndFlags.h"
#include "SourceGroupSettingsWithExcludeFilters.h"
#include "SourceGroupSettingsWithIndexedHeaderPaths.h"
#include "SourceGroupSettingsWithSourceExtensions.h"
#include "SourceGroupSettingsWithSourcePaths.h"
#include "SourceGroupSettingsWithSwiftOptions.h"

namespace
{
using Strings = std::vector<std::string>;

Strings toStrings(const std::vector<FilePath>& paths)
{
	Strings strings;
	for (const FilePath& path: paths)
	{
		strings.push_back(path.str());
	}
	return strings;
}

std::vector<FilePath> toPaths(const Strings& strings)
{
	std::vector<FilePath> paths;
	for (const std::string& string: strings)
	{
		paths.emplace_back(string);
	}
	return paths;
}

// A field: whether a group has it, its value, and how it is set (from a value of
// the field's kind; the kind is checked before).
struct Field
{
	std::string_view name;
	std::size_t kind;	// the SourceGroupField alternative it takes
	std::function<bool(const SourceGroupSettings&)> has;
	std::function<SourceGroupField(const SourceGroupSettings&)> read;
	std::function<void(SourceGroupSettings&, const SourceGroupField&)> write;
};

template <typename T>
constexpr std::size_t kindOf = std::is_same_v<T, bool> ? 0 : std::is_same_v<T, std::string> ? 1 : 2;

// A field of `Component`: `get(component)` gives its value as a T, `set(component, value)`
// sets it.
template <typename Component, typename T, typename Get, typename Set>
Field field(std::string_view name, Get get, Set set)
{
	return {
		name,
		kindOf<T>,
		[](const SourceGroupSettings& settings) { return dynamic_cast<const Component*>(&settings) != nullptr; },
		[get](const SourceGroupSettings& settings) -> SourceGroupField {
			return T(get(dynamic_cast<const Component&>(settings)));
		},
		[set](SourceGroupSettings& settings, const SourceGroupField& value) {
			set(dynamic_cast<Component&>(settings), std::get<T>(value));
		}};
}

template <typename Component, typename Get, typename Set>
Field pathsField(std::string_view name, Get get, Set set)
{
	return field<Component, Strings>(
		name,
		[get](const Component& component) { return toStrings(get(component)); },
		[set](Component& component, const Strings& value) { set(component, toPaths(value)); });
}

template <typename Component, typename Get, typename Set>
Field pathField(std::string_view name, Get get, Set set)
{
	return field<Component, std::string>(
		name,
		[get](const Component& component) { return get(component).str(); },
		[set](Component& component, const std::string& value) { set(component, FilePath(value)); });
}

const std::vector<Field>& fields()
{
	using G = SourceGroupSettings;
	using Paths = SourceGroupSettingsWithSourcePaths;
	using Exclude = SourceGroupSettingsWithExcludeFilters;
	using Extensions = SourceGroupSettingsWithSourceExtensions;
	using Cdb = SourceGroupSettingsWithCxxCdbPath;
	using Flags = SourceGroupSettingsWithCxxPathsAndFlags;
	using Headers = SourceGroupSettingsWithIndexedHeaderPaths;
	using Cpp = SourceGroupSettingsWithCppStandard;
	using C = SourceGroupSettingsWithCStandard;
	using CMake = SourceGroupSettingsWithCxxCMakeBuildDirectory;
	using Command = SourceGroupSettingsWithCustomCommand;
	using Cargo = SourceGroupSettingsWithCargoOptions;
	using Swift = SourceGroupSettingsWithSwiftOptions;

	static const std::vector<Field> fields {
		field<G, std::string>(
			"name", [](const G& g) { return g.getName(); }, [](G& g, const std::string& v) { g.setName(v); }),
		field<G, bool>(
			"enabled",
			[](const G& g) { return g.getStatus() == SourceGroupStatusType::ENABLED; },
			[](G& g, bool v) { g.setStatus(v ? SourceGroupStatusType::ENABLED : SourceGroupStatusType::DISABLED); }),
		pathsField<Paths>(
			"source_paths", [](const Paths& c) { return c.getSourcePaths(); }, [](Paths& c, const auto& v) { c.setSourcePaths(v); }),
		field<Exclude, Strings>(
			"exclude_filters",
			[](const Exclude& c) { return c.getExcludeFilterStrings(); },
			[](Exclude& c, const Strings& v) { c.setExcludeFilterStrings(v); }),
		field<Extensions, Strings>(
			"extensions",
			[](const Extensions& c) { return c.getSourceExtensions(); },
			[](Extensions& c, const Strings& v) { c.setSourceExtensions(v); }),
		pathField<Cdb>(
			"compilation_database",
			[](const Cdb& c) { return c.getCompilationDatabasePath(); },
			[](Cdb& c, const FilePath& v) { c.setCompilationDatabasePath(v); }),
		pathField<CMake>(
			"cmake_source_directory",
			[](const CMake& c) { return c.getSourceDirectory(); },
			[](CMake& c, const FilePath& v) { c.setSourceDirectory(v); }),
		field<CMake, std::string>(
			"cmake_preset",
			[](const CMake& c) { return c.getPresetName(); },
			[](CMake& c, const std::string& v) { c.setPresetName(v); }),
		pathField<CMake>(
			"cmake_build_directory",
			[](const CMake& c) { return c.getBuildDirectory(); },
			[](CMake& c, const FilePath& v) { c.setBuildDirectory(v); }),
		field<CMake, std::string>(
			"cmake_target_glob",
			[](const CMake& c) { return c.getTargetGlob(); },
			[](CMake& c, const std::string& v) { c.setTargetGlob(v); }),
		field<CMake, Strings>(
			"cmake_arguments",
			[](const CMake& c) { return c.getCMakeArguments(); },
			[](CMake& c, const Strings& v) { c.setCMakeArguments(v); }),
		field<CMake, bool>(
			"cmake_index_linked_targets",
			[](const CMake& c) { return c.getIndexLinkedTargets(); },
			[](CMake& c, bool v) { c.setIndexLinkedTargets(v); }),
		field<CMake, std::string>(
			"cmake_configuration",
			[](const CMake& c) { return c.getConfiguration(); },
			[](CMake& c, const std::string& v) { c.setConfiguration(v); }),
		pathsField<Flags>(
			"header_search_paths",
			[](const Flags& c) { return c.getHeaderSearchPaths(); },
			[](Flags& c, const auto& v) { c.setHeaderSearchPaths(v); }),
		pathsField<Flags>(
			"framework_search_paths",
			[](const Flags& c) { return c.getFrameworkSearchPaths(); },
			[](Flags& c, const auto& v) { c.setFrameworkSearchPaths(v); }),
		field<Flags, Strings>(
			"compiler_flags",
			[](const Flags& c) { return c.getCompilerFlags(); },
			[](Flags& c, const Strings& v) { c.setCompilerFlags(v); }),
		pathsField<Headers>(
			"indexed_header_paths",
			[](const Headers& c) { return c.getIndexedHeaderPaths(); },
			[](Headers& c, const auto& v) { c.setIndexedHeaderPaths(v); }),
		field<Cpp, std::string>(
			"cpp_standard",
			[](const Cpp& c) { return c.getCppStandard(); },
			[](Cpp& c, const std::string& v) { c.setCppStandard(v); }),
		field<C, std::string>(
			"c_standard", [](const C& c) { return c.getCStandard(); }, [](C& c, const std::string& v) { c.setCStandard(v); }),
		field<Command, std::string>(
			"custom_command",
			[](const Command& c) { return c.getCustomCommand(); },
			[](Command& c, const std::string& v) { c.setCustomCommand(v); }),
		field<Command, bool>(
			"run_in_parallel",
			[](const Command& c) { return c.getRunInParallel(); },
			[](Command& c, bool v) { c.setRunInParallel(v); }),
		pathField<Cargo>(
			"cargo_workspace",
			[](const Cargo& c) { return c.getCargoWorkspaceDirectory(); },
			[](Cargo& c, const FilePath& v) { c.setCargoWorkspaceDirectory(v); }),
		field<Cargo, Strings>(
			"cargo_features",
			[](const Cargo& c) { return c.getCargoFeatures(); },
			[](Cargo& c, const Strings& v) { c.setCargoFeatures(v); }),
		field<Cargo, bool>(
			"cargo_all_features",
			[](const Cargo& c) { return c.getCargoAllFeatures(); },
			[](Cargo& c, bool v) { c.setCargoAllFeatures(v); }),
		field<Cargo, bool>(
			"cargo_no_default_features",
			[](const Cargo& c) { return c.getCargoNoDefaultFeatures(); },
			[](Cargo& c, bool v) { c.setCargoNoDefaultFeatures(v); }),
		field<Cargo, std::string>(
			"cargo_target_triple",
			[](const Cargo& c) { return c.getCargoTargetTriple(); },
			[](Cargo& c, const std::string& v) { c.setCargoTargetTriple(v); }),
		field<Cargo, std::string>(
			"rust_specialization_scope",
			[](const Cargo& c) { return c.getRustSpecializationScope(); },
			[](Cargo& c, const std::string& v) { c.setRustSpecializationScope(v); }),
		pathField<Swift>(
			"swift_toolchain",
			[](const Swift& c) { return c.getSwiftToolchainPath(); },
			[](Swift& c, const FilePath& v) { c.setSwiftToolchainPath(v); }),
		pathField<Swift>(
			"swift_index_store",
			[](const Swift& c) { return c.getSwiftIndexStorePath(); },
			[](Swift& c, const FilePath& v) { c.setSwiftIndexStorePath(v); }),
		field<Swift, Strings>(
			"swift_build_args",
			[](const Swift& c) { return c.getSwiftBuildArgs(); },
			[](Swift& c, const Strings& v) { c.setSwiftBuildArgs(v); }),
	};
	return fields;
}

constexpr std::array kindNames {"true or false", "a string", "a list of strings"};

constexpr std::array typeIds {
	std::pair {SourceGroupType::C_EMPTY, std::string_view("c")},
	std::pair {SourceGroupType::CXX_EMPTY, std::string_view("cpp")},
	std::pair {SourceGroupType::CXX_CDB, std::string_view("cdb")},
	std::pair {SourceGroupType::CXX_CMAKE_FILE_API, std::string_view("cmake")},
	std::pair {SourceGroupType::RUST_EMPTY, std::string_view("rust")},
	std::pair {SourceGroupType::SWIFT_EMPTY, std::string_view("swift")},
	std::pair {SourceGroupType::ZIG_EMPTY, std::string_view("zig")},
	std::pair {SourceGroupType::CUSTOM_COMMAND, std::string_view("custom")},
};
}	 // namespace

std::map<std::string, SourceGroupField> readSourceGroupFields(const SourceGroupSettings& settings)
{
	std::map<std::string, SourceGroupField> values;
	for (const Field& field: fields())
	{
		if (field.has(settings))
		{
			values.emplace(field.name, field.read(settings));
		}
	}
	return values;
}

std::expected<void, std::string> writeSourceGroupField(
	SourceGroupSettings& settings, const std::string& name, const SourceGroupField& value)
{
	const auto it = std::ranges::find(fields(), name, &Field::name);
	if (it == fields().end() || !it->has(settings))
	{
		std::string names;
		for (const Field& field: fields())
		{
			if (field.has(settings))
			{
				names += (names.empty() ? "" : ", ") + std::string(field.name);
			}
		}
		return std::unexpected(std::format("'{}' is not a field of this source group (it has {})", name, names));
	}
	if (value.index() != it->kind)
	{
		return std::unexpected(std::format("'{}' takes {}", name, kindNames[it->kind]));
	}
	it->write(settings, value);
	return {};
}

std::string_view sourceGroupTypeId(SourceGroupType type)
{
	const auto it = std::ranges::find(typeIds, type, &std::pair<SourceGroupType, std::string_view>::first);
	return it != typeIds.end() ? it->second : std::string_view();
}

SourceGroupType sourceGroupTypeForId(std::string_view id)
{
	const auto it = std::ranges::find(typeIds, id, &std::pair<SourceGroupType, std::string_view>::second);
	return it != typeIds.end() ? it->first : SourceGroupType::UNKNOWN;
}
