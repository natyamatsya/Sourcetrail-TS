#ifndef SOURCE_GROUP_SETTINGS_WITH_CARGO_OPTIONS_H
#define SOURCE_GROUP_SETTINGS_WITH_CARGO_OPTIONS_H

#include <string>
#include <vector>

#include "FilePath.h"
#include "SourceGroupSettingsComponent.h"

// Cargo project-model options for Rust source groups (project model v1:
// feature selection and target triple — see
// context/DESIGN_RUST_PROJECT_MODEL.md; per-target scoping is deferred).
class SourceGroupSettingsWithCargoOptions: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithCargoOptions() override = default;

	// The cargo project/workspace directory (contains Cargo.toml). Optional:
	// when empty, the working directory is inferred from the project file's
	// directory or the source paths (see SourceGroupRust::getIndexerCommands).
	// Mirrors the CMake File API group's explicit source_directory.
	const FilePath& getCargoWorkspaceDirectory() const;
	FilePath getCargoWorkspaceDirectoryExpandedAndAbsolute() const;
	void setCargoWorkspaceDirectory(const FilePath& path);

	const std::vector<std::string>& getCargoFeatures() const;
	void setCargoFeatures(const std::vector<std::string>& features);

	bool getCargoAllFeatures() const;
	void setCargoAllFeatures(bool allFeatures);

	bool getCargoNoDefaultFeatures() const;
	void setCargoNoDefaultFeatures(bool noDefaultFeatures);

	const std::string& getCargoTargetTriple() const;
	void setCargoTargetTriple(const std::string& targetTriple);

	// Implicit generic-specialization node scope ("off"/"local"/"all";
	// §7 of context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md). Default "local".
	const std::string& getRustSpecializationScope() const;
	void setRustSpecializationScope(const std::string& scope);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	FilePath m_cargoWorkspaceDirectory;
	std::vector<std::string> m_cargoFeatures;
	bool m_cargoAllFeatures = false;
	bool m_cargoNoDefaultFeatures = false;
	std::string m_cargoTargetTriple;
	std::string m_rustSpecializationScope = "local";
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_CARGO_OPTIONS_H
