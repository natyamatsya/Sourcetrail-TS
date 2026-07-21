#include "Project.h"

#include <algorithm>

#include "ApplicationSettings.h"
#include "CombinedIndexerCommandProvider.h"
#include "DialogView.h"
#include "IndexerClusterPlan.h"
#include "IndexerCommand.h"
#include "IndexerCommandCustom.h"
#include "IndexerCommandRust.h"
#include "IndexerCommandSwift.h"
#include "MemoryIndexerCommandProvider.h"
#include "PersistentStorage.h"
#include "ProjectSettings.h"
#include "RefreshInfoGenerator.h"
#include "SourceGroup.h"
#include "SourceGroupFactory.h"
#include "SourceGroupStatusType.h"
#include "StorageCache.h"
#include "StorageProvider.h"
#include "TaskBuildIndex.h"
#include "TaskCleanStorage.h"
#include "TaskExecuteCustomCommands.h"
#include "TaskFillIndexerCommandQueue.h"
#include "TaskFinally.h"
#include "TaskFinishParsing.h"
#include "TaskInjectStorage.h"
#include "TaskMergeStorages.h"
#include "TaskParseWrapper.h"

#include "FilePath.h"
#include "FileSystem.h"
#include "MessageErrorCountClear.h"
#include "IndexingOutcome.h"
#include "MessageIndexingFinished.h"
#include "MessageIndexingShowDialog.h"
#include "MessageIndexingStarted.h"
#include "MessageIndexingStatus.h"
#include "MessageRefresh.h"
#include "MessageStatus.h"
#include "TabIds.h"
#include "TaskDecoratorRepeat.h"
#include "TaskFindKeyOnBlackboard.h"
#include "TaskGroupParallel.h"
#include "TaskGroupSelector.h"
#include "TaskGroupSequence.h"
#include "TaskLambda.h"
#include "TaskReturnSuccessIf.h"
#include "TaskSetValue.h"
#include "TextAccess.h"
#include "language_package_flags.h"
#include "utility.h"
#include "utilityApp.h"
#include "utilityFile.h"

Project::Project(
	std::shared_ptr<ProjectSettings> settings,
	StorageCache* storageCache,
	const std::string& appUUID,
	bool hasGUI)
	: m_settings(settings)
	, m_storageCache(storageCache)
	, m_appUUID(appUUID)
	, m_hasGUI(hasGUI)
{
}

Project::~Project() = default;

void Project::setShardConfig(const ShardConfig& config)
{
	m_shardConfig = config;
}

FilePath Project::getProjectSettingsFilePath() const
{
	return m_settings->getFilePath();
}

std::string Project::getDescription() const
{
	return m_settings->getDescription();
}

bool Project::isLoaded() const
{
	switch (m_state)
	{
	case ProjectStateType::PROJECT_STATE_EMPTY:
	case ProjectStateType::PROJECT_STATE_LOADED:
	case ProjectStateType::PROJECT_STATE_OUTDATED:
		return true;

	default:
		break;
	}

	return false;
}

bool Project::isIndexing() const
{
	return m_refreshStage == RefreshStageType::INDEXING;
}

bool Project::settingsEqualExceptNameAndLocation(const ProjectSettings& otherSettings) const
{
	return m_settings->equalsExceptNameAndLocation(otherSettings);
}

void Project::setStateOutdated()
{
	if (m_state == ProjectStateType::PROJECT_STATE_LOADED)
	{
		m_state = ProjectStateType::PROJECT_STATE_OUTDATED;
	}
}

