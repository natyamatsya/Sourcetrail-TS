#ifndef SOURCE_GROUP_SWIFT_H
#define SOURCE_GROUP_SWIFT_H

#include "language_packages.h"

#if BUILD_SWIFT_LANGUAGE_PACKAGE

#include <memory>
#include <set>
#include <vector>

#include "SourceGroup.h"

class SourceGroupSettingsSwiftEmpty;

class SourceGroupSwift: public SourceGroup
{
public:
	explicit SourceGroupSwift(const std::shared_ptr<SourceGroupSettingsSwiftEmpty>& settings);

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
	std::shared_ptr<SourceGroupSettingsSwiftEmpty> m_settings;
};

#endif	  // BUILD_SWIFT_LANGUAGE_PACKAGE

#endif	  // SOURCE_GROUP_SWIFT_H
