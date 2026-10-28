#include "ProjectDetector.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <regex>
#include <set>
#include <system_error>

#include <QStandardPaths>
#include <QString>

#include "CMakeFileAPIReader.h"
#include "FilePath.h"
#include "ToolChain.h"

namespace
{
namespace fs = std::filesystem;

using Strings = std::vector<std::string>;

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::string generic(const fs::path& path)
{
	return path.generic_string();
}

bool isDirectory(const fs::path& path)
{
	std::error_code error;
	return fs::is_directory(path, error);
}

bool isFile(const fs::path& path)
{
	std::error_code error;
	return fs::is_regular_file(path, error);
}

// The path with symlinks resolved where it exists, so paths compare as the same folder.
fs::path resolvedPath(const fs::path& path)
{
	std::error_code error;
	const fs::path resolved{fs::weakly_canonical(path, error)};
	return error ? path.lexically_normal() : resolved;
}

// The beginning of a file: what detection reads of it.
std::string readText(const fs::path& path, std::size_t maxBytes = 256 * 1024)
{
	std::ifstream stream{path, std::ios::binary};
	if (!stream)
	{
		return {};
	}
	std::string text(maxBytes, '\0');
	stream.read(text.data(), static_cast<std::streamsize>(maxBytes));
	text.resize(static_cast<std::size_t>(stream.gcount()));
	return text;
}

std::optional<fs::file_time_type> modified(const fs::path& path)
{
	std::error_code error;
	const fs::file_time_type time{fs::last_write_time(path, error)};
	return error ? std::nullopt : std::optional{time};
}

// The directories in `dir`, by name.
std::vector<fs::path> subdirectories(const fs::path& dir)
{
	std::vector<fs::path> result;
	std::error_code error;
	for (fs::directory_iterator it{dir, fs::directory_options::skip_permission_denied, error}, end;
		 !error && it != end;
		 it.increment(error))
	{
		std::error_code typeError;
		if (it->is_directory(typeError))
		{
			result.push_back(it->path());
		}
	}
	std::ranges::sort(result);
	return result;
}

bool isEmptyDirectory(const fs::path& dir)
{
	std::error_code error;
	return fs::is_empty(dir, error) && !error;
}

std::string lower(std::string text)
{
	std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string trim(std::string_view text)
{
	const auto first{text.find_first_not_of(" \t\r\n")};
	if (first == std::string_view::npos)
	{
		return {};
	}
	const auto last{text.find_last_not_of(" \t\r\n")};
	return std::string{text.substr(first, last - first + 1)};
}

// "a, b and c"
std::string listed(const Strings& items)
{
	std::string text;
	for (std::size_t i = 0; i < items.size(); i++)
	{
		text += (i == 0 ? "" : i + 1 == items.size() ? " and " : ", ") + items[i];
	}
	return text;
}

// `path` relative to `base` where it is inside it ("build/debug"), otherwise absolute.
std::string shown(const fs::path& path, const fs::path& base)
{
	const fs::path relative{path.lexically_relative(base)};
	if (!relative.empty() && *relative.begin() != "..")
	{
		return generic(relative);
	}
	return generic(path);
}

bool hasExecutableOnPath(const std::string& name)
{
	return !QStandardPaths::findExecutable(QString::fromStdString(name)).isEmpty();
}

// ---------------------------------------------------------------------------
// Directory kinds
// ---------------------------------------------------------------------------

// A build tree by its name: build, build-debug, cmake-build-release, out, _build ...
bool isBuildDirectoryName(const std::string& name)
{
	const std::string n{lower(name)};
	return n == "build" || n.starts_with("build-") || n.starts_with("build_") || n.starts_with("build.") ||
		n.starts_with("cmake-build-") || n == "out" || n == "_build" || n == "builddir" ||
		n.ends_with(".srctrl-build");
}

// Where projects keep the dependencies they ship along.
bool isVendorDirectoryName(const std::string& name)
{
	static constexpr std::array names{
		"third_party", "third-party", "thirdparty", "3rdparty", "3rd_party", "external", "extern",
		"externals", "vendor", "vendored", "deps", "contrib"};
	return std::ranges::contains(names, lower(name));
}

// A build tree by its content: CMake's, Meson's, Ninja's, or one with a compilation database.
bool looksConfigured(const fs::path& dir)
{
	return isFile(dir / "CMakeCache.txt") || isDirectory(dir / "meson-private") ||
		isFile(dir / "compile_commands.json") || isFile(dir / "build.ninja") || isDirectory(dir / "CMakeFiles");
}

// A build tree: one of the usual names for one (build, out, cmake-build-debug ...), or a
// build-* directory with a build in it (build-aux, Autotools' scripts, is none).
bool isBuildTree(const fs::path& dir)
{
	const std::string n{lower(dir.filename().string())};
	return n == "build" || n == "out" || n == "_build" || n == "builddir" || n.starts_with("cmake-build-") ||
		n.ends_with(".srctrl-build") || looksConfigured(dir);
}

// A project of its own: its license, its build files, its repository.
bool looksLikeProject(const fs::path& dir)
{
	static constexpr std::array markers{
		"CMakeLists.txt", "meson.build", "Makefile", "configure", "configure.ac", "BUILD", "BUILD.bazel",
		"LICENSE", "LICENSE.txt", "LICENSE.md", "COPYING", "README.md", ".git"};
	return std::ranges::any_of(markers, [&](const char* marker) {
		std::error_code error;
		return fs::exists(dir / marker, error);
	});
}

// ---------------------------------------------------------------------------
// Source scan
// ---------------------------------------------------------------------------

constexpr std::array cppSourceExtensions{".cpp", ".cxx", ".cc", ".c++", ".cppm", ".ixx", ".cxxm", ".ccm", ".c++m"};
constexpr std::array cppHeaderExtensions{".hpp", ".hh", ".hxx", ".h++", ".ipp", ".tpp"};

struct SourceScan
{
	std::map<std::string, std::size_t> extensions;	// lower case, with the dot
	std::vector<fs::path> samples;	// files whose content tells the language and standard
	std::vector<fs::path> headers;	// .h files, to tell C from C++
	bool truncated = false;
};

std::size_t countOf(const SourceScan& scan, std::string_view extension)
{
	const auto it{scan.extensions.find(std::string{extension})};
	return it == scan.extensions.end() ? 0 : it->second;
}

template <std::size_t N>
std::size_t countOf(const SourceScan& scan, const std::array<const char*, N>& extensions)
{
	std::size_t count{0};
	for (const char* extension: extensions)
	{
		count += countOf(scan, extension);
	}
	return count;
}

// The C and C++ files under `folder`, leaving out hidden directories and `skipped` ones.
SourceScan scanSources(const fs::path& folder, const std::set<fs::path>& skipped)
{
	constexpr std::size_t maxEntries{200'000};
	constexpr std::size_t maxSamples{300};

	SourceScan scan;
	std::size_t entries{0};
	std::error_code error;
	fs::recursive_directory_iterator it{folder, fs::directory_options::skip_permission_denied, error};
	for (const fs::recursive_directory_iterator end; !error && it != end; it.increment(error))
	{
		if (++entries > maxEntries)
		{
			scan.truncated = true;
			break;
		}
		const fs::path& path{it->path()};
		const std::string name{path.filename().string()};
		std::error_code typeError;
		if (it->is_directory(typeError))
		{
			if (name.starts_with('.') || skipped.contains(path) || name == "node_modules" || looksConfigured(path))
			{
				it.disable_recursion_pending();
			}
			continue;
		}
		const std::string extension{lower(path.extension().string())};
		const bool isSource{
			extension == ".c" || extension == ".h" || std::ranges::contains(cppSourceExtensions, extension) ||
			std::ranges::contains(cppHeaderExtensions, extension)};
		if (!isSource)
		{
			continue;
		}
		scan.extensions[extension]++;
		if (extension == ".h" && scan.headers.size() < maxSamples)
		{
			scan.headers.push_back(path);
		}
		if (extension != ".h" && extension != ".c" && scan.samples.size() < maxSamples)
		{
			scan.samples.push_back(path);
		}
	}
	return scan;
}

// Whether the .h files are C++: namespaces, templates, classes, the standard library.
std::optional<fs::path> cppHeaderAmong(const std::vector<fs::path>& headers)
{
	static const std::regex cpp{
		R"(\bnamespace\s+\w+\s*\{|\btemplate\s*<|\bclass\s+\w+\s*(final\s*)?[:{]|\bstd::\w|#\s*include\s*<(iostream|vector|string|memory|map|algorithm|utility)>)"};
	for (const fs::path& header: headers | std::views::take(60))
	{
		const std::string text{readText(header, 32 * 1024)};
		if (std::regex_search(text, cpp))
		{
			return header;
		}
	}
	return std::nullopt;
}

// ---------------------------------------------------------------------------
// Language standards
// ---------------------------------------------------------------------------

// The build files of a folder that name compiler flags.
std::vector<fs::path> buildFiles(const fs::path& folder)
{
	std::vector<fs::path> files;
	std::error_code error;
	for (fs::directory_iterator it{folder, fs::directory_options::skip_permission_denied, error}, end;
		 !error && it != end;
		 it.increment(error))
	{
		const std::string name{it->path().filename().string()};
		const std::string extension{lower(it->path().extension().string())};
		static constexpr std::array names{
			"Makefile", "makefile", "GNUmakefile", "meson.build", "meson_options.txt", "configure.ac",
			"configure.in", "SConstruct", "premake5.lua", "xmake.lua", "BUILD", "BUILD.bazel", ".bazelrc",
			"Makefile.am", "CMakeLists.txt"};
		if (std::ranges::contains(names, name) || extension == ".mk" || extension == ".pro")
		{
			files.push_back(it->path());
		}
	}
	std::ranges::sort(files);
	return files;
}

std::string cppStandardFromSuffix(const std::string& prefix, std::string suffix)
{
	static const std::map<std::string, std::string> aliases{
		{"0x", "11"}, {"1y", "14"}, {"1z", "17"}, {"2a", "20"}, {"2b", "23"}, {"2c", "26"}};
	if (const auto alias{aliases.find(suffix)}; alias != aliases.end())
	{
		suffix = alias->second;
	}
	return prefix + "++" + suffix;
}

std::string cStandardFromSuffix(const std::string& prefix, std::string suffix)
{
	static const std::map<std::string, std::string> aliases{
		{"90", "89"}, {"9x", "99"}, {"1x", "11"}, {"18", "17"}, {"2x", "23"}};
	if (const auto alias{aliases.find(suffix)}; alias != aliases.end())
	{
		suffix = alias->second;
	}
	return prefix + suffix;
}

struct Standard
{
	std::string value;
	std::string why;
};

// The C++ standard the build files ask for: -std=c++17, cpp_std=c++17,
// CMAKE_CXX_STANDARD 17, cxx_std_17, CONFIG += c++17.
std::optional<Standard> cppStandardFromBuildFiles(const fs::path& folder)
{
	static const std::regex flag{R"((?:std=|std\s*[:=]\s*['"]?)(c|gnu)\+\+(\d[0-9a-z]))"};
	static const std::regex cmake{R"((?:CMAKE_CXX_STANDARD\s+|cxx_std_)(\d\d))"};
	static const std::regex qmake{R"(CONFIG\s*\+?=.*\bc\+\+(\d[0-9a-z]))"};
	const Strings available{ClangCompiler::getAvailableCppStandards()};
	for (const fs::path& file: buildFiles(folder))
	{
		const std::string text{readText(file)};
		std::smatch match;
		std::optional<std::string> standard;
		if (std::regex_search(text, match, flag))
		{
			standard = cppStandardFromSuffix(match[1].str(), match[2].str());
		}
		else if (std::regex_search(text, match, cmake) || std::regex_search(text, match, qmake))
		{
			standard = cppStandardFromSuffix("c", match[1].str());
		}
		if (standard && std::ranges::contains(available, *standard))
		{
			return Standard{*standard, std::format("{} asks for it ({})", file.filename().string(), match[0].str())};
		}
	}
	return std::nullopt;
}

std::optional<Standard> cStandardFromBuildFiles(const fs::path& folder)
{
	static const std::regex flag{R"((?:std=|c_std\s*=\s*['"]?)(c|gnu)(\d[0-9x])\b)"};
	const Strings available{ClangCompiler::getAvailableCStandards()};
	for (const fs::path& file: buildFiles(folder))
	{
		const std::string text{readText(file)};
		std::smatch match;
		if (std::regex_search(text, match, flag))
		{
			const std::string standard{cStandardFromSuffix(match[1].str(), match[2].str())};
			if (std::ranges::contains(available, standard))
			{
				return Standard{standard, std::format("{} asks for it ({})", file.filename().string(), match[0].str())};
			}
		}
	}
	return std::nullopt;
}