void Project::load(std::shared_ptr<DialogView> dialogView)
{
	if (m_refreshStage != RefreshStageType::NONE)
	{
		MessageStatus("Cannot load another project while indexing.", true, false).dispatch();
		return;
	}

	m_storageCache->clear();
	m_storageCache->setSubject(
		std::weak_ptr<StorageAccess>());	// TODO: check if this is really required.

	if (!m_settings->reload())
	{
		return;
	}

	const FilePath dbPath = m_settings->getDBFilePath();
	const FilePath tempDbPath = m_settings->getTempDBFilePath();
	const FilePath bookmarkDbPath = m_settings->getBookmarkDBFilePath();

	{
		if (tempDbPath.exists())
		{
			if (dbPath.exists())
			{
				if (dialogView->confirm(
						"Sourcetrail has been closed unexpectedly while indexing this project. "
						"You can either choose to keep "
						"the data that has already been indexed or discard that data and restore "
						"the state of your project "
						"before indexing?",
						{"Keep and Continue", "Discard and Restore"}) == 0)
				{
					LOG_INFO("Switching to temporary indexing data on user's decision");
					if (!swapToTempStorageFile(dbPath, tempDbPath, dialogView))
					{
						m_state = ProjectStateType::PROJECT_STATE_NOT_LOADED;
						MessageStatus("Unable to load project", true, false).dispatch();
						return;
					}
				}
				else
				{
					LOG_INFO("Discarding temporary indexing data on user's decision");
					FileSystem::remove(tempDbPath);
				}
			}
			else
			{
				LOG_INFO(
					"Switching to temporary indexing data because no other persistent data was "
					"found");
				FileSystem::rename(tempDbPath, dbPath);
			}
		}
	}

	m_storage = std::make_shared<PersistentStorage>(dbPath, bookmarkDbPath);

	bool canLoad = false;

	if (m_storage->isEmpty())
	{
		m_state = ProjectStateType::PROJECT_STATE_EMPTY;
	}
	else if (m_storage->isIncompatible())
	{
		m_state = ProjectStateType::PROJECT_STATE_OUTVERSIONED;
	}
	else
	{
		ProjectSettings storedSettings;
		if (storedSettings.loadFromString(m_storage->getProjectSettingsText()) &&
			m_settings->equalsExceptNameAndLocation(storedSettings))
		{
			m_state = ProjectStateType::PROJECT_STATE_LOADED;
			canLoad = true;
		}
		else
		{
			m_state = ProjectStateType::PROJECT_STATE_OUTDATED;
			canLoad = true;
		}
	}

	try
	{
		m_storage->setup();
	}
	catch (...)
	{
		LOG_ERROR("Exception has been encountered while loading the project.");

		canLoad = false;
		m_state = ProjectStateType::PROJECT_STATE_DB_CORRUPTED;
	}

	m_sourceGroups = SourceGroupFactory::getInstance()->createSourceGroups(
		m_settings->getAllSourceGroupSettings());

	if (canLoad)
	{
		m_storage->setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
		m_storage->buildCaches();
		m_storageCache->setSubject(m_storage);

		if (m_hasGUI)
		{
			MessageIndexingFinished().dispatch();
		}
		MessageStatus("Finished Loading", false, false).dispatch();
	}
	else
	{
		switch (m_state)
		{
		case ProjectStateType::PROJECT_STATE_EMPTY:
			MessageStatus(
				"Project could not load any symbols because the index database is empty. Please "
				"re-index the "
				"project.",
				false,
				false)
				.dispatch();
			break;
		case ProjectStateType::PROJECT_STATE_OUTVERSIONED:
			MessageStatus(
				"Project could not be loaded because the indexed data format is incompatible to "
				"the current "
				"version of Sourcetrail. Please re-index the project.",
				false,
				false)
				.dispatch();
			break;
		default:
			MessageStatus("Project could not be loaded.", false, false).dispatch();
		}
	}

	if (m_state != ProjectStateType::PROJECT_STATE_LOADED && m_hasGUI)
	{
		MessageRefresh().dispatch();
	}
}

void Project::refresh(std::shared_ptr<DialogView> dialogView, RefreshMode refreshMode)
{
	// std::expected exception boundary (ADR-0004): refresh runs inside a generic scheduled task
	// whose runner would swallow an escaping exception with a single log line -- and without a
	// terminal MessageIndexingFinished a headless run waits forever. Convert any escape into a
	// failed outcome and dispatch the terminal event ourselves.
	const IndexingOutcome outcome = utility::expectedFromExceptions<void>(
		IndexingErrorCode::PipelineFailed,
		IndexingErrorCode::PipelineFailed,
		"project refresh failed",
		[&]() { refreshImpl(dialogView, refreshMode); });
	if (!outcome)
	{
		LOG_ERROR(utility::expectedErrorToString(outcome.error()));
		m_refreshStage = RefreshStageType::NONE;
		MessageIndexingFinished(outcome).dispatch();
	}
}

void Project::refreshImpl(std::shared_ptr<DialogView> dialogView, RefreshMode refreshMode)
{
	if (m_refreshStage != RefreshStageType::NONE)
	{
		return;
	}

	if (m_state == ProjectStateType::PROJECT_STATE_NOT_LOADED)
	{
		return;
	}

	bool needsFullRefresh = false;
	bool fullRefresh = false;
	std::string question;

	switch (m_state)
	{
	case ProjectStateType::PROJECT_STATE_EMPTY:
		needsFullRefresh = true;
		break;

	case ProjectStateType::PROJECT_STATE_LOADED:
		break;

	case ProjectStateType::PROJECT_STATE_OUTDATED:
		question =
			"The project file was changed after the last indexing. The project needs to get fully "
			"reindexed to "
			"reflect the current project state. Alternatively you can also choose to just reindex "
			"updated or "
			"incomplete files. Do you want to reindex the project?";
		fullRefresh = true;
		break;

	case ProjectStateType::PROJECT_STATE_OUTVERSIONED:
		question =
			"This project was indexed with a different version of Sourcetrail. It needs to be "
			"fully reindexed to "
			"be used with this version of Sourcetrail. Do you want to reindex the project?";
		needsFullRefresh = true;
		break;

	case ProjectStateType::PROJECT_STATE_DB_CORRUPTED:
		question =
			"There was a problem loading the index of this project. The project needs to get "
			"fully reindexed. "
			"Do you want to reindex the project?";
		needsFullRefresh = true;
		break;

	default:
		break;
	}

	if (question.size() && m_hasGUI)
	{
		if (dialogView->confirm(question, {"Reindex", "Cancel"}) == 1)
		{
			return;
		}
	}

	if (ApplicationSettings::getInstance()->getLoggingEnabled() &&
		ApplicationSettings::getInstance()->getVerboseIndexerLoggingEnabled() && m_hasGUI)
	{
		if (dialogView->confirm(
				"Warning: You are about to index your project with the \"verbose indexer "
				"logging\" setting "
				"enabled. This will cause a significant slowdown in indexing performance. Do you "
				"want to proceed?",
				{"Yes", "No"}) == 1)
		{
			return;
		}
	}

	m_refreshStage = RefreshStageType::REFRESHING;

	m_settings->reload();

	m_sourceGroups = SourceGroupFactory::getInstance()->createSourceGroups(
		m_settings->getAllSourceGroupSettings());
	for (const std::shared_ptr<SourceGroup>& sourceGroup: m_sourceGroups)
	{
		if (sourceGroup->getStatus() == SourceGroupStatusType::ENABLED && !sourceGroup->prepareIndexing())
		{
			m_refreshStage = RefreshStageType::NONE;
			return;
		}
	}

	if (needsFullRefresh || fullRefresh)
	{
		refreshMode = RefreshMode::ALL_FILES;
	}
	else if (refreshMode == RefreshMode::NONE)
	{
		refreshMode = RefreshMode::UPDATED_FILES;
	}

	if (m_hasGUI)
	{
		std::vector<RefreshMode> enabledModes = {RefreshMode::ALL_FILES};
		if (!needsFullRefresh)
		{
			enabledModes.insert(
				enabledModes.end(), {RefreshMode::UPDATED_FILES, RefreshMode::UPDATED_AND_INCOMPLETE_FILES});
		}

		dialogView->startIndexingDialog(
			this,
			enabledModes,
			refreshMode,
			[this, dialogView](const RefreshInfo& info) { buildIndex(info, dialogView); },
			[this]() { m_refreshStage = RefreshStageType::NONE; });
	}
	else
	{
		RefreshInfo info = getRefreshInfo(refreshMode);

		if (m_shardConfig.isActive())
		{
			// Distributed indexing: keep only this process's deterministic stripe.
			// The CLI enforces --full, so info holds the complete sorted TU set --
			// identical on every shard producer, making the stripes disjoint and
			// complete across producers.
			const size_t totalFileCount = info.filesToIndex.size();
			shard::stripeFilter(&info.filesToIndex, m_shardConfig.index, m_shardConfig.count);
			LOG_INFO_STREAM(
				<< "shard " << m_shardConfig.index << "/" << m_shardConfig.count << ": indexing "
				<< info.filesToIndex.size() << " of " << totalFileCount << " source files");
		}

		buildIndex(info, dialogView);
	}
}

