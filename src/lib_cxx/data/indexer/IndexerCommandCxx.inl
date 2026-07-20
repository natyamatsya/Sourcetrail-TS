// Inline implementations for IndexerCommandCxx.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "MessageStatus.h"
#include "OrderedCache.h"
#include "Platform.h"
#include "ToolChain.h"
#include "logging.h"
#include "utilityApp.h"
#include "CdbLoad.h"
#include "utilityString.h"

#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>

#include <cstdint>
#include <mutex>
#endif

inline std::vector<FilePath> IndexerCommandCxx::getSourceFilesFromCDB(const FilePath& cdbPath)
{
	const std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, utility::CdbLoadError> cdb =
		utility::loadCDB(cdbPath);
	if (!cdb)
	{
		// A missing path is the normal "no compilation database here" case; only a parse failure is
		// worth surfacing to the user.
		if (cdb.error().code == utility::CdbLoadError::Code::ParseFailed)
		{
			const std::string message =
				"Loading Clang compilation database failed with error: \"" + cdb.error().message + "\"";
			LOG_ERROR(message);
			MessageStatus(message, true).dispatch();
		}
		return {};
	}

	return getSourceFilesFromCDB(cdb.value(), cdbPath);
}

inline std::vector<FilePath> IndexerCommandCxx::getSourceFilesFromCDB(std::shared_ptr<clang::tooling::CompilationDatabase> cdb, const FilePath& cdbPath)
{
	std::vector<FilePath> filePaths;
	if (cdb)
	{
		OrderedCache<FilePath, FilePath> canonicalDirectoryPathCache(
			[](const FilePath& path) { return path.getCanonical(); });

		for (const std::string& fileString: cdb->getAllFiles())
		{
			FilePath path = FilePath(fileString);
			if (!path.isAbsolute())
			{
				std::vector<clang::tooling::CompileCommand> commands = cdb->getCompileCommands(fileString);
				if (!commands.empty())
				{
					path = FilePath(commands.front().Directory + '/' + commands.front().Filename).makeCanonical();
				}
			}
			if (!path.isAbsolute())
			{
				path = cdbPath.getParentDirectory().getConcatenated(path).makeCanonical();
			}
			filePaths.push_back(canonicalDirectoryPathCache.getValue(path.getParentDirectory())
									.concatenate(path.fileName()));
		}
	}
	return filePaths;
}

inline std::string IndexerCommandCxx::getCompilerFlagLanguageStandard(const std::string& languageStandard)
{
	return ClangCompiler::stdOption(languageStandard);
}

inline const std::string& IndexerCommandCxx::getMacOSSysrootPath()
{
	static std::once_flag onceFlag;
	static std::string cachedSysroot;
	std::call_once(onceFlag, []() {
		if constexpr (utility::Platform::isMac())
		{
			const utility::ProcessOutput output = utility::executeProcess("xcrun", {"--show-sdk-path"});
			if (output.exitCode == 0 && !output.output.empty())
			{
				cachedSysroot = utility::trim(output.output);
			}
		}
	});
	return cachedSysroot;
}

inline std::vector<std::string> IndexerCommandCxx::getCompilerFlagsForSysroot(
	const std::vector<std::string>& existingFlags)
{
	if constexpr (!utility::Platform::isMac())
	{
		return {};
	}

	for (const std::string& flag: existingFlags)
	{
		if (utility::isPrefix("-isysroot", flag) || utility::isPrefix("--sysroot", flag))
		{
			return {};	// a sysroot is already specified; don't override it
		}
	}

	const std::string& sysroot = getMacOSSysrootPath();
	if (sysroot.empty())
	{
		return {};
	}
	return {"-isysroot", sysroot};
}

inline std::string IndexerCommandCxx::hashCompilerFlags(const std::vector<std::string>& compilerFlags)
{
	// FNV-1a over the flags joined by '\n'. Deterministic across processes/runs
	// (unlike std::hash), so the value can be persisted and compared on refresh.
	uint64_t hash = 1469598103934665603ULL;	// FNV offset basis
	const auto mix = [&hash](unsigned char c) {
		hash ^= c;
		hash *= 1099511628211ULL;	// FNV prime
	};
	for (const std::string& flag: compilerFlags)
	{
		for (const char c: flag)
		{
			mix(static_cast<unsigned char>(c));
		}
		mix('\n');
	}

	// hex string
	std::string out(16, '0');
	for (int i = 15; i >= 0; --i)
	{
		out[i] = "0123456789abcdef"[hash & 0xF];
		hash >>= 4;
	}
	return out;
}