// The newest C++ standard whose features the code uses.
std::optional<Standard> cppStandardFromCode(const std::vector<fs::path>& files, const fs::path& folder)
{
	struct Feature
	{
		int standard;
		const char* text;
	};
	static constexpr std::array features{
		Feature{23, "std::expected<"}, Feature{23, "#include <print>"}, Feature{23, "if consteval"},
		Feature{23, "std::mdspan"}, Feature{20, "co_await"}, Feature{20, "co_return"},
		Feature{20, "consteval "}, Feature{20, "constinit "}, Feature{20, "#include <concepts>"},
		Feature{20, "#include <ranges>"}, Feature{20, "std::span<"}, Feature{20, "<=>"},
		Feature{20, "import std;"}, Feature{17, "if constexpr"}, Feature{17, "std::optional<"},
		Feature{17, "std::variant<"}, Feature{17, "std::string_view"}, Feature{17, "[[nodiscard]]"},
		Feature{17, "[[maybe_unused]]"}, Feature{17, "std::filesystem"}, Feature{17, "inline constexpr"},
		Feature{14, "std::make_unique"}, Feature{14, "decltype(auto)"}, Feature{14, "[[deprecated"},
		Feature{11, "nullptr"}, Feature{11, "std::unique_ptr"}, Feature{11, "constexpr "},
		Feature{11, " override"}};

	int found{0};
	std::string why;
	for (const fs::path& file: files)
	{
		const std::string text{readText(file, 64 * 1024)};
		for (const Feature& feature: features)
		{
			if (feature.standard <= found)
			{
				break;	// newest first: the rest is no newer
			}
			if (text.contains(feature.text))
			{
				found = feature.standard;
				why = std::format("the code uses {} ({})", trim(feature.text), shown(file, folder));
				break;
			}
		}
	}
	if (found == 0)
	{
		return std::nullopt;
	}
	return Standard{std::format("c++{}", found), why};
}

