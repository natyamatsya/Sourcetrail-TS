#ifndef SOURCE_GROUP_CXX_CDB_H
#define SOURCE_GROUP_CXX_CDB_H

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "CdbLoad.h"
#include "SourceGroup.h"

class FilePath;

namespace clang::tooling
{
class CompilationDatabase;
}


class SourceGroupSettingsCxxCdb;

class SourceGroupCxxCdb: public SourceGroup
{
public:
	SourceGroupCxxCdb(std::shared_ptr<SourceGroupSettingsCxxCdb> settings);

	//! Whether the compilation database at `cdbPath` can be indexed: it loads (see utility::loadCDB)
	//! and at least one of the files its commands compile exists. Files that do not exist beside
	//! ones that do are logged, not an error.
	static std::expected<void, utility::CdbLoadError> checkCompilationDatabase(const FilePath& cdbPath);

	std::expected<void, PrepareIndexingError> prepareIndexing() override;
	std::set<FilePath> filterToContainedFilePaths(const std::set<FilePath>& filePaths) const override;
	std::set<FilePath> getAllSourceFilePaths() const override;
	std::set<FilePath> getAllSourceFilePaths(std::shared_ptr<clang::tooling::CompilationDatabase> cdb) const;
	std::shared_ptr<IndexerCommandProvider> getIndexerCommandProvider(const RefreshInfo& info) const override;
	std::vector<std::shared_ptr<IndexerCommand>> getIndexerCommands(const RefreshInfo& info) const override;
	std::function<void()> getPreIndexWork(std::shared_ptr<StorageProvider> storageProvider, std::shared_ptr<DialogView> dialogView) const override;

private:
	std::shared_ptr<SourceGroupSettings> getSourceGroupSettings() override;
	std::shared_ptr<const SourceGroupSettings> getSourceGroupSettings() const override;
	std::vector<std::string> getBaseCompilerFlags() const;

	std::shared_ptr<SourceGroupSettingsCxxCdb> m_settings;
};

#endif	  // SOURCE_GROUP_CXX_CDB_H
