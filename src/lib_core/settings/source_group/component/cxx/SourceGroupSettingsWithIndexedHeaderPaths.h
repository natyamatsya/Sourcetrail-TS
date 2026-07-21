#ifndef SOURCE_GROUP_SETTINGS_WITH_INDEXED_HEADER_PATHS_H
#define SOURCE_GROUP_SETTINGS_WITH_INDEXED_HEADER_PATHS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>
#endif

#ifndef SRCTRL_MODULE_PURVIEW
class FilePath;
#endif

SRCTRL_EXPORT class SourceGroupSettingsWithIndexedHeaderPaths: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithIndexedHeaderPaths() override = default;

	std::vector<FilePath> getIndexedHeaderPaths() const;
	std::vector<FilePath> getIndexedHeaderPathsExpandedAndAbsolute() const;
	void setIndexedHeaderPaths(const std::vector<FilePath>& indexedHeaderPaths);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	std::vector<FilePath> m_indexedHeaderPaths;
};

#include "SourceGroupSettingsWithIndexedHeaderPaths.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_INDEXED_HEADER_PATHS_H