// ---------------------------------------------------------------------------
// CMake
// ---------------------------------------------------------------------------

struct BuildDirectory
{
	fs::path path;
	bool hasReply = false;
	std::string generator;
	fs::file_time_type time;	// when it was last configured or built
};

// `dir` as CMake configured it for `sourceDir`: CMake configured and generated it (a failed
// configure leaves a CMakeCache.txt, too; CMakeFileAPIReader::isConfigured), and its
// CMakeCache.txt names that source directory, or it has a File API reply.
std::optional<BuildDirectory> configuredFor(const fs::path& dir, const fs::path& sourceDir)
{
	const fs::path cache{dir / "CMakeCache.txt"};
	const CMakeFileAPIReader reader{FilePath{generic(dir)}};
	if (!reader.isConfigured())
	{
		return std::nullopt;
	}
	const bool hasReply{reader.hasReply()};
	const bool hasCache{isFile(cache)};
	BuildDirectory build{.path = dir, .hasReply = hasReply, .generator = {}, .time = {}};
	if (hasCache)
	{
		static const std::regex home{R"(CMAKE_HOME_DIRECTORY:INTERNAL=([^\r\n]*))"};
		static const std::regex generator{R"(CMAKE_GENERATOR:INTERNAL=([^\r\n]*))"};
		const std::string text{readText(cache, 4 * 1024 * 1024)};
		std::smatch match;
		if (std::regex_search(text, match, home) && resolvedPath(fs::path{match[1].str()}) != resolvedPath(sourceDir))
		{
			return std::nullopt;	// another project's
		}
		if (std::regex_search(text, match, generator))
		{
			build.generator = match[1].str();
		}
	}
	// A build touches .ninja_log (Ninja) or the generated build files: the newest of them
	// tells which build directory is in use. A file that is not there does not count (not as
	// the clock's epoch: libstdc++'s is 2174, after any file's time).
	build.time = modified(dir).value_or(fs::file_time_type::min());
	for (const char* file: {"CMakeCache.txt", "build.ninja", ".ninja_log", ".ninja_deps", "Makefile", "compile_commands.json"})
	{
		if (const std::optional<fs::file_time_type> time{modified(dir / file)})
		{
			build.time = std::max(build.time, *time);
		}
	}
	return build;
}

// The build trees in `folder`: build*/, cmake-build-*/, out/ ..., the directories in them
// (build/debug), and out/build/*/ (Visual Studio's).
std::vector<fs::path> buildTreeCandidates(const fs::path& folder)
{
	std::vector<fs::path> candidates;
	for (const fs::path& dir: subdirectories(folder))
	{
		if (!isBuildDirectoryName(dir.filename().string()))
		{
			continue;
		}
		candidates.push_back(dir);
		for (const fs::path& inner: subdirectories(dir))
		{
			if (inner.filename().string().starts_with('.') || inner.filename() == "CMakeFiles")
			{
				continue;
			}
			candidates.push_back(inner);
			if (inner.filename() == "build")
			{
				for (const fs::path& preset: subdirectories(inner))
				{
					candidates.push_back(preset);
				}
			}
		}
	}
	return candidates;
}

// The configured build directory to use: one with a File API reply first, then the newest.
std::optional<BuildDirectory> configuredBuildDirectory(const std::vector<fs::path>& candidates, const fs::path& sourceDir)
{
	std::optional<BuildDirectory> best;
	for (const fs::path& candidate: candidates)
	{
		const std::optional<BuildDirectory> build{configuredFor(candidate, sourceDir)};
		if (build &&
			(!best || std::pair{build->hasReply, build->time} > std::pair{best->hasReply, best->time}))
		{
			best = build;
		}
	}
	return best;
}

#if defined(_WIN32)
constexpr bool isWindows{true};
#else
constexpr bool isWindows{false};
#endif

enum class Platform : std::uint8_t
{
	Windows,
	Mac,
	Linux,
};

#if defined(_WIN32)
constexpr Platform hostPlatform{Platform::Windows};
#elif defined(__APPLE__)
constexpr Platform hostPlatform{Platform::Mac};
#else
constexpr Platform hostPlatform{Platform::Linux};
#endif

std::string_view platformName(Platform platform)
{
	switch (platform)
	{
	case Platform::Windows:
		return "Windows";
	case Platform::Mac:
		return "macOS";
	case Platform::Linux:
		return "Linux";
	}
	return {};
}

// The platform a preset's name, display name or description says it is for, if any
// ("dev-win", "Windows preset for ...", "macos-release").
std::optional<Platform> presetPlatform(const CMakeFileAPIReader::PresetInfo& preset)
{
	static const std::regex token{R"([a-z0-9]+)"};
	const std::string text{lower(preset.name + " " + preset.displayName + " " + preset.description)};
	for (std::sregex_iterator it{text.begin(), text.end(), token}, end; it != end; ++it)
	{
		const std::string word{it->str()};
		static constexpr std::array windowsWords{"win", "windows", "win32", "win64", "msvc", "vs", "mingw", "clangcl"};
		static constexpr std::array macWords{"mac", "macos", "osx", "darwin", "apple", "xcode"};
		static constexpr std::array linuxWords{"linux", "unix", "ubuntu", "debian", "fedora"};
		if (std::ranges::contains(windowsWords, word))
		{
			return Platform::Windows;
		}
		if (std::ranges::contains(macWords, word))
		{
			return Platform::Mac;
		}
		if (std::ranges::contains(linuxWords, word))
		{
			return Platform::Linux;
		}
	}
	return std::nullopt;
}

