#ifndef SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H
#define SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H

#include "FilePath.h"
#include "SourceGroupSettingsComponent.h"

class SourceGroupSettingsWithCxxCMakeBuildDirectory: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithCxxCMakeBuildDirectory() override = default;

	// The CMake source directory (contains CMakeLists.txt + CMakePresets.json).
	FilePath getSourceDirectory() const;
	FilePath getSourceDirectoryExpandedAndAbsolute() const;
	void setSourceDirectory(const FilePath& path);

	// The configure preset name to use (e.g. "vcpkg-debug").
	const std::string& getPresetName() const;
	void setPresetName(const std::string& name);

	// Resolves the binary directory by running cmake -S <sourceDir> --preset <presetName> -N.
	// Returns an empty FilePath on failure.
	FilePath resolveBuildDirectory() const;

	// Optional: restrict indexing to targets matching this glob (e.g. "MyLib*").
	const std::string& getTargetGlob() const;
	void setTargetGlob(const std::string& glob);

	// Optional: CMake configuration name (e.g. "Debug", "Release").
	const std::string& getConfiguration() const;
	void setConfiguration(const std::string& config);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;
	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	FilePath m_sourceDirectory;
	std::string m_presetName;
	std::string m_targetGlob;
	std::string m_configuration;
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H