RefreshInfo Project::getRefreshInfo(RefreshMode mode) const
{
	switch (mode)
	{
	case RefreshMode::NONE:
		return RefreshInfo();

	case RefreshMode::UPDATED_FILES:
		return RefreshInfoGenerator::getRefreshInfoForUpdatedFiles(m_sourceGroups, m_storage);

	case RefreshMode::UPDATED_AND_INCOMPLETE_FILES:
		return RefreshInfoGenerator::getRefreshInfoForIncompleteFiles(m_sourceGroups, m_storage);

	case RefreshMode::ALL_FILES:
	default:
		return RefreshInfoGenerator::getRefreshInfoForAllFiles(m_sourceGroups);
	}
}

std::vector<std::shared_ptr<const SourceGroup>> Project::getSourceGroups() const
{
	std::vector<std::shared_ptr<const SourceGroup>> sourceGroups{};
	for (const auto& sourceGroup : m_sourceGroups)
		sourceGroups.push_back(sourceGroup);
	return sourceGroups;
}

void Project::buildIndex(RefreshInfo info, std::shared_ptr<DialogView> dialogView)
{
	// Same std::expected boundary as refresh(): buildIndex is also entered directly by the GUI
	// indexing dialog's callback.
	const IndexingOutcome outcome = utility::expectedFromExceptions<void>(
		IndexingErrorCode::PipelineFailed,
		IndexingErrorCode::PipelineFailed,
		"assembling the indexing pipeline failed",
		[&]() { buildIndexImpl(std::move(info), dialogView); });
	if (!outcome)
	{
		LOG_ERROR(utility::expectedErrorToString(outcome.error()));
		m_refreshStage = RefreshStageType::NONE;
		MessageIndexingFinished(outcome).dispatch();
	}
}