// How well a preset fits this computer: 0 made for it, 1 for no platform in particular,
// 2 for a related one (Linux and macOS), 3 for another.
int platformFit(const CMakeFileAPIReader::PresetInfo& preset)
{
	const std::optional<Platform> platform{presetPlatform(preset)};
	if (!platform)
	{
		return 1;
	}
	if (*platform == hostPlatform)
	{
		return 0;
	}
	return *platform != Platform::Windows && hostPlatform != Platform::Windows ? 2 : 3;
}

// Why a CMake generator cannot run here; empty when it can (or is CMake's choice).
std::string generatorProblem(const std::string& generator, const std::function<bool(const std::string&)>& hasExecutable)
{
	if (generator.starts_with("Ninja") && !hasExecutable("ninja"))
	{
		return "its generator is Ninja, and ninja is not on PATH";
	}
	if (generator.starts_with("Visual Studio") && !isWindows)
	{
		return "its generator is Visual Studio, which needs Windows";
	}
	if (generator == "Xcode" && !hasExecutable("xcodebuild"))
	{
		return "its generator is Xcode, and xcodebuild is not on PATH";
	}
	if (generator == "Unix Makefiles" && !hasExecutable("make"))
	{
		return "its generator is Unix Makefiles, and make is not on PATH";
	}
	return {};
}

// ---------------------------------------------------------------------------
// Compilation databases
// ---------------------------------------------------------------------------

std::optional<fs::path> newestCompilationDatabase(const fs::path& folder, const std::vector<fs::path>& buildTrees)
{
	std::optional<fs::path> newest;
	std::optional<fs::file_time_type> newestTime;
	std::vector<fs::path> dirs{folder};
	dirs.insert(dirs.end(), buildTrees.begin(), buildTrees.end());
	for (const fs::path& dir: dirs)
	{
		const fs::path database{dir / "compile_commands.json"};
		const std::optional<fs::file_time_type> time{isFile(database) ? modified(database) : std::nullopt};
		if (time && (!newestTime || *time > *newestTime))
		{
			newest = database;
			newestTime = time;
		}
	}
	return newest;
}

// ---------------------------------------------------------------------------
// Git
// ---------------------------------------------------------------------------

// The .git of the repository `folder` is in: the nearest one up the tree.
std::optional<fs::path> findDotGit(const fs::path& folder)
{
	for (fs::path dir{folder}; !dir.empty(); dir = dir.parent_path())
	{
		std::error_code error;
		if (fs::exists(dir / ".git", error))
		{
			return dir / ".git";
		}
		if (dir == dir.root_path() || dir.parent_path() == dir)
		{
			break;
		}
	}
	return std::nullopt;
}

// ---------------------------------------------------------------------------
// The proposal
// ---------------------------------------------------------------------------

std::string projectNameFor(const fs::path& folder)
{
	std::string name{folder.filename().string()};
	if (name.empty())
	{
		name = "project";
	}
	std::ranges::replace_if(
		name,
		[](unsigned char c) { return !(std::isalnum(c) || c == '_' || c == '.' || c == '-'); },
		'_');
	if (name.starts_with('-'))
	{
		name.front() = '_';
	}
	return name;
}

struct Detection
{
	const fs::path folder;
	const fs::path location;
	const std::string name;
	const std::function<bool(const std::string&)>& hasExecutable;
	const std::optional<GitRepositoryInfo> git;
	const std::vector<GitSubmodule> submodules;	// the folder's, relative to it
	ProjectProposal proposal;
};

bool isSubmodule(const Detection& detection, const fs::path& dir)
{
	const std::string relative{generic(dir.lexically_relative(detection.folder))};
	return std::ranges::any_of(detection.submodules, [&](const GitSubmodule& submodule) {
		return submodule.path == relative || submodule.path.starts_with(relative + "/");
	});
}

// The vendored dependencies to leave out: directories named so (at the top or one level
// down) that hold other projects or submodules, unless `usedByBuild` says the build
// compiles them.
Strings vendoredExclusions(Detection& detection, const std::function<bool(const fs::path&)>& usedByBuild)
{
	std::vector<fs::path> vendorDirs;
	for (const fs::path& dir: subdirectories(detection.folder))
	{
		const std::string name{dir.filename().string()};
		if (isVendorDirectoryName(name))
		{
			vendorDirs.push_back(dir);
		}
		else if (!name.starts_with('.') && !isBuildTree(dir))
		{
			for (const fs::path& inner: subdirectories(dir))
			{
				if (isVendorDirectoryName(inner.filename().string()))
				{
					vendorDirs.push_back(inner);
				}
			}
		}
	}

	Strings excluded;
	for (const fs::path& dir: vendorDirs)
	{
		const std::string shownDir{shown(dir, detection.folder)};
		const std::vector<fs::path> inner{subdirectories(dir)};
		const bool holdsProjects{
			isSubmodule(detection, dir) ||
			std::ranges::any_of(inner, [&](const fs::path& project) {
				return looksLikeProject(project) || isSubmodule(detection, project);
			})};
		if (!holdsProjects || isEmptyDirectory(dir))
		{
			continue;
		}
		if (usedByBuild && usedByBuild(dir))
		{
			// Submodules not checked out are empty directories: nothing of them is indexed (the
			// submodule notice says how to get them).
			const bool checkedOut{
				std::ranges::any_of(inner, [](const fs::path& project) { return !isEmptyDirectory(project); })};
			detection.proposal.reasons.push_back(std::format(
				"{}/ holds dependencies the build compiles: {}",
				shownDir,
				checkedOut ? "indexed with the project" : "they are not checked out, so there is nothing of them to index"));
			continue;
		}
		excluded.push_back(generic(dir));
		detection.proposal.reasons.push_back(
			std::format("{}/ holds vendored dependencies: excluded (remove it from the excluded paths to index them)", shownDir));
	}
	return excluded;
}

void addExclusions(Detection& detection, const Strings& excluded)
{
	if (!excluded.empty())
	{
		detection.proposal.fields["exclude_filters"] = excluded;
	}
}

// What the build files of the folder say about how it is built, when nothing here reads
// them yet.
void noteUndetectedBuildSystems(Detection& detection)
{
	struct BuildSystem
	{
		std::vector<const char*> markers;
		const char* note;
	};
	static const std::array systems{
		BuildSystem{
			{"meson.build"},
			"Meson (meson.build) is not detected yet: 'meson setup builddir' writes "
			"builddir/compile_commands.json, which New Project then uses"},
		BuildSystem{
			{"MODULE.bazel", "WORKSPACE", "WORKSPACE.bazel"},
			"Bazel is not detected yet: a compile_commands.json from a Bazel compile commands "
			"extractor would be used"},
		BuildSystem{
			{"configure.ac", "configure.in"},
			"Autotools (configure.ac) is not detected yet: configure, then 'bear -- make' writes a "
			"compile_commands.json with the exact flags"},
		BuildSystem{
			{"Makefile", "makefile", "GNUmakefile"},
			"Makefile: its flags are not read; 'bear -- make' (or compiledb) writes a "
			"compile_commands.json with the exact ones"},
		BuildSystem{{"SConstruct"}, "SCons (SConstruct) is not detected yet"},
		BuildSystem{{"premake5.lua"}, "Premake (premake5.lua) is not detected yet"},
		BuildSystem{
			{"xmake.lua"},
			"xmake (xmake.lua) is not detected yet: 'xmake project -k compile_commands' writes a "
			"compilation database"},
		BuildSystem{
			{"BUILD.gn", ".gn"},
			"GN is not detected yet: 'gn gen out --export-compile-commands' writes a compilation database"},
	};
	for (const BuildSystem& system: systems)
	{
		const auto marker{std::ranges::find_if(system.markers, [&](const char* name) {
			std::error_code error;
			return fs::exists(detection.folder / name, error);
		})};
		if (marker != system.markers.end())
		{
			detection.proposal.notices.emplace_back(system.note);
		}
	}
}

