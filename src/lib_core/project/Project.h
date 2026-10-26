#ifndef PROJECT_H
#define PROJECT_H

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "RefreshInfo.h"
#include "SourceGroup.h"
#include "ShardConfig.h"
#include "StdexecPrelude.h"

struct FileInfo;
class DialogView;
class FilePath;
class PersistentStorage;
class ProjectSettings;
class StorageCache;

class Project: public std::enable_shared_from_this<Project>
{
public:
	Project(
		std::shared_ptr<ProjectSettings> settings,
		StorageCache* storageCache,
		const std::string& appUUID,
		bool hasGUI);
	virtual ~Project();

	FilePath getProjectSettingsFilePath() const;
	std::string getDescription() const;

	bool isLoaded() const;
	bool isIndexing() const;
	//! The settings loaded (whatever the index's state); false after a failed load.
	bool isOpen() const;
	//! The index loaded and browsable (it may be outdated).
	bool hasIndex() const;

	bool settingsEqualExceptNameAndLocation(const ProjectSettings& otherSettings) const;
	void setStateOutdated();

	//! Loads the project on the calling thread, asking what it asks and waiting.
	void load(std::shared_ptr<DialogView> dialogView);

	//! The load in its steps, for a pipeline that opens the index off the app's
	//! thread (Application::loadProject): beginLoad and finishLoad where the app's
	//! state is touched, readSettings, resolveInterruptedRun and openIndex where
	//! the files are; a step that returns false ends the load. load() runs them
	//! in a row.
	struct Question
	{
		std::string text;
		std::vector<std::string> options;
	};
	bool beginLoad();
	bool readSettings();
	//! The question an interrupted indexing run leaves (keep its data or discard it).
	std::optional<Question> interruptedRunQuestion() const;
	//! Keeps the interrupted run's data on option 0, else discards it; takes it when
	//! there is no other index. False when the index is in use by another process:
	//! indexInUse() says so.
	bool resolveInterruptedRun(int option);
	static Question indexInUse();
	void openIndex();
	void finishLoad();

	//! Refreshes on the calling thread, asking what it asks and waiting.
	void refresh(std::shared_ptr<DialogView> dialogView, RefreshMode refreshMode);

	//! What a refresh asks before it starts (reindex an outdated index? index with
	//! verbose logging?), for a caller that asks without waiting: option 1 of any
	//! question cancels it. Nothing when there is nothing to refresh.
	struct RefreshPlan
	{
		RefreshMode mode = RefreshMode::NONE;
		bool needsFullRefresh = false;
		bool fullRefresh = false;
		std::vector<Question> questions;
	};
	std::optional<RefreshPlan> planRefresh(RefreshMode refreshMode) const;
	//! The refresh once its questions were answered and none cancelled it.
	void refreshPlanned(std::shared_ptr<DialogView> dialogView, const RefreshPlan& plan);

	//! Distributed indexing: when active, refresh() indexes only this process's
	//! stripe into a standalone shard DB (see ShardConfig). Set before load().
	void setShardConfig(const ShardConfig& config);

	RefreshInfo getRefreshInfo(RefreshMode mode) const;
	std::vector<std::shared_ptr<const SourceGroup>> getSourceGroups() const;

	void buildIndex(RefreshInfo info, std::shared_ptr<DialogView> dialogView);

	//! Stops an indexing run and returns once it ended (it reports how). For the app
	//! closing, and the project going.
	void stopIndexing();

private:
	// What an indexing run does, its steps made (buildIndexImpl) and run in order by
	// a coroutine on the work pool (startIndexing).
	struct IndexingRun;
	void startIndexing(std::shared_ptr<IndexingRun> run);

	// The actual refresh/buildIndex bodies. The public entry points are std::expected exception
	// boundaries around these: any escaping exception becomes a failed IndexingOutcome and a
	// terminal MessageIndexingFinished, so a headless run can never lose its quit event to a
	// throw (the generic task runner would otherwise just log and drop it).
	void refreshImpl(std::shared_ptr<DialogView> dialogView, RefreshMode refreshMode);
	void refreshPlannedImpl(std::shared_ptr<DialogView> dialogView, const RefreshPlan& plan);
	void refreshGuarded(const std::function<void()>& refresh);
	void buildIndexImpl(RefreshInfo info, std::shared_ptr<DialogView> dialogView);

	ShardConfig m_shardConfig;

	enum class ProjectStateType
	{
		PROJECT_STATE_NOT_LOADED,
		PROJECT_STATE_EMPTY,
		PROJECT_STATE_LOADED,
		PROJECT_STATE_OUTDATED,
		PROJECT_STATE_OUTVERSIONED,
		PROJECT_STATE_DB_CORRUPTED
	};

	enum class RefreshStageType
	{
		REFRESHING,
		INDEXING,
		NONE
	};

	Project(const Project&);

	// A run that cannot clear a source group partially asks whether to re-index all;
	// the answer goes on with it on the app's scheduler.
	void askToReindexAll(SourceGroupType type, std::shared_ptr<DialogView> dialogView);
	void reindexAllAnswered(int option, std::shared_ptr<DialogView> dialogView);

	void swapToTempStorage(std::shared_ptr<DialogView> dialogView);
	bool swapToTempStorageFile(
		const FilePath& indexDbFilePath,
		const FilePath& tempIndexDbFilePath,
		std::shared_ptr<DialogView> dialogView);
	static bool moveTempStorageFile(const FilePath& indexDbFilePath, const FilePath& tempIndexDbFilePath);
	void discardTempStorage();

	bool hasCxxSourceGroup() const;

	std::shared_ptr<ProjectSettings> m_settings;
	StorageCache* const m_storageCache;

	ProjectStateType m_state = ProjectStateType::PROJECT_STATE_NOT_LOADED;
	RefreshStageType m_refreshStage = RefreshStageType::NONE;

	std::shared_ptr<PersistentStorage> m_storage;
	std::vector<std::shared_ptr<SourceGroup>> m_sourceGroups;

	std::string m_appUUID;
	bool m_hasGUI;

	// The indexing run, while one runs; the scope waits for it.
	exec::async_scope m_indexing;
	std::weak_ptr<IndexingRun> m_indexingRun;
};

#endif	  // PROJECT_H