void Project::buildIndexImpl(RefreshInfo info, std::shared_ptr<DialogView> dialogView)
{
	if (m_refreshStage == RefreshStageType::INDEXING)
	{
		MessageStatus("Cannot refresh project while indexing.", true, false).dispatch();
		return;
	}

	{
		std::string message;
		if (info.mode != RefreshMode::ALL_FILES && info.filesToClear.empty() && info.filesToIndex.empty())
		{
			message = "Nothing to refresh, all files are up-to-date.";
		}
		else if (m_sourceGroups.empty())
		{
			message = "Nothing to refresh, no Source Groups loaded.";
		}

		if (!message.empty())
		{
			if (m_hasGUI)
			{
				dialogView->clearDialogs();
			}
			else
			{
				MessageIndexingFinished().dispatch();
			}

			MessageStatus(message).dispatch();
			m_refreshStage = RefreshStageType::NONE;
			return;
		}
	}

	if (info.mode != RefreshMode::ALL_FILES &&
		(info.filesToClear.size() || info.nonIndexedFilesToClear.size()))
	{
		for (const std::shared_ptr<SourceGroup>& sourceGroup: m_sourceGroups)
		{
			if (sourceGroup->getStatus() == SourceGroupStatusType::ENABLED &&
				!sourceGroup->allowsPartialClearing())
			{
				bool abortIndexing = false;
				for (const FilePath& sourcePath:
					 utility::concat(info.filesToClear, info.nonIndexedFilesToClear))
				{
					if (sourceGroup->containsSourceFilePath(sourcePath))
					{
						abortIndexing = true;
						break;
					}
				}

				if (abortIndexing)
				{
					if (m_hasGUI &&
						dialogView->confirm(
							"<p>This project contains a source group of type \"" +
								sourceGroupTypeToString(sourceGroup->getType()) +
								"\" that cannot be partially cleared. Do you want to re-index the "
								"whole project instead?</p>",
							{"Full Re-Index", "Cancel"}) == 1)
					{
						MessageStatus("Cannot partially clear project. Indexing aborted.").dispatch();
						m_refreshStage = RefreshStageType::NONE;
						dialogView->clearDialogs();
						return;
					}
					else
					{
						info = getRefreshInfo(RefreshMode::ALL_FILES);
					}
				}

				break;
			}
		}
	}

	MessageStatus("Preparing Indexing", false, true).dispatch();
	MessageErrorCountClear().dispatch();

	dialogView->showUnknownProgressDialog("Preparing Indexing", "Setting up Indexers");
	MessageIndexingStatus(true, 0).dispatch();

	m_storageCache->clear();
	m_storageCache->setSubject(m_storage);

	const FilePath indexDbFilePath = m_settings->getDBFilePath();
	FilePath tempIndexDbFilePath = m_settings->getTempDBFilePath();
	FilePath bookmarkDbFilePath = m_storage->getBookmarkDbFilePath();

	if (m_shardConfig.isActive())
	{
		// Shard runs write a standalone shard DB and never touch the live project
		// DB or the bookmarks; the swap at the end is skipped (see below).
		tempIndexDbFilePath = m_shardConfig.outputPath.empty()
			? indexDbFilePath.getParentDirectory().getConcatenated(FilePath(
				  m_settings->getFilePath().withoutExtension().fileName() + ".shard" +
				  std::to_string(m_shardConfig.index) + "of" +
				  std::to_string(m_shardConfig.count) + ".srctrl.db"))
			: m_shardConfig.outputPath;
		bookmarkDbFilePath = FilePath();

		if (tempIndexDbFilePath.exists())
		{
			FileSystem::remove(tempIndexDbFilePath);
		}
	}
	else if (info.mode != RefreshMode::ALL_FILES)
	{
		// store the indexed data into the temp db but keep the current state to allow browsing
		// while indexing
		FileSystem::copyFile(indexDbFilePath, tempIndexDbFilePath);
	}

	std::shared_ptr<PersistentStorage> tempStorage = std::make_shared<PersistentStorage>(
		tempIndexDbFilePath, bookmarkDbFilePath);
	tempStorage->setup();
	// Indexing target is throwaway until the final swap: skip fsync per commit.
	// Restored to NORMAL in TaskFinishParsing before optimize/swap.
	tempStorage->setBulkWritePragmas(true);

	std::shared_ptr<TaskGroupSequence> taskSequential = std::make_shared<TaskGroupSequence>();

	if (info.mode != RefreshMode::ALL_FILES &&
		(info.filesToClear.size() || info.nonIndexedFilesToClear.size()))
	{
		taskSequential->addTask(std::make_shared<TaskCleanStorage>(
			tempStorage,
			dialogView,
			utility::toVector(utility::concat(info.filesToClear, info.nonIndexedFilesToClear)),
			info.mode == RefreshMode::UPDATED_AND_INCOMPLETE_FILES));
	}

	tempStorage->setProjectSettingsText(
		TextAccess::createFromFile(getProjectSettingsFilePath())->getText());
	tempStorage->updateVersion();

	std::unique_ptr<CombinedIndexerCommandProvider> indexerCommandProvider =
		std::make_unique<CombinedIndexerCommandProvider>();
	std::unique_ptr<CombinedIndexerCommandProvider> customIndexerCommandProvider =
		std::make_unique<CombinedIndexerCommandProvider>();
	std::vector<IndexerClusterEntry> cxxClusters;
	size_t rustCrateCount = 0;
	size_t swiftPackageCount = 0;

	// Distributed indexing (SW7): keep only this shard's stripe of package/crate
	// commands, and shrink `roots` to match so the fan-out supervisor count
	// reflects the striped work. A no-op when no ShardConfig is active. The
	// per-command key is getSourceFilePath(): SourceGroupRust/Swift set both the
	// source-file path and the working directory to the package root.
	auto shardStripeCommands =
		[this](
			std::vector<std::shared_ptr<IndexerCommand>> commands,
			std::set<std::string>& roots) -> std::vector<std::shared_ptr<IndexerCommand>>
	{
		if (!m_shardConfig.isActive())
			return commands;

		const std::set<std::string> kept =
			shard::stripeKeys(roots, m_shardConfig.index, m_shardConfig.count);
		std::vector<std::shared_ptr<IndexerCommand>> striped;
		for (const std::shared_ptr<IndexerCommand>& command: commands)
		{
			if (kept.contains(command->getSourceFilePath().str()))
				striped.push_back(command);
		}
		roots = kept;
		return striped;
	};
	for (const std::shared_ptr<SourceGroup>& sourceGroup: m_sourceGroups)
	{
		if (sourceGroup->getStatus() == SourceGroupStatusType::ENABLED)
		{
			const std::string sourceGroupId = sourceGroup->getSourceGroupId();
			if (sourceGroup->getType() == SourceGroupType::CUSTOM_COMMAND)
			{
				customIndexerCommandProvider->addProvider(
					sourceGroup->getIndexerCommandProvider(info), sourceGroupId);
			}
			else
			{
				// Rust fan-out R1: SourceGroupRust emits ONE command per group
				// (working directory = the workspace root; the subprocess indexes
				// all member crates in that one run). Distinct working
				// directories therefore == parallelizable Rust commands, i.e.
				// K > 1 only helps projects with multiple Rust groups today.
				// Splitting a workspace into per-member-crate commands is R1b
				// (see DESIGN_RUST_CRATE_FANOUT.md). Peeking is free — Rust
				// groups materialize commands eagerly anyway.
				const LanguageType language = sourceGroup->getLanguage();
				if (language == LanguageType::RUST)
				{
					std::vector<std::shared_ptr<IndexerCommand>> commands =
						sourceGroup->getIndexerCommands(info);
					std::set<std::string> crateRoots;
					for (const std::shared_ptr<IndexerCommand>& command: commands)
					{
						if (const auto rustCommand =
								command->target<IndexerCommandRust>())
						{
							crateRoots.insert(rustCommand->getWorkingDirectory().str());
						}
					}

					// Distributed indexing (SW7): a Rust crate is one work unit,
					// so stripe by crate root — otherwise every shard reindexes
					// every crate (the file-level stripe never reaches these
					// commands, which only gate on filesToIndex emptiness).
					commands = shardStripeCommands(commands, crateRoots);
					rustCrateCount += crateRoots.size();

					indexerCommandProvider->addProvider(
						std::make_shared<MemoryIndexerCommandProvider>(commands), sourceGroupId);
					continue;
				}

				// Swift fan-out (SW6): the analog of the Rust branch —
				// SourceGroupSwift emits one command per SPM package root, so
				// distinct working directories are parallelizable Swift
				// commands. Materialize eagerly to count them.
				if (language == LanguageType::SWIFT)
				{
					std::vector<std::shared_ptr<IndexerCommand>> commands =
						sourceGroup->getIndexerCommands(info);
					std::set<std::string> packageRoots;
					for (const std::shared_ptr<IndexerCommand>& command: commands)
					{
						if (const auto swiftCommand =
								command->target<IndexerCommandSwift>())
						{
							packageRoots.insert(swiftCommand->getWorkingDirectory().str());
						}
					}

					// Distributed indexing (SW7): one work unit per SPM package,
					// striped by package root like the Rust crates above.
					commands = shardStripeCommands(commands, packageRoots);
					swiftPackageCount += packageRoots.size();

					indexerCommandProvider->addProvider(
						std::make_shared<MemoryIndexerCommandProvider>(commands), sourceGroupId);
					continue;
				}

				std::shared_ptr<IndexerCommandProvider> provider =
					sourceGroup->getIndexerCommandProvider(info);

				// Fan-out S3: C/C++ groups form per-group subprocess clusters.
				if (language == LanguageType::C || language == LanguageType::CXX)
				{
					cxxClusters.push_back(
						IndexerClusterEntry{sourceGroupId, language, provider->size(), 0});
				}

				indexerCommandProvider->addProvider(std::move(provider), sourceGroupId);
			}
		}
	}

	size_t sourceFileCount = indexerCommandProvider->size() + customIndexerCommandProvider->size();

	taskSequential->addTask(std::make_shared<TaskSetValue<int>>("source_file_count", static_cast<int>(sourceFileCount)));
	taskSequential->addTask(std::make_shared<TaskSetValue<int>>("indexed_source_file_count", 0));
	taskSequential->addTask(std::make_shared<TaskSetValue<bool>>("interrupted_indexing", false));
	taskSequential->addTask(std::make_shared<TaskSetValue<float>>("index_time", 0.0f));


	int indexerThreadCount = ApplicationSettings::getInstance()->getIndexerThreadCount();
	if (indexerThreadCount <= 0)
	{
		indexerThreadCount = utility::getIdealThreadCount();
		if (indexerThreadCount <= 0)
		{
			indexerThreadCount = 4;	   // setting to some fallback value
		}
	}

	if (!indexerCommandProvider->empty())
	{
		const int adjustedIndexerThreadCount = std::min<int>(
			indexerThreadCount, static_cast<int>(indexerCommandProvider->size()));

		std::shared_ptr<StorageProvider> storageProvider = std::make_shared<StorageProvider>();
		// add tasks for setting some variables on the blackboard that are used during indexing
		taskSequential->addTask(
			std::make_shared<TaskSetValue<bool>>("indexer_threads_started", false));
		taskSequential->addTask(
			std::make_shared<TaskSetValue<bool>>("indexer_threads_stopped", false));
		taskSequential->addTask(
			std::make_shared<TaskSetValue<bool>>("indexer_command_queue_started", false));
		taskSequential->addTask(
			std::make_shared<TaskSetValue<bool>>("indexer_command_queue_stopped", false));

		std::shared_ptr<TaskGroupSequence> preIndexTasks = std::make_shared<TaskGroupSequence>();
		taskSequential->addTask(preIndexTasks);
		for (const std::shared_ptr<SourceGroup>& sourceGroup: m_sourceGroups)
		{
			if (sourceGroup->getStatus() == SourceGroupStatusType::ENABLED)
			{
				preIndexTasks->addTask(sourceGroup->getPreIndexTask(storageProvider, dialogView));
			}
		}

		std::shared_ptr<TaskParseWrapper> taskParserWrapper = std::make_shared<TaskParseWrapper>(
			tempStorage, dialogView);
		taskSequential->addTask(taskParserWrapper);

		std::shared_ptr<TaskGroupParallel> taskParallelIndexing =
			std::make_shared<TaskGroupParallel>();
		taskParserWrapper->setTask(taskParallelIndexing);

		// add task for refilling the indexer command queue
		taskParallelIndexing->addTask(std::make_shared<TaskFillIndexerCommandsQueue>(
			m_appUUID, std::move(indexerCommandProvider), 2));

		// add task for indexing.
		// No hardcoded subprocess cap (was 6, then 12): the user setting /
		// hardware-concurrency default is the knob, and the pipeline self-regulates
		// (subprocess outbox + provider back-pressure). Measured on a 12-core/32GB
		// machine indexing this repo: 6 -> 12 subprocesses cut wall time 21% (writer
		// at 95.5% utilization, zero stalls); peak RSS 8.4GB total (~0.8GB per
		// indexer). The indexing-summary stall counter is the tripwire: stalls > 0
		// means the serial writer became the ceiling (-> roadmap B3, sharded ingest).
		const int effectiveIndexerThreadCount = hasCxxSourceGroup() ? adjustedIndexerThreadCount
																	: 0;

		// Fan-out S5 gate: the tri-state override governs the whole fan-out
		// feature — "off" forces today's exact legacy path, "auto" (default)
		// enables it under its structural conditions, "on" additionally uses
		// the sole writer for single-group full refreshes. (The group-aware
		// queue fill stays active regardless: it is a starvation fix for the
		// Rust/Swift supervisors, independent of fan-out.)
		const std::string fanOutMode = ApplicationSettings::getInstance()->getMultiGroupFanOutMode();
		const bool fanOutEnabled = fanOutMode != "off";

		// Fan-out S3: with >= 2 non-empty C++ clusters, split the subprocess
		// budget across them proportional to command counts (min 1 each) and pin
		// each subprocess to its group. One cluster => empty plan => today's
		// exact single-pool behavior.
		std::vector<IndexerClusterEntry> cxxClusterPlan;
		const auto nonEmptyClusterCount = std::count_if(
			cxxClusters.begin(), cxxClusters.end(), [](const IndexerClusterEntry& cluster) {
				return cluster.commandCount > 0;
			});
		if (fanOutEnabled && nonEmptyClusterCount >= 2)
		{
			cxxClusterPlan = allocateIndexerSubprocesses(
				std::move(cxxClusters), static_cast<size_t>(effectiveIndexerThreadCount));
		}

#ifdef SOURCETRAIL_TURSO_CONCURRENT
		// Fan-out S4: with the fan-out active and a clean target (full refresh),
		// the concurrent Turso writer becomes the SOLE ingest writer; the result
		// is exported back to SQLite after the drain. Incremental refreshes stay
		// on the serial path — the writer's in-process dedup index cannot see
		// rows already present in the copied-over database. Mode "on" engages
		// the sole writer even for a single C++ cluster.
		const bool soleWriterWanted = fanOutMode == "on"
			? hasCxxSourceGroup()
			: !cxxClusterPlan.empty();
		if (fanOutEnabled && soleWriterWanted && info.mode == RefreshMode::ALL_FILES)
		{
			tempStorage->setupConcurrentTursoSoleWriter();
		}
#endif

		// Rust fan-out R1: K Rust supervisors share the command queue when the
		// fan-out is enabled and >= 2 parallelizable Rust commands exist.
		// Bootstrap count — hard cap 3 until R2 adds the memory/thread-budget
		// policy.
		size_t rustSupervisorCount = 1;
		if (fanOutEnabled && rustCrateCount >= 2)
		{
			rustSupervisorCount = std::min<size_t>(rustCrateCount, 3);
		}

		// Swift fan-out (SW6): K Swift supervisors, same policy as Rust — hard
		// cap 3 until a memory/thread-budget policy lands.
		size_t swiftSupervisorCount = 1;
		if (fanOutEnabled && swiftPackageCount >= 2)
		{
			swiftSupervisorCount = std::min<size_t>(swiftPackageCount, 3);
		}

		taskParallelIndexing->addChildTasks(std::make_shared<TaskGroupSequence>()->addChildTasks(
			// block until there are indexer commands to process
			std::make_shared<TaskDecoratorRepeat>(
				TaskDecoratorRepeat::ConditionType::CONDITION_WHILE_SUCCESS, Task::TaskState::STATE_SUCCESS, 25)
				->addChildTask(std::make_shared<TaskReturnSuccessIf<bool>>(
					"indexer_command_queue_started", TaskReturnSuccessIf<bool>::ConditionType::CONDITION_EQUALS, false)),
			std::make_shared<TaskBuildIndex>(
				effectiveIndexerThreadCount,
				storageProvider,
				dialogView,
				m_appUUID,
				cxxClusterPlan,
				rustSupervisorCount,
				swiftSupervisorCount)));

		// add task for merging the intermediate storages.
		// Event-driven: TaskMergeStorages blocks on the StorageProvider until enough
		// storages exist or the producers are done (SUCCESS per merge, FAILURE once
		// done) -- no repeat delay, no blackboard-polling fallback needed.
		taskParallelIndexing->addTask(std::make_shared<TaskGroupSequence>()->addChildTasks(
			// block until there are indexers running
			std::make_shared<TaskDecoratorRepeat>(
				TaskDecoratorRepeat::ConditionType::CONDITION_WHILE_SUCCESS, Task::TaskState::STATE_SUCCESS, 25)
				->addChildTask(std::make_shared<TaskReturnSuccessIf<bool>>(
					"indexer_threads_started", TaskReturnSuccessIf<bool>::ConditionType::CONDITION_EQUALS, false)),
			std::make_shared<TaskDecoratorRepeat>(
				TaskDecoratorRepeat::ConditionType::CONDITION_WHILE_SUCCESS, Task::TaskState::STATE_SUCCESS, 0)
				->addChildTask(std::make_shared<TaskMergeStorages>(storageProvider))));

		// add task for injecting the intermediate storages into the persistent storage.
		// Event-driven like the merge stream: TaskInjectStorage blocks on the provider,
		// drains the remainder once producers are done, and ends via FAILURE.
		taskParallelIndexing->addTask(std::make_shared<TaskGroupSequence>()->addChildTasks(
			// block until there are indexers running
			std::make_shared<TaskDecoratorRepeat>(
				TaskDecoratorRepeat::ConditionType::CONDITION_WHILE_SUCCESS, Task::TaskState::STATE_SUCCESS, 25)
				->addChildTask(std::make_shared<TaskReturnSuccessIf<bool>>(
					"indexer_threads_started", TaskReturnSuccessIf<bool>::ConditionType::CONDITION_EQUALS, false)),
			std::make_shared<TaskDecoratorRepeat>(
				TaskDecoratorRepeat::ConditionType::CONDITION_WHILE_SUCCESS, Task::TaskState::STATE_SUCCESS, 0)
				->addChildTask(std::make_shared<TaskInjectStorage>(storageProvider, tempStorage))));

		// add task that notifies the user of what's going on
		taskSequential->addTask(	// we don't need to hide this dialog again, because it's
									// overridden by other dialogs later on.
			std::make_shared<TaskLambda>([dialogView]() {
				dialogView->showUnknownProgressDialog("Finish Indexing", "Saving\nRemaining Data");
			}));

		// add task that injects the remaining intermediate storages into the persistent
		// storage (producers are done by now, so the inject task never blocks: it
		// drains the remainder and ends via FAILURE when the provider is empty)
		taskSequential->addTask(
			std::make_shared<TaskDecoratorRepeat>(
				TaskDecoratorRepeat::ConditionType::CONDITION_WHILE_SUCCESS, Task::TaskState::STATE_SUCCESS, 0)
				->addChildTask(std::make_shared<TaskInjectStorage>(storageProvider, tempStorage)));
	}
	else
	{
		dialogView->hideUnknownProgressDialog();
	}

	if (!customIndexerCommandProvider->empty())
	{
		const int adjustedIndexerThreadCount = std::min<int>(
			indexerThreadCount, static_cast<int>(customIndexerCommandProvider->size()));

		taskSequential->addTask(std::make_shared<TaskExecuteCustomCommands>(
			std::move(customIndexerCommandProvider),
			tempStorage,
			dialogView,
			adjustedIndexerThreadCount,
			getProjectSettingsFilePath().getParentDirectory()));
	}

	taskSequential->addTask(std::make_shared<TaskFinishParsing>(tempStorage, dialogView));

	if (m_shardConfig.isActive())
	{
		// Shard run: no swap/discard -- the shard DB stays at its output path.
		// Record the manifest so `merge` can verify stripe consistency.
		const size_t stripedFileCount = info.filesToIndex.size();
		taskSequential->addTask(std::make_shared<TaskLambda>(
			[tempStorage, tempIndexDbFilePath, stripedFileCount, this]() {
				tempStorage->setMetaValue("shard_index", std::to_string(m_shardConfig.index));
				tempStorage->setMetaValue("shard_count", std::to_string(m_shardConfig.count));
				tempStorage->setMetaValue("shard_file_count", std::to_string(stripedFileCount));
				LOG_INFO_STREAM(
					<< "shard " << m_shardConfig.index << "/" << m_shardConfig.count
					<< " written to " << tempIndexDbFilePath.str());
			}));
	}
	else
	{
		taskSequential->addTask(std::make_shared<TaskGroupSelector>()->addChildTasks(
			std::make_shared<TaskGroupSequence>()->addChildTasks(
				std::make_shared<TaskFindKeyOnBlackboard>("keep_database"),
				std::make_shared<TaskLambda>([dialogView, this]() {
					Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([dialogView, this]() {
									   swapToTempStorage(dialogView);
								   }));
				})),
			std::make_shared<TaskGroupSequence>()->addChildTasks(
				std::make_shared<TaskFindKeyOnBlackboard>("discard_database"),
				std::make_shared<TaskLambda>([this]() {
					Task::dispatch(
						TabIds::app(), std::make_shared<TaskLambda>([this]() { discardTempStorage(); }));
				}))));
	}

	taskSequential->addTask(std::make_shared<TaskLambda>([dialogView, this]() {
		m_refreshStage = RefreshStageType::NONE;
		MessageIndexingFinished().dispatch();
	}));

	taskSequential->addTask(std::make_shared<TaskGroupSelector>()->addChildTasks(
		std::make_shared<TaskGroupSequence>()->addChildTasks(
			std::make_shared<TaskFindKeyOnBlackboard>("refresh_database"),
			std::make_shared<TaskLambda>([dialogView, this]() {
				Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([dialogView, this]() {
								   MessageIndexingShowDialog().dispatch();
								   MessageRefresh().refreshAll().dispatch();
							   }));
			})),
		std::make_shared<TaskGroupSequence>()->addChildTasks(std::make_shared<TaskLambda>([this]() {
			Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([this]() {}));
		}))));

	taskSequential->setIsBackgroundTask(true);
	// The pipeline root owns the terminal event: whatever way the tree ends abnormally (a failed
	// or throwing stage -- converted to STATE_FAILURE by its TaskRunner -- or termination), the
	// run still reaches exactly one MessageIndexingFinished. Success is dispatched by the tree's
	// own final stage, so the callback stays silent on Success.
	Task::dispatch(
		TabIds::app(),
		std::make_shared<TaskFinally>([this](TaskFinally::TerminalCause cause) {
			if (cause == TaskFinally::TerminalCause::Success)
			{
				return;
			}
			m_refreshStage = RefreshStageType::NONE;
			const bool terminated = cause == TaskFinally::TerminalCause::Terminated;
			MessageIndexingFinished(std::unexpected(utility::makeExpectedError(
					terminated ? IndexingErrorCode::PipelineTerminated
							   : IndexingErrorCode::PipelineFailed,
					terminated ? "indexing was terminated before completion"
							   : "an indexing pipeline stage failed; see the errors above")))
				.dispatch();
		})->addChildTask(taskSequential));

	m_refreshStage = RefreshStageType::INDEXING;
	MessageStatus(
		"Starting Indexing: " + std::to_string(sourceFileCount) + " source files", false, true)
		.dispatch();
	MessageIndexingStarted().dispatch();
}

