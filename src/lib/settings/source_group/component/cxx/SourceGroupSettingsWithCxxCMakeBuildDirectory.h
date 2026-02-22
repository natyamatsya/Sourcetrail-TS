#ifndef SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H
#define SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H

#include "FilePath.h"
#include "SourceGroupSettingsComponent.h"

class SourceGroupSettingsWithCxxCMakeBuildDirectory: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithCxxCMakeBuildDirectory() override = default;

	FilePath getBuildDirectory() const;
	FilePath getBuildDirectoryExpandedAndAbsolute() const;
	void setBuildDirectory(const FilePath& path);

	const std::string& getTargetGlob() const;
	void setTargetGlob(const std::string& glob);

	const std::string& getConfiguration() const;
	void setConfiguration(const std::string& config);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;
	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	FilePath m_buildDirectory;
	std::string m_targetGlob;
	std::string m_configuration;
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H
