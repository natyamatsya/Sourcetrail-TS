#ifndef SOURCE_GROUP_RUST_H
#define SOURCE_GROUP_RUST_H

#include <memory>
#include <set>
#include <vector>

#include "SourceGroup.h"

class SourceGroupSettingsRustEmpty;

class SourceGroupRust: public SourceGroup
{
public:
	explicit SourceGroupRust(const std::shared_ptr<SourceGroupSettingsRustEmpty>& settings);

	bool prepareIndexing() override;

	std::set<FilePath> filterToContainedFilePaths(
		const std::set<FilePath>& filePaths) const override;
	std::set<FilePath> getAllSourceFilePaths() const override;
	std::vector<std::shared_ptr<IndexerCommand>> getIndexerCommands(
		const RefreshInfo& info) const override;

protected:
	std::shared_ptr<SourceGroupSettings> getSourceGroupSettings() override;
	std::shared_ptr<const SourceGroupSettings> getSourceGroupSettings() const override;

private:
	std::shared_ptr<SourceGroupSettingsRustEmpty> m_settings;
};

#endif	  // SOURCE_GROUP_RUST_H