void Project::swapToTempStorage(std::shared_ptr<DialogView> dialogView)
{
	LOG_INFO("Switching to temporary indexing data");

	const FilePath indexDbFilePath = m_settings->getDBFilePath();
	const FilePath tempIndexDbFilePath = m_settings->getTempDBFilePath();
	const FilePath bookmarkDbFilePath = m_settings->getBookmarkDBFilePath();

	m_storage.reset();

	if (!swapToTempStorageFile(indexDbFilePath, tempIndexDbFilePath, dialogView))
	{
		m_state = ProjectStateType::PROJECT_STATE_NOT_LOADED;
		return;
	}

	m_storage = std::make_shared<PersistentStorage>(indexDbFilePath, bookmarkDbFilePath);
	m_storage->setup();

	// Persist the compile-command hash of every current source file, so a later
	// incremental refresh can detect flag changes that leave the source mtime
	// untouched (flag-aware refresh). Written to the freshly swapped-in database
	// here -- there is no further swap, so the connection stays consistent (writing
	// to the pre-swap temp database instead corrupts the rename/WAL handoff).
	m_storage->setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
	m_storage->startInjection();
	for (const std::shared_ptr<SourceGroup>& sourceGroup: m_sourceGroups)
	{
		if (sourceGroup->getStatus() != SourceGroupStatusType::ENABLED)
		{
			continue;
		}
		for (const auto& [path, hash]: sourceGroup->getAllSourceFileCommandHashes())
		{
			m_storage->setFileCommandHash(path.str(), hash);
		}
	}
	m_storage->finishInjection();

	m_storage->setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);

	// std::shared_ptr<DialogView> dialogView =
	// Application::getInstance()->getDialogView(DialogView::UseCase::INDEXING);
	// dialogView->showUnknownProgressDialog("Finish Indexing", "Building caches");
	m_storage->buildCaches();
	// dialogView->hideUnknownProgressDialog();

	m_storageCache->setSubject(m_storage);
	m_state = ProjectStateType::PROJECT_STATE_LOADED;
}