// Other languages' projects in the folder: their source groups are added by hand.
void noteOtherLanguages(Detection& detection)
{
	struct Language
	{
		const char* marker;
		const char* note;
	};
	static constexpr std::array languages{
		Language{"Cargo.toml", "Cargo.toml: a Rust project, not detected yet (add a Rust source group)"},
		Language{"Package.swift", "Package.swift: a Swift package, not detected yet (add a Swift source group)"},
		Language{"build.zig", "build.zig: a Zig project, not detected yet"},
	};
	for (const Language& language: languages)
	{
		if (isFile(detection.folder / language.marker))
		{
			detection.proposal.notices.emplace_back(language.note);
		}
	}
}

void noteGit(Detection& detection)
{
	if (detection.git)
	{
		const GitRepositoryInfo& git{*detection.git};
		const std::string head{
			!git.branch.empty() ? std::format("branch {}", git.branch)
			: !git.commit.empty() ? std::format("detached at {}", git.commit)
								  : std::string{"no commit yet"}};
		const std::string kind{
			git.kind == GitRepositoryInfo::Kind::Worktree    ? "Git worktree"
			: git.kind == GitRepositoryInfo::Kind::Submodule ? "Git submodule"
															 : "Git repository"};
		const fs::path root{git.root};
		const std::string where{
			resolvedPath(root) == resolvedPath(detection.folder)
				? std::string{}
				: std::format(" (the folder is {} in it)", shown(detection.folder, root))};
		detection.proposal.notices.push_back(std::format("{} {}, {}{}", kind, git.name, head, where));
	}

	if (detection.submodules.empty())
	{
		return;
	}
	Strings paths;
	Strings missing;
	for (const GitSubmodule& submodule: detection.submodules)
	{
		paths.push_back(submodule.path);
		if (!submodule.checkedOut)
		{
			missing.push_back(submodule.path);
		}
	}
	constexpr std::size_t shownCount{5};
	std::string text{std::format(
		"{} git submodule{}: {}",
		paths.size(),
		paths.size() == 1 ? "" : "s",
		listed(Strings(paths.begin(), paths.begin() + std::min(paths.size(), shownCount))))};
	if (paths.size() > shownCount)
	{
		text += std::format(" and {} more", paths.size() - shownCount);
	}
	if (missing.empty())
	{
		text += paths.size() > 1 ? ", all checked out" : ", checked out";
	}
	else if (missing.size() == paths.size())
	{
		text += std::format(
			"; {} checked out ({} sources and headers are missing: 'git submodule update --init')",
			paths.size() == 1 ? "it is not" : "none is",
			paths.size() == 1 ? "its" : "their");
	}
	else
	{
		text += std::format(
			"; not checked out: {} ({} sources and headers are missing: 'git submodule update --init')",
			listed(missing),
			missing.size() == 1 ? "its" : "their");
	}
	detection.proposal.notices.push_back(text);
}

ProjectProposal& cmakeGroup(Detection& detection)
{
	ProjectProposal& proposal{detection.proposal};
	proposal.sourceGroupType = SourceGroupType::CXX_CMAKE_FILE_API;
	proposal.fields["cmake_source_directory"] = generic(detection.folder);
	proposal.fields["indexed_header_paths"] = Strings{generic(detection.folder)};
	return proposal;
}

// Whether a CMakeLists.txt of the folder (its own, or one level down) names `dir`.
bool cmakeUses(const fs::path& folder, const fs::path& dir)
{
	const std::string relative{generic(dir.lexically_relative(folder))};
	std::vector<fs::path> lists{folder / "CMakeLists.txt"};
	for (const fs::path& inner: subdirectories(folder))
	{
		if (inner != dir && !inner.filename().string().starts_with('.'))
		{
			lists.push_back(inner / "CMakeLists.txt");
		}
	}
	const std::string name{dir.filename().string()};
	return std::ranges::any_of(lists, [&](const fs::path& list) {
		const std::string text{readText(list, 1024 * 1024)};
		return text.contains(relative) || (list.parent_path() == dir.parent_path() && text.contains(name));
	});
}

