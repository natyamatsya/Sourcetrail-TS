#ifndef SOURCE_GROUP_CXX_MAKE_H
#define SOURCE_GROUP_CXX_MAKE_H

#include <expected>
#include <memory>

#include "MakeCompileCommands.h"
#include "SourceGroupCxxCdb.h"

class SourceGroupSettingsCxxMake;

// A compilation database group on the database a Makefile's dry run gives
// (SourceGroupSettingsCxxMake): written before indexing, and on first use when there is none
// yet, then indexed as SourceGroupCxxCdb indexes any.
class SourceGroupCxxMake: public SourceGroupCxxCdb
{
public:
	explicit SourceGroupCxxMake(std::shared_ptr<SourceGroupSettingsCxxMake> settings);

	//! Runs the dry run and writes its compile commands to the group's database (only when they
	//! changed, so its date says when they did). Fails when make cannot run or compiles nothing,
	//! saying why.
	static std::expected<make::DryRun, make::Error> writeCompilationDatabase(SourceGroupSettingsCxxMake& settings);

	std::expected<void, PrepareIndexingError> prepareIndexing() override;
	std::set<FilePath> filterToContainedFilePaths(const std::set<FilePath>& filePaths) const override;
	std::set<FilePath> getAllSourceFilePaths() const override;
	std::shared_ptr<IndexerCommandProvider> getIndexerCommandProvider(const RefreshInfo& info) const override;
	std::vector<std::shared_ptr<IndexerCommand>> getIndexerCommands(const RefreshInfo& info) const override;

private:
	// Writes the database when it is not there yet (a group set up from a project file).
	void ensureCompilationDatabase() const;

	std::shared_ptr<SourceGroupSettingsCxxMake> m_settings;
};

#endif	  // SOURCE_GROUP_CXX_MAKE_H
