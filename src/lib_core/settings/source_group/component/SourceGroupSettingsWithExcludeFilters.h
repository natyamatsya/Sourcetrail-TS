#ifndef SOURCE_GROUP_SETTINGS_WITH_EXCLUDE_FILTERS_H
#define SOURCE_GROUP_SETTINGS_WITH_EXCLUDE_FILTERS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>
#endif

#ifndef SRCTRL_MODULE_PURVIEW
class FilePathFilter;
#endif

SRCTRL_EXPORT class SourceGroupSettingsWithExcludeFilters: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithExcludeFilters() override = default;

	std::vector<std::string> getExcludeFilterStrings() const;
	std::vector<FilePathFilter> getExcludeFiltersExpandedAndAbsolute() const;
	void setExcludeFilterStrings(const std::vector<std::string>& excludeFilters);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	std::vector<FilePathFilter> getFiltersExpandedAndAbsolute(
		const std::vector<std::string>& filterStrings) const;

	std::vector<std::string> m_excludeFilters;
};

#include "SourceGroupSettingsWithExcludeFilters.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_EXCLUDE_FILTERS_H
