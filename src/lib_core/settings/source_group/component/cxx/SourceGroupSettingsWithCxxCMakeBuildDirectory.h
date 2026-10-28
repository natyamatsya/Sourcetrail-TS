#ifndef SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H
#define SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#include <vector>

#include "FilePath.h"
#endif

SRCTRL_EXPORT class SourceGroupSettingsWithCxxCMakeBuildDirectory: public SourceGroupSettingsComponent
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

	// The build directory to use without a preset (a project without CMakePresets.json):
	// configured with `cmake -S <sourceDir> -B <buildDir>`, or already configured.
	FilePath getBuildDirectory() const;
	FilePath getBuildDirectoryExpandedAndAbsolute() const;
	void setBuildDirectory(const FilePath& path);

	// The build directory used: the preset's binaryDir, or without a preset the build
	// directory above. Returns an empty FilePath when it cannot be resolved.
	FilePath resolveBuildDirectory() const;

	// Optional: restrict indexing to targets matching this glob (e.g. "MyLib*").
	const std::string& getTargetGlob() const;
	void setTargetGlob(const std::string& glob);

	// Without a preset: what the configure adds to `cmake -S <source> -B <build>`, one
	// argument each: cache variables (-DBUILD_TESTS=OFF), a generator (-G Ninja), ...
	const std::vector<std::string>& getCMakeArguments() const;
	void setCMakeArguments(const std::vector<std::string>& arguments);

	// With a target filter: also index the targets the matched ones depend on (link to,
	// or need built first), transitively.
	bool getIndexLinkedTargets() const;
	void setIndexLinkedTargets(bool index);

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
	FilePath m_buildDirectory;
	std::string m_targetGlob;
	std::string m_configuration;
	std::vector<std::string> m_cmakeArguments;
	bool m_indexLinkedTargets = false;
};

#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_CXX_CMAKE_BUILD_DIRECTORY_H