bool detectCMakePreset(Detection& detection)
{
	const FilePath sourceDir{generic(detection.folder)};
	if (CMakeFileAPIReader::getPresetFiles(sourceDir).empty())
	{
		return false;
	}
	ProjectProposal& proposal{detection.proposal};
	const std::vector<CMakeFileAPIReader::PresetInfo> presets{CMakeFileAPIReader::discoverPresetInfos(sourceDir)};
	if (presets.empty())
	{
		proposal.reasons.emplace_back(
			"CMakePresets.json has no configure preset for this computer (all are hidden, or their "
			"condition is false here)");
		return false;
	}

	struct Usable
	{
		const CMakeFileAPIReader::PresetInfo* preset;
		fs::path binaryDir;
		std::optional<BuildDirectory> configured;
	};
	std::vector<Usable> usable;
	Strings skipped;
	for (const CMakeFileAPIReader::PresetInfo& preset: presets)
	{
		const FilePath binaryDir{CMakeFileAPIReader::resolveBinaryDir(sourceDir, preset.name)};
		std::string problem;
		if (binaryDir.empty())
		{
			problem = "it has no binaryDir";
		}
		else if (!preset.undefinedEnvironment.empty())
		{
			Strings variables;
			for (const std::string& variable: preset.undefinedEnvironment)
			{
				variables.push_back(std::format("${}{{{}}}", "env", variable));
			}
			problem = std::format(
				"it reads {}, which {} not set", listed(variables), variables.size() == 1 ? "is" : "are");
		}
		else
		{
			problem = generatorProblem(preset.generator, detection.hasExecutable);
		}
		if (!problem.empty())
		{
			skipped.push_back(std::format("'{}': {}", preset.name, problem));
			continue;
		}
		const fs::path binary{binaryDir.str()};
		usable.push_back({&preset, binary, configuredFor(binary, detection.folder)});
	}

	Strings passedOver;
	for (const std::string& skip: skipped)
	{
		passedOver.push_back(std::format("Skipped configure preset {}", skip));
	}
	if (usable.empty())
	{
		proposal.reasons.insert(proposal.reasons.end(), passedOver.begin(), passedOver.end());
		proposal.reasons.emplace_back("No configure preset is usable here: CMake is used without a preset");
		return false;
	}

	// A configured one first, the one built last among them; then the one that fits this
	// computer best; then the first. (Times are compared, not subtracted: libstdc++'s file
	// clock counts from 2174, so today's times are negative.)
	const auto before = [](const Usable& a, const Usable& b) {
		if (a.configured.has_value() != b.configured.has_value())
		{
			return a.configured.has_value();
		}
		if (a.configured && a.configured->time != b.configured->time)
		{
			return a.configured->time > b.configured->time;
		}
		return platformFit(*a.preset) < platformFit(*b.preset);
	};
	const Usable& chosen{*std::ranges::min_element(usable, before)};
	const auto configuredCount{std::ranges::count_if(usable, [](const Usable& u) { return u.configured.has_value(); })};
	const CMakeFileAPIReader::PresetInfo& preset{*chosen.preset};
	for (const Usable& other: usable)
	{
		if (&other == &chosen || other.configured || platformFit(*other.preset) <= platformFit(preset))
		{
			continue;
		}
		if (const std::optional<Platform> platform{presetPlatform(*other.preset)}; platform && *platform != hostPlatform)
		{
			passedOver.push_back(std::format(
				"Configure preset '{}' looks made for {}, and this is {}",
				other.preset->name,
				platformName(*platform),
				platformName(hostPlatform)));
		}
	}

	cmakeGroup(detection).fields["cmake_preset"] = preset.name;
	const std::string generator{preset.generator.empty() ? std::string{} : std::format(" ({})", preset.generator)};
	const std::string label{
		preset.displayName.empty() || preset.displayName == preset.name ? std::string{}
																		: std::format(" \"{}\"", preset.displayName)};
	proposal.reasons.insert(
		proposal.reasons.begin(),
		std::format(
			"Found {}: using configure preset '{}'{}{}",
			preset.file.empty() ? std::string{"CMakePresets.json"} : shown(fs::path{preset.file.str()}, detection.folder),
			preset.name,
			label,
			generator));
	if (chosen.configured)
	{
		const std::string which{
			configuredCount > 1 ? std::format(", built last of the {} configured", configuredCount) : std::string{}};
		proposal.reasons.push_back(
			chosen.configured->hasReply
				? std::format(
					  "Its build directory ({}{}) has a CMake File API reply: the source group's page lists its "
					  "targets",
					  shown(chosen.binaryDir, detection.folder),
					  which)
				: std::format(
					  "Its build directory ({}{}) is configured by CMake already: Sourcetrail asks CMake for its "
					  "targets when indexing, or with Run CMake on the source group's page",
					  shown(chosen.binaryDir, detection.folder),
					  which));
	}
	else
	{
		proposal.reasons.push_back(std::format(
			"CMake configures its build directory ({}) when indexing, or with Run CMake on the source "
			"group's page",
			shown(chosen.binaryDir, detection.folder)));
	}
	if (usable.size() > 1)
	{
		Strings others;
		for (const Usable& other: usable)
		{
			if (other.preset != chosen.preset)
			{
				others.push_back(std::format("'{}'", other.preset->name));
			}
		}
		proposal.reasons.push_back(std::format("Other presets to choose from: {}", listed(others)));
	}
	proposal.reasons.insert(proposal.reasons.end(), passedOver.begin(), passedOver.end());
	return true;
}

void detectCMakeBuildDirectory(Detection& detection)
{
	ProjectProposal& proposal{detection.proposal};
	std::vector<fs::path> candidates{buildTreeCandidates(detection.folder)};
	const fs::path ownBuild{detection.location / (detection.name + ".srctrl-build")};
	if (resolvedPath(detection.location) != resolvedPath(detection.folder))
	{
		candidates.push_back(ownBuild);
	}

	cmakeGroup(detection).fields["cmake_preset"] = std::string{};
	if (const std::optional<BuildDirectory> build{configuredBuildDirectory(candidates, detection.folder)})
	{
		proposal.fields["cmake_build_directory"] = generic(build->path);
		proposal.reasons.insert(
			proposal.reasons.begin(),
			std::format(
				"Found CMakeLists.txt and the build directory {} CMake configured for it{}",
				shown(build->path, detection.folder),
				build->generator.empty() ? std::string{} : std::format(" ({})", build->generator)));
		proposal.reasons.emplace_back(
			build->hasReply ? "It has a CMake File API reply: its targets are listed on the source group's page"
							: "Sourcetrail asks CMake for its File API reply when indexing");
		return;
	}

	proposal.fields["cmake_build_directory"] = generic(ownBuild);
	proposal.reasons.insert(
		proposal.reasons.begin(),
		std::format(
			"Found CMakeLists.txt without presets: CMake configures it for Sourcetrail in {}, beside the "
			"project file",
			ownBuild.filename().string()));
	proposal.reasons.emplace_back(
		"Indexing runs 'cmake -S <folder> -B <build directory>' while the build directory has no "
		"File API reply (or Run CMake on the source group's page, to pick targets first)");
}

bool detectCMake(Detection& detection)
{
	if (!isFile(detection.folder / "CMakeLists.txt"))
	{
		return false;
	}
	ProjectProposal& proposal{detection.proposal};
	const bool hasCMake{detection.hasExecutable("cmake")};
	if (!hasCMake)
	{
		const std::optional<fs::path> database{
			newestCompilationDatabase(detection.folder, buildTreeCandidates(detection.folder))};
		if (database)
		{
			proposal.reasons.emplace_back("cmake is not on PATH: the compilation database is used instead of CMake");
			return false;
		}
	}

	if (!detectCMakePreset(detection))
	{
		detectCMakeBuildDirectory(detection);
	}
	if (!hasCMake)
	{
		proposal.reasons.emplace_back(
			"cmake is not on PATH: indexing needs it to configure the project (start Sourcetrail where "
			"cmake is found)");
	}
	addExclusions(detection, vendoredExclusions(detection, [&](const fs::path& dir) {
		return cmakeUses(detection.folder, dir);
	}));
	return true;
}

bool detectCompilationDatabase(Detection& detection)
{
	const std::optional<fs::path> database{
		newestCompilationDatabase(detection.folder, buildTreeCandidates(detection.folder))};
	if (!database)
	{
		return false;
	}
	ProjectProposal& proposal{detection.proposal};
	proposal.sourceGroupType = SourceGroupType::CXX_CDB;
	proposal.fields["compilation_database"] = generic(*database);
	proposal.fields["indexed_header_paths"] = Strings{generic(detection.folder)};
	proposal.reasons.insert(
		proposal.reasons.begin(),
		std::format("Found the compilation database {}", shown(*database, detection.folder)));
	proposal.reasons.emplace_back("Headers under the folder are indexed along with the files it compiles");

	const std::string commands{readText(*database, 64 * 1024 * 1024)};
	addExclusions(detection, vendoredExclusions(detection, [&](const fs::path& dir) {
		return commands.contains(generic(dir) + "/");
	}));
	return true;
}

