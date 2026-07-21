#ifndef SOURCE_GROUP_SETTINGS_WITH_C_STANDARD_H
#define SOURCE_GROUP_SETTINGS_WITH_C_STANDARD_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>
#endif

SRCTRL_EXPORT class SourceGroupSettingsWithCStandard: public SourceGroupSettingsComponent
{
SRCTRL_EXPORT public:
	static std::string getDefaultCStandard();
	static std::vector<std::string> getAvailableCStandards();

	~SourceGroupSettingsWithCStandard() override = default;

	std::string getCStandard() const;
	void setCStandard(const std::string& standard);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	std::string m_cStandard;
};

#include "SourceGroupSettingsWithCStandard.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_C_STANDARD_H
