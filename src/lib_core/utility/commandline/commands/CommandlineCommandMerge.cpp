#include "CommandlineCommandMerge.h"

#include <iostream>
#include <set>
#include <span>

#include "CommandLineParser.h"
#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#include "FileSystem.h"
#endif
#include "GlazeCli.h"
#include "PersistentStorage.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ProjectSettings.h"
#include "StorageStats.h"
#include "TextAccess.h"
#include "TimeStamp.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.settings;
import srctrl.storage;
import srctrl.utility;
#endif

namespace glz
{
template <>
struct meta<commandline::MergeOpts>
{
	using T = commandline::MergeOpts;
	static constexpr auto value = object("project-file", &T::project_file, "shard-dbs",
		&T::shard_dbs, "output", &T::output, "allow-partial", &T::allow_partial);
};
}	 // namespace glz

namespace
{
long long metaValueAsNumber(const PersistentStorage& storage, const std::string& key)
{
	const std::string value = storage.getMetaValue(key);
	try
	{
		return value.empty() ? -1 : std::stoll(value);
	}
	catch (const std::exception&)
	{
		return -1;
	}
}
}	 // namespace

namespace commandline
{
CommandlineCommandMerge::CommandlineCommandMerge(CommandLineParser* parser)
	: CommandlineCommand(
		  "merge", "Merge shard DBs produced by 'index --shard' into one project DB.", parser)
{
}

CommandlineCommandMerge::~CommandlineCommandMerge() = default;

void CommandlineCommandMerge::setup() {}

void CommandlineCommandMerge::printHelp()
{
	std::cout << "Usage: Sourcetrail merge [options] <project-file> <shard-db> <shard-db>...\n\nOptions:\n"
			  << glzcli::help<MergeOpts>() << std::endl;
}

CommandlineCommand::ReturnStatus CommandlineCommandMerge::parse(std::vector<std::string>& args)
{
	if (args.empty())
	{
		printHelp();
		return ReturnStatus::CMD_QUIT;
	}

	if (const auto stop = earlyExit(glzcli::parse(m_opts, std::span<const std::string>(args))))
		return *stop;

	if (m_opts.project_file.empty() || m_opts.shard_dbs.size() < 2)
	{
		std::cerr << "ERROR: merge needs a project file and at least two shard DBs" << std::endl;
		return ReturnStatus::CMD_FAILURE;
	}

	const ReturnStatus status = merge();
	// Success is CMD_QUIT: the merge ran synchronously; the app should exit.
	return status == ReturnStatus::CMD_OK ? ReturnStatus::CMD_QUIT : status;
}

CommandlineCommand::ReturnStatus CommandlineCommandMerge::merge() const
{
	const FilePath projectFilePath(m_opts.project_file);
	if (!projectFilePath.exists())
	{
		std::cerr << "ERROR: project file does not exist: " << projectFilePath.str() << std::endl;
		return ReturnStatus::CMD_FAILURE;
	}

	ProjectSettings settings(projectFilePath);
	if (!settings.reload())
	{
		std::cerr << "ERROR: could not load project file: " << projectFilePath.str() << std::endl;
		return ReturnStatus::CMD_FAILURE;
	}

	// ---- guards: shards exist, are version-compatible, and form a consistent stripe set
	std::set<long long> shardIndices;
	long long shardCount = -1;
	bool manifestsConsistent = true;

	for (const std::string& shardArg: m_opts.shard_dbs)
	{
		const FilePath shardPath(shardArg);
		if (!shardPath.exists())
		{
			std::cerr << "ERROR: shard DB does not exist: " << shardPath.str() << std::endl;
			return ReturnStatus::CMD_FAILURE;
		}

		PersistentStorage shard(shardPath, FilePath());
		if (shard.isEmpty() || shard.isIncompatible())
		{
			std::cerr << "ERROR: shard DB is empty or has an incompatible storage version: "
					  << shardPath.str() << std::endl;
			return ReturnStatus::CMD_FAILURE;
		}

		const long long index = metaValueAsNumber(shard, "shard_index");
		const long long count = metaValueAsNumber(shard, "shard_count");
		if (index < 1 || count < 2)
		{
			std::cerr << "WARNING: no shard manifest in " << shardPath.str()
					  << " (not produced by 'index --shard'?)" << std::endl;
			manifestsConsistent = false;
			continue;
		}
		if (shardCount == -1)
			shardCount = count;
		else if (shardCount != count)
			manifestsConsistent = false;
		if (!shardIndices.insert(index).second)
			manifestsConsistent = false;	// duplicate stripe
	}

	if (manifestsConsistent && shardCount != -1 &&
		static_cast<long long>(shardIndices.size()) != shardCount)
	{
		manifestsConsistent = false;	// incomplete stripe set
	}

	if (!manifestsConsistent)
	{
		if (!m_opts.allow_partial)
		{
			std::cerr << "ERROR: shard set is inconsistent or incomplete (expected " << shardCount
					  << " distinct stripes, got " << shardIndices.size()
					  << "). Use --allow-partial to merge anyway." << std::endl;
			return ReturnStatus::CMD_FAILURE;
		}
		std::cout << "WARNING: merging a partial/inconsistent shard set (--allow-partial)"
				  << std::endl;
	}

	// ---- target
	const FilePath targetPath = m_opts.output.empty() ? settings.getDBFilePath()
													: FilePath(m_opts.output);
	if (targetPath.exists())
	{
		std::cout << "Replacing existing DB: " << targetPath.str() << std::endl;
		FileSystem::remove(targetPath);
	}

	PersistentStorage target(targetPath, FilePath());
	target.setup();
	target.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
	target.setBulkWritePragmas(true);
	target.buildCaches();

	// ---- inject each shard (sequential; the dedup machinery collapses cross-shard duplicates)
	const TimeStamp mergeStart = TimeStamp::now();
	for (const std::string& shardArg: m_opts.shard_dbs)
	{
		const FilePath shardPath(shardArg);
		const TimeStamp shardStart = TimeStamp::now();

		PersistentStorage shard(shardPath, FilePath());
		shard.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
		shard.buildCaches();
		target.inject(&shard);

		std::cout << "merged " << shardPath.fileName() << " in "
				  << TimeStamp::now().deltaMS(shardStart) << " ms" << std::endl;
	}

	// ---- finalize: project settings so Project::load accepts the DB, caches, optimize.
	// Cross-shard duplicate rows collapse during inject already: nodes dedup by
	// serialized name, source locations by position, and occurrences via the
	// occurrence table's composite PK + "INSERT OR IGNORE" -- so no post-merge
	// dedup pass is needed (the smoke test asserts merged counts == baseline).
	target.setProjectSettingsText(TextAccess::createFromFile(projectFilePath)->getText());
	target.updateVersion();
	target.setBulkWritePragmas(false);
	target.buildCaches();
	target.optimizeMemory();

	const StorageStats stats = target.getStorageStats();
	std::cout << "merge complete: " << m_opts.shard_dbs.size() << " shards -> " << targetPath.str()
			  << " in " << TimeStamp::now().deltaMS(mergeStart) << " ms (" << stats.nodeCount
			  << " nodes, " << stats.edgeCount << " edges, " << stats.fileCount << " files)"
			  << std::endl;

	return ReturnStatus::CMD_OK;
}

}	 // namespace commandline
