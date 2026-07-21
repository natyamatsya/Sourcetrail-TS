#ifndef SOURCE_GROUP_SETTINGS_WITH_CPP_STANDARD_H
#define SOURCE_GROUP_SETTINGS_WITH_CPP_STANDARD_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>
#endif

SRCTRL_EXPORT class SourceGroupSettingsWithCppStandard: public SourceGroupSettingsComponent
{
public:
	static std::string getDefaultCppStandard();
	static std::vector<std::string> getAvailableCppStandards();

	~SourceGroupSettingsWithCppStandard() override = default;

	std::string getCppStandard() const;
	void setCppStandard(const std::string& standard);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	std::string m_cppStandard;
};

#include "SourceGroupSettingsWithCppStandard.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_CPP_STANDARD_H
