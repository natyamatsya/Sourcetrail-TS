#ifndef SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_H
#define SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>
#endif

SRCTRL_EXPORT class SourceGroupSettingsWithSourceExtensions: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithSourceExtensions() override = default;

	std::vector<std::string> getSourceExtensions() const;
	void setSourceExtensions(const std::vector<std::string>& sourceExtensions);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	virtual std::vector<std::string> getDefaultSourceExtensions() const = 0;

	std::vector<std::string> m_sourceExtensions;
};

#include "SourceGroupSettingsWithSourceExtensions.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_H