inline std::string IndexerCommandCxx::getIndexerCommandHash() const
{
	return hashCompilerFlags(getCompilerFlags());
}

inline std::vector<std::string> IndexerCommandCxx::getCompilerFlagsForSystemHeaderSearchPaths(
	const std::vector<FilePath>& systemHeaderSearchPaths)
{
	std::vector<std::string> compilerFlags;
	compilerFlags.reserve(systemHeaderSearchPaths.size() * 2);

	for (const FilePath& path: systemHeaderSearchPaths)
	{
		// On macOS the SDK C headers must come from -isysroot alone. An explicit
		// "-isystem <SDK>/usr/include" places that directory ahead of the sysroot's
		// own copy and breaks libc++'s #include_next chain (<cctype> can no longer
		// reach the C <ctype.h>). Drop any SDK usr/include path -- the sysroot
		// provides it in the correct position. Matches both the Xcode and the
		// CommandLineTools SDKs regardless of which one -isysroot points at.
		if constexpr (utility::Platform::isMac())
		{
			const std::string s = path.str();
			if (utility::isPostfix("/usr/include", s) && s.find(".sdk/") != std::string::npos)
			{
				continue;
			}
		}
		compilerFlags.push_back(ClangCompiler::systemIncludeOption());
		compilerFlags.push_back(path.str());
	}
	return compilerFlags;
}

inline std::vector<std::string> IndexerCommandCxx::getCompilerFlagsForFrameworkSearchPaths(
	const std::vector<FilePath>& frameworkSearchPaths)
{
	std::vector<std::string> compilerFlags;
	compilerFlags.reserve(frameworkSearchPaths.size() * 2);
	for (const FilePath& path: frameworkSearchPaths)
	{
		compilerFlags.push_back(ClangCompiler::frameworkIncludeOption());
		compilerFlags.push_back(path.str());
	}
	return compilerFlags;
}

inline IndexerCommandType IndexerCommandCxx::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_CXX;
}

inline IndexerCommandCxx::IndexerCommandCxx(
	const FilePath& sourceFilePath,
	const std::set<FilePath>& indexedPaths,
	const std::set<FilePathFilter>& excludeFilters,
	const std::set<FilePathFilter>& includeFilters,
	const FilePath& workingDirectory,
	const std::vector<std::string>& compilerFlags,
	const std::string& compilerPath)
	: m_sourceFilePath(sourceFilePath)
	, m_indexedPaths(indexedPaths)
	, m_excludeFilters(excludeFilters)
	, m_includeFilters(includeFilters)
	, m_workingDirectory(workingDirectory)
	, m_compilerFlags(compilerFlags)
	, m_compilerPath(compilerPath)
{
}

inline IndexerCommandType IndexerCommandCxx::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

inline const FilePath& IndexerCommandCxx::getSourceFilePath() const
{
	return m_sourceFilePath;
}

inline size_t IndexerCommandCxx::getByteSize(size_t stringSize) const
{
	size_t size = 0;

	for (const FilePath& path: m_indexedPaths)
	{
		size += stringSize + path.str().size();
	}

	for (const FilePathFilter& filter: m_excludeFilters)
	{
		size += stringSize + filter.str().size();
	}

	for (const FilePathFilter& filter: m_includeFilters)
	{
		size += stringSize + filter.str().size();
	}

	for (const std::string& flag: m_compilerFlags)
	{
		size += stringSize + flag.size();
	}

	size += stringSize + m_compilerPath.size();

	return size;
}

inline const std::set<FilePath>& IndexerCommandCxx::getIndexedPaths() const
{
	return m_indexedPaths;
}

inline const std::set<FilePathFilter>& IndexerCommandCxx::getExcludeFilters() const
{
	return m_excludeFilters;
}

inline const std::set<FilePathFilter>& IndexerCommandCxx::getIncludeFilters() const
{
	return m_includeFilters;
}

inline const std::vector<std::string>& IndexerCommandCxx::getCompilerFlags() const
{
	return m_compilerFlags;
}

inline const FilePath& IndexerCommandCxx::getWorkingDirectory() const
{
	return m_workingDirectory;
}

inline const std::string& IndexerCommandCxx::getCompilerPath() const
{
	return m_compilerPath;
}