// Include directories: include/ (and inc/) up to three levels down, and what the Makefile
// passes with -I.
Strings includeDirectories(const fs::path& folder)
{
	Strings dirs;
	const auto add = [&](const fs::path& dir) {
		const std::string path{generic(resolvedPath(dir))};
		if (!std::ranges::contains(dirs, path))
		{
			dirs.push_back(path);
		}
	};

	// The folder itself when it holds headers: sources include them by name (stb, single-file
	// libraries).
	std::error_code error;
	for (fs::directory_iterator it{folder, fs::directory_options::skip_permission_denied, error}, end;
		 !error && it != end;
		 it.increment(error))
	{
		const std::string extension{lower(it->path().extension().string())};
		if (extension == ".h" || std::ranges::contains(cppHeaderExtensions, extension))
		{
			add(folder);
			break;
		}
	}

	std::vector<std::pair<fs::path, int>> pending{{folder, 0}};
	while (!pending.empty())
	{
		const auto [dir, depth]{pending.back()};
		pending.pop_back();
		for (const fs::path& inner: subdirectories(dir))
		{
			const std::string name{inner.filename().string()};
			if (name.starts_with('.') || isBuildTree(inner) || name == "node_modules")
			{
				continue;
			}
			if (name == "include" || name == "inc")
			{
				add(inner);
			}
			else if (depth < 2)
			{
				pending.emplace_back(inner, depth + 1);
			}
		}
	}

	static const std::regex includeFlag{R"((?:^|\s)-I\s*([^\s$()'"]+))"};
	for (const char* makefile: {"Makefile", "makefile", "GNUmakefile"})
	{
		const std::string text{readText(folder / makefile)};
		for (std::sregex_iterator it{text.begin(), text.end(), includeFlag}, end; it != end; ++it)
		{
			const fs::path dir{fs::path{(*it)[1].str()}.is_absolute() ? fs::path{(*it)[1].str()} : folder / (*it)[1].str()};
			if (isDirectory(dir))
			{
				add(dir);
			}
		}
	}
	std::ranges::sort(dirs);
	return dirs;
}

void detectEmptyGroup(Detection& detection)
{
	ProjectProposal& proposal{detection.proposal};

	// Build trees and vendored dependencies are left out, the latter as for the other groups.
	Strings excluded;
	std::set<fs::path> skipped;
	for (const fs::path& dir: subdirectories(detection.folder))
	{
		const std::string name{dir.filename().string()};
		if (!name.starts_with('.') && isBuildTree(dir))
		{
			excluded.push_back(generic(dir));
			skipped.insert(dir);
		}
	}
	if (!excluded.empty())
	{
		Strings names;
		for (const std::string& dir: excluded)
		{
			names.push_back(shown(fs::path{dir}, detection.folder) + "/");
		}
		proposal.reasons.push_back(std::format("Build directories excluded: {}", listed(names)));
	}
	for (const std::string& dir: vendoredExclusions(detection, {}))
	{
		excluded.push_back(dir);
		skipped.insert(fs::path{dir});
	}

	const SourceScan scan{scanSources(detection.folder, skipped)};
	const std::size_t cppSources{countOf(scan, cppSourceExtensions)};
	const std::size_t cppHeaders{countOf(scan, cppHeaderExtensions)};
	const std::size_t cSources{countOf(scan, ".c")};
	const std::size_t hHeaders{countOf(scan, ".h")};

	// The language most files are in; .h files alone are C++ when they read as C++.
	const std::optional<fs::path> cppHeader{
		cppSources == 0 && cppHeaders == 0 && cSources == 0 ? cppHeaderAmong(scan.headers) : std::nullopt};
	const bool cpp{(cppSources + cppHeaders > 0 && cppSources + cppHeaders >= cSources) || cppHeader.has_value()};
	proposal.sourceGroupType = cpp ? SourceGroupType::CXX_EMPTY : SourceGroupType::C_EMPTY;
	proposal.fields["source_paths"] = Strings{generic(detection.folder)};

	// What the group compiles: the source files' extensions; headers only for a header-only library.
	Strings extensions;
	if (cpp)
	{
		for (const char* extension: cppSourceExtensions)
		{
			if (countOf(scan, extension) > 0)
			{
				extensions.emplace_back(extension);
			}
		}
		if (extensions.empty())
		{
			for (const char* extension: {".hpp", ".hh", ".hxx", ".h"})
			{
				if (countOf(scan, extension) > 0)
				{
					extensions.emplace_back(extension);
				}
			}
		}
	}
	else
	{
		extensions.emplace_back(cSources > 0 || hHeaders == 0 ? ".c" : ".h");
	}
	proposal.fields["extensions"] = extensions;

	Strings counts;
	for (const auto& [count, what]: {
			 std::pair{cppSources, "C++ source"}, std::pair{cppHeaders, "C++ header"}, std::pair{cSources, ".c file"},
			 std::pair{hHeaders, ".h file"}})
	{
		if (count > 0)
		{
			counts.push_back(std::format("{} {}{}", count, what, count == 1 ? "" : "s"));
		}
	}
	const std::string files{
		counts.empty() ? std::string{"no C or C++ files"}
					   : listed(counts) + (scan.truncated ? " in the part of the folder looked at" : "")};

	// The build files found that are not read (yet): why the group is an empty one.
	// (By the names the folder has: on a case-insensitive file system Makefile is makefile too.)
	Strings buildFiles;
	{
		static constexpr std::array names{
			"Makefile", "makefile", "GNUmakefile", "meson.build", "configure.ac", "SConstruct", "BUILD.bazel",
			"BUILD.gn", "premake5.lua", "xmake.lua"};
		std::error_code error;
		for (fs::directory_iterator it{detection.folder, fs::directory_options::skip_permission_denied, error}, end;
			 !error && it != end;
			 it.increment(error))
		{
			const std::string name{it->path().filename().string()};
			if (std::ranges::contains(names, name))
			{
				buildFiles.push_back(name);
			}
		}
		std::ranges::sort(buildFiles);
	}
	proposal.reasons.insert(
		proposal.reasons.begin(),
		std::format(
			"{}: an empty {} group on the folder ({})",
			buildFiles.empty() ? std::string{"No build system found"}
							   : std::format(
									 "Found {}, which {} not read", listed(buildFiles), buildFiles.size() == 1 ? "is" : "are"),
			cpp ? "C++" : "C",
			files));
	if (cppHeader)
	{
		proposal.reasons.push_back(
			std::format("The .h files are C++ ({} uses C++)", shown(*cppHeader, detection.folder)));
	}
	if (cpp && cppSources == 0 && !extensions.empty())
	{
		proposal.reasons.push_back(std::format(
			"There are no C++ source files (a header-only library): its headers ({}) are indexed as "
			"source files",
			listed(extensions)));
	}
	if (!cpp && cppSources > 0)
	{
		proposal.reasons.push_back(std::format(
			"The {} C++ source file{} {} not in this C group: add a C++ source group for them",
			cppSources,
			cppSources == 1 ? "" : "s",
			cppSources == 1 ? "is" : "are"));
	}
	if (cpp && cSources > 0)
	{
		proposal.reasons.push_back(std::format(
			"The {} .c file{} {} not in this C++ group: add a C source group for them",
			cSources,
			cSources == 1 ? "" : "s",
			cSources == 1 ? "is" : "are"));
	}

	// The language standard: the build files' word, else what the code uses.
	if (cpp)
	{
		std::optional<Standard> standard{cppStandardFromBuildFiles(detection.folder)};
		if (!standard)
		{
			std::vector<fs::path> files{scan.samples};
			files.insert(files.end(), scan.headers.begin(), scan.headers.end());
			standard = cppStandardFromCode(files, detection.folder);
		}
		if (standard)
		{
			proposal.fields["cpp_standard"] = standard->value;
			proposal.reasons.push_back(std::format("C++ standard {}: {}", standard->value, standard->why));
		}
		else
		{
			proposal.reasons.push_back(std::format(
				"C++ standard: nothing tells, so the newest ({})", ClangCompiler::getLatestCppStandard()));
		}
	}
	else if (const std::optional<Standard> standard{cStandardFromBuildFiles(detection.folder)})
	{
		proposal.fields["c_standard"] = standard->value;
		proposal.reasons.push_back(std::format("C standard {}: {}", standard->value, standard->why));
	}

	const Strings includes{includeDirectories(detection.folder)};
	if (!includes.empty())
	{
		proposal.fields["header_search_paths"] = includes;
		Strings names;
		for (const std::string& dir: includes)
		{
			names.push_back(
				fs::path{dir} == detection.folder ? std::string{"the folder itself"} : shown(fs::path{dir}, detection.folder));
		}
		proposal.reasons.push_back(std::format("Include paths: {}", listed(names)));
	}

	addExclusions(detection, excluded);
	noteUndetectedBuildSystems(detection);
}
}	 // namespace

