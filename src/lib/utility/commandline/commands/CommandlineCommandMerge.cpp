#include "CommandlineCommandMerge.h"

#include <iostream>
#include <set>

#include "CommandLineParser.h"
#include "FilePath.h"
#include "FileSystem.h"
#include "PersistentStorage.h"
#include "ProjectSettings.h"
#include "StorageStats.h"
#include "TextAccess.h"
#include "TimeStamp.h"

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

void CommandlineCommandMerge::setup()
{
	m_app.add_option(
		"project-file", m_projectFileArg, "Project file the shards were produced from (.srctrl.toml)");
	m_app.add_option("shard-dbs", m_shardDbArgs, "Shard DB files to merge (2 or more)");
	m_app.add_option(
		"-o,--output",
		m_outputArg,
		"Output DB path (default: the project's DB, so the project opens normally afterwards)");
	m_app.add_flag(
		"--allow-partial",
		m_allowPartial,
		"Merge even if the shard set is incomplete or inconsistent (missing stripes)");
}

CommandlineCommand::ReturnStatus CommandlineCommandMerge::parse(std::vector<std::string>& args)
{
	if (args.empty() || args[0] == "help")
	{
		printHelp();
		return ReturnStatus::CMD_QUIT;
	}

	try
	{
		std::vector<std::string> fullArgs{m_name};
		fullArgs.insert(fullArgs.end(), args.begin(), args.end());
		std::vector<const char*> argv;
		for (const auto& a: fullArgs)
			argv.push_back(a.c_str());
		m_app.parse(static_cast<int>(argv.size()), argv.data());
	}
	catch (const CLI::ParseError& e)
	{
		if (e.get_exit_code() == 0)
		{
			printHelp();
			return ReturnStatus::CMD_QUIT;
		}
		std::cerr << "ERROR: " << e.what() << std::endl;
		return ReturnStatus::CMD_FAILURE;
	}

	if (m_projectFileArg.empty() || m_shardDbArgs.size() < 2)
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
	const FilePath projectFilePath(m_projectFileArg);
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

	for (const std::string& shardArg: m_shardDbArgs)
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
		if (!m_allowPartial)
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
	const FilePath targetPath = m_outputArg.empty() ? settings.getDBFilePath()
													: FilePath(m_outputArg);
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
	for (const std::string& shardArg: m_shardDbArgs)
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
	std::cout << "merge complete: " << m_shardDbArgs.size() << " shards -> " << targetPath.str()
			  << " in " << TimeStamp::now().deltaMS(mergeStart) << " ms (" << stats.nodeCount
			  << " nodes, " << stats.edgeCount << " edges, " << stats.fileCount << " files)"
			  << std::endl;

	return ReturnStatus::CMD_OK;
}

}	 // namespace commandline
