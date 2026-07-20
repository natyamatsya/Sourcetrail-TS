#ifndef SOURCE_GROUP_ZIG_H
#define SOURCE_GROUP_ZIG_H

#include <memory>
#include <set>
#include <vector>

#include "SourceGroup.h"

class SourceGroupSettingsZigEmpty;

class SourceGroupZig: public SourceGroup
{
public:
	explicit SourceGroupZig(const std::shared_ptr<SourceGroupSettingsZigEmpty>& settings);

	std::expected<void, PrepareIndexingError> prepareIndexing() override;

	std::set<FilePath> filterToContainedFilePaths(
		const std::set<FilePath>& filePaths) const override;
	std::set<FilePath> getAllSourceFilePaths() const override;
	std::vector<std::shared_ptr<IndexerCommand>> getIndexerCommands(
		const RefreshInfo& info) const override;

protected:
	std::shared_ptr<SourceGroupSettings> getSourceGroupSettings() override;
	std::shared_ptr<const SourceGroupSettings> getSourceGroupSettings() const override;

private:
	std::shared_ptr<SourceGroupSettingsZigEmpty> m_settings;
};

#endif	  // SOURCE_GROUP_ZIG_H