std::expected<ProjectProposal, ProjectDetectionError> detectProject(
	const std::string& folder, const ProjectDetectorOptions& options)
{
	std::error_code error;
	const fs::path path{fs::path{folder}};
	if (folder.empty() || !fs::exists(path, error))
	{
		return std::unexpected(ProjectDetectionError::FolderMissing);
	}
	if (!isDirectory(path))
	{
		return std::unexpected(ProjectDetectionError::NotAFolder);
	}

	const fs::path dir{resolvedPath(fs::absolute(path, error))};
	const fs::path location{
		options.projectLocation.empty() ? dir : resolvedPath(fs::absolute(fs::path{options.projectLocation}, error))};
	const std::function<bool(const std::string&)> hasExecutable{
		options.hasExecutable ? options.hasExecutable : std::function<bool(const std::string&)>{hasExecutableOnPath}};

	const std::optional<GitRepositoryInfo> git{detectGitRepository(generic(dir))};
	std::vector<GitSubmodule> submodules;
	if (git)
	{
		// The repository's submodules in the folder, relative to it.
		const fs::path root{git->root};
		for (const GitSubmodule& submodule: readGitSubmodules(git->root))
		{
			const fs::path relative{(root / submodule.path).lexically_relative(dir)};
			if (!relative.empty() && *relative.begin() != "..")
			{
				submodules.push_back({generic(relative), submodule.checkedOut});
			}
		}
	}

	Detection detection{
		.folder = dir,
		.location = location,
		.name = projectNameFor(dir),
		.hasExecutable = hasExecutable,
		.git = git,
		.submodules = submodules,
		.proposal = {}};
	detection.proposal.folder = generic(dir);
	detection.proposal.projectName = detection.name;
	detection.proposal.projectLocation = generic(location);

	if (!detectCMake(detection) && !detectCompilationDatabase(detection))
	{
		detectEmptyGroup(detection);
	}
	noteOtherLanguages(detection);
	noteGit(detection);
	return detection.proposal;
}

std::optional<GitRepositoryInfo> detectGitRepository(const std::string& folder)
{
	const std::optional<fs::path> dotGit{findDotGit(resolvedPath(fs::path{folder}))};
	if (!dotGit)
	{
		return std::nullopt;
	}

	GitRepositoryInfo info;
	info.root = generic(dotGit->parent_path());
	info.name = dotGit->parent_path().filename().string();

	// A .git file points to the repository's directory: a worktree's or a submodule's.
	fs::path gitDir{*dotGit};
	if (isFile(*dotGit))
	{
		const std::string text{readText(*dotGit, 4096)};
		static const std::regex pointer{R"(gitdir:\s*([^\r\n]+))"};
		std::smatch match;
		if (!std::regex_search(text, match, pointer))
		{
			return info;
		}
		const fs::path target{trim(match[1].str())};
		gitDir = resolvedPath(target.is_absolute() ? target : dotGit->parent_path() / target);
		const std::string targetPath{generic(gitDir)};
		if (targetPath.contains("/worktrees/"))
		{
			info.kind = GitRepositoryInfo::Kind::Worktree;
		}
		else if (targetPath.contains("/modules/"))
		{
			info.kind = GitRepositoryInfo::Kind::Submodule;
		}
	}

	const std::string head{trim(readText(gitDir / "HEAD", 4096))};
	if (head.starts_with("ref:"))
	{
		std::string ref{trim(std::string_view{head}.substr(4))};
		constexpr std::string_view heads{"refs/heads/"};
		info.branch = ref.starts_with(heads) ? ref.substr(heads.size()) : ref;
	}
	else if (!head.empty())
	{
		info.commit = head.substr(0, 12);
	}
	return info;
}

std::vector<GitSubmodule> readGitSubmodules(const std::string& root)
{
	const fs::path rootPath{root};
	std::vector<GitSubmodule> submodules;
	const std::string text{readText(rootPath / ".gitmodules", 1024 * 1024)};
	static const std::regex pathLine{R"((?:^|\n)\s*path\s*=\s*([^\r\n]+))"};
	for (std::sregex_iterator it{text.begin(), text.end(), pathLine}, end; it != end; ++it)
	{
		const std::string path{trim((*it)[1].str())};
		if (path.empty())
		{
			continue;
		}
		const fs::path dir{rootPath / path};
		submodules.push_back({path, isDirectory(dir) && !isEmptyDirectory(dir)});
	}
	return submodules;
}
