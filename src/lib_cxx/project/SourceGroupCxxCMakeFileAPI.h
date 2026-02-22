#ifndef SOURCE_GROUP_CXX_CMAKE_FILE_API_H
#define SOURCE_GROUP_CXX_CMAKE_FILE_API_H

#include <memory>
#include <set>
#include <vector>

#include "SourceGroup.h"

class SourceGroupSettingsCxxCMakeFileAPI;

class SourceGroupCxxCMakeFileAPI : public SourceGroup
{
public:
	explicit SourceGroupCxxCMakeFileAPI(
		std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings);

	bool prepareIndexing() override;
	std::set<FilePath> filterToContainedFilePaths(
		const std::set<FilePath>& filePaths) const override;
	std::set<FilePath> getAllSourceFilePaths() const override;
	std::shared_ptr<IndexerCommandProvider> getIndexerCommandProvider(
		const RefreshInfo& info) const override;
	std::vector<std::shared_ptr<IndexerCommand>> getIndexerCommands(
		const RefreshInfo& info) const override;

private:
	std::shared_ptr<SourceGroupSettings> getSourceGroupSettings() override;
	std::shared_ptr<const SourceGroupSettings> getSourceGroupSettings() const override;

	std::vector<std::string> getBaseCompilerFlags() const;

	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> m_settings;
};

#endif	  // SOURCE_GROUP_CXX_CMAKE_FILE_API_H
