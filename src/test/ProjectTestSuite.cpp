#include "Catch2.hpp"

#include <fstream>
#include <memory>
#include <optional>

#include "DialogView.h"
#include "FilePath.h"
#include "IndexDatabaseFiles.h"
#include "IndexingStateMachine.h"
#include "Project.h"
#include "ProjectSettings.h"
#include "SqliteIndexStorage.h"
#include "StorageConnection.h"
#include "TestTempDirectory.h"

namespace
{
// A project with an index and what an indexing run left behind.
struct LeftRun
{
	TestTempDirectory directory {"ProjectTestSuite"};
	FilePath settingsPath {directory.getFilePath("project.srctrl.toml")};
	std::shared_ptr<ProjectSettings> settings;
	std::shared_ptr<Project> project;

	explicit LeftRun(std::optional<IndexingRunMark> mark)
	{
		std::ofstream {settingsPath.str()} << "version = '8'\n";
		settings = std::make_shared<ProjectSettings>(settingsPath);
		writeIndex(settings->getDBFilePath(), 5, IndexingRunMark::Finished);
		writeIndex(settings->getTempDBFilePath(), 3, mark);
		project = std::make_shared<Project>(settings, nullptr, "ProjectTestSuite", false);
		REQUIRE(project->readSettings());
	}

	static void writeIndex(const FilePath& path, int nodeCount, std::optional<IndexingRunMark> mark)
	{
		StorageConnection connection(path);
		SqliteIndexStorage storage(connection);
		storage.setup();
		storage.setVersion(SqliteIndexStorage::getStorageVersion());
		if (mark)
		{
			storage.insertOrUpdateMetaValue(index_database::runMarkKey, std::string(to_std_sv(*mark)));
		}
		storage.beginTransaction();
		for (int i = 0; i < nodeCount; i++)
		{
			storage.addNode(StorageNodeData(NODE_CLASS, "node" + std::to_string(i)));
		}
		storage.commitTransaction();
	}

	int indexNodeCount() const
	{
		StorageConnection connection(settings->getDBFilePath());
		const SqliteIndexStorage storage(connection);
		return storage.getNodeCount();
	}

	// The option the question labels so.
	int option(const std::string& label) const
	{
		const std::optional<Project::Question> question = project->interruptedRunQuestion();
		REQUIRE(question);
		for (std::size_t i = 0; i < question->options.size(); i++)
		{
			if (question->options[i] == label)
			{
				return static_cast<int>(i);
			}
		}
		FAIL("no option " + label);
		return -1;
	}
};
}	 // namespace

// A clean quit during a full reindex left the partial database; the reopen said
// Sourcetrail had closed unexpectedly, and its default kept the partial data.
TEST_CASE("a run stopped by a quit is named so, and its question defaults to the complete index")
{
	const LeftRun left(IndexingRunMark::Stopped);

	const std::optional<Project::Question> question = left.project->interruptedRunQuestion();

	REQUIRE(question);
	REQUIRE(question->text.find("closed while indexing") != std::string::npos);
	REQUIRE(question->text.find("unexpectedly") == std::string::npos);
	REQUIRE(question->options.front() == "Discard and Restore");
}

TEST_CASE("a crashed run's question defaults to the complete index")
{
	const LeftRun left(IndexingRunMark::Running);

	const std::optional<Project::Question> question = left.project->interruptedRunQuestion();

	REQUIRE(question);
	REQUIRE(question->text.find("unexpectedly") != std::string::npos);
	REQUIRE(question->options.front() == "Discard and Restore");
}

TEST_CASE("a finished run's question defaults to its index")
{
	const LeftRun left(IndexingRunMark::Finished);

	const std::optional<Project::Question> question = left.project->interruptedRunQuestion();

	REQUIRE(question);
	REQUIRE(question->options.front() == "Use the New Index");
}

TEST_CASE("discarding a stopped run's data keeps the complete index")
{
	const LeftRun left(IndexingRunMark::Stopped);

	REQUIRE(left.project->resolveInterruptedRun(left.option("Discard and Restore")));

	REQUIRE(left.indexNodeCount() == 5);
	REQUIRE_FALSE(left.settings->getTempDBFilePath().recheckExists());
}

TEST_CASE("no answer about a stopped run keeps the complete index")
{
	const LeftRun left(IndexingRunMark::Stopped);

	REQUIRE(left.project->resolveInterruptedRun(-1));

	REQUIRE(left.indexNodeCount() == 5);
}

TEST_CASE("keeping a stopped run's data takes it")
{
	const LeftRun left(IndexingRunMark::Stopped);

	REQUIRE(left.project->resolveInterruptedRun(left.option("Keep and Continue")));

	REQUIRE(left.indexNodeCount() == 3);
	REQUIRE_FALSE(left.settings->getTempDBFilePath().recheckExists());
}

TEST_CASE("using a finished run's index takes it")
{
	const LeftRun left(IndexingRunMark::Finished);

	REQUIRE(left.project->resolveInterruptedRun(left.option("Use the New Index")));

	REQUIRE(left.indexNodeCount() == 3);
}

namespace
{
class RecordingDialogView: public DialogView
{
public:
	RecordingDialogView(): DialogView(UseCase::INDEXING, nullptr) {}

	void clearDialogs() override
	{
		cleared++;
	}

	int cleared = 0;
};
}	 // namespace

// A project whose index files cannot be made (a 240-character name): the reindex
// failed with "std::exception" behind a progress dialog that stayed up, and asked for
// a reindex again.
TEST_CASE("a refresh no reindex can help ends at once, with the dialogs gone and the reason")
{
	const TestTempDirectory directory {"ProjectTestSuite"};
	const FilePath settingsPath(directory.getFilePath(std::string(240, 'p') + ".srctrl.toml"));
	std::ofstream {settingsPath.str()} << "version = '8'\n";
	const auto project = std::make_shared<Project>(
		std::make_shared<ProjectSettings>(settingsPath), nullptr, "ProjectTestSuite", false);
	REQUIRE(project->readSettings());
	project->openIndex();
	const auto dialogView = std::make_shared<RecordingDialogView>();

	REQUIRE_FALSE(project->planRefresh(RefreshMode::ALL_FILES));	// no question to ask
	project->refresh(dialogView, RefreshMode::ALL_FILES);

	REQUIRE(dialogView->cleared == 1);
	REQUIRE_FALSE(project->isIndexing());
	REQUIRE(IndexingStateMachine::instance()->snapshot().context.find("too long") != std::string::npos);
}
