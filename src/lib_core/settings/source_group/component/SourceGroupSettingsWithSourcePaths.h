#ifndef SOURCE_GROUP_SETTINGS_WITH_SOURCE_PATHS_H
#define SOURCE_GROUP_SETTINGS_WITH_SOURCE_PATHS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>

#include "FilePath.h"
#endif

SRCTRL_EXPORT class SourceGroupSettingsWithSourcePaths: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithSourcePaths() override = default;

	std::vector<FilePath> getSourcePaths() const;
	std::vector<FilePath> getSourcePathsExpandedAndAbsolute() const;
	void setSourcePaths(const std::vector<FilePath>& sourcePaths);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	std::vector<FilePath> m_sourcePaths;
};

#include "SourceGroupSettingsWithSourcePaths.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_SOURCE_PATHS_H