bool Project::swapToTempStorageFile(
	const FilePath& indexDbFilePath,
	const FilePath& tempIndexDbFilePath,
	std::shared_ptr<DialogView> dialogView)
{
	try
	{
		FileSystem::remove(indexDbFilePath);
		FileSystem::rename(tempIndexDbFilePath, indexDbFilePath);
	}
	catch (std::exception& /*e*/)
	{
		if (m_hasGUI)
		{
			dialogView->confirm(
				"<p>The old index database file of this project seems to be used by a different "
				"process and cannot "
				"be updated.</p><p>Please close all processes that are using this database and "
				"re-load this project to "
				"apply or discard the changes pending from the current indexer run.</p>");
		}
		return false;
	}
	return true;
}

void Project::discardTempStorage()
{
	const FilePath tempIndexDbPath = m_settings->getTempDBFilePath();
	if (tempIndexDbPath.exists())
	{
		LOG_INFO("Discarding temporary indexing data");
		FileSystem::remove(tempIndexDbPath);
	}
}

bool Project::hasCxxSourceGroup() const
{
	if constexpr (language_packages::buildCxxLanguagePackage)
	{
		for (const std::shared_ptr<SourceGroup>& sourceGroup: m_sourceGroups)
		{
			if (sourceGroup->getStatus() == SourceGroupStatusType::ENABLED)
			{
				if (sourceGroup->getLanguage() == LanguageType::C || sourceGroup->getLanguage() == LanguageType::CXX)
					return true;
			}
		}
	}

	return false;
}
