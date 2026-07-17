#ifndef SOURCE_GROUP_SETTINGS_WITH_SWIFT_OPTIONS_H
#define SOURCE_GROUP_SETTINGS_WITH_SWIFT_OPTIONS_H

#include <string>
#include <vector>

#include "FilePath.h"
#include "SourceGroupSettingsComponent.h"

// Swift project-model options for Swift source groups (SW5). Mirrors the Rust
// SourceGroupSettingsWithCargoOptions; see context/DESIGN_SWIFT_INDEXER.md and
// context/ROADMAP_SWIFT_INDEXER.md.
class SourceGroupSettingsWithSwiftOptions: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithSwiftOptions() override = default;

	// Extra args appended to `swift build` (e.g. {"--configuration", "release"}).
	const std::vector<std::string>& getSwiftBuildArgs() const;
	void setSwiftBuildArgs(const std::vector<std::string>& args);

	// Toolchain root to build with (contains usr/bin/swift + usr/lib/
	// libIndexStore.dylib). Empty = the default toolchain resolved via xcrun.
	const FilePath& getSwiftToolchainPath() const;
	FilePath getSwiftToolchainPathExpandedAndAbsolute() const;
	void setSwiftToolchainPath(const FilePath& path);

	// An existing index store to read; when set the subprocess SKIPS
	// `swift build` and indexes the store directly (read-only / prebuilt
	// checkout). Empty = build the package.
	const FilePath& getSwiftIndexStorePath() const;
	FilePath getSwiftIndexStorePathExpandedAndAbsolute() const;
	void setSwiftIndexStorePath(const FilePath& path);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	std::vector<std::string> m_swiftBuildArgs;
	FilePath m_swiftToolchainPath;
	FilePath m_swiftIndexStorePath;
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_SWIFT_OPTIONS_H
