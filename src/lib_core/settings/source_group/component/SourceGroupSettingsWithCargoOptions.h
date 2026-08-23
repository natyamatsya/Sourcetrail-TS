#ifndef SOURCE_GROUP_SETTINGS_WITH_CARGO_OPTIONS_H
#define SOURCE_GROUP_SETTINGS_WITH_CARGO_OPTIONS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#include <vector>

#include "FilePath.h"
#endif

// Cargo project-model options for Rust source groups (project model v1:
// feature selection and target triple — see
// context/DESIGN_RUST_PROJECT_MODEL.md; per-target scoping is deferred).
SRCTRL_EXPORT class SourceGroupSettingsWithCargoOptions: public SourceGroupSettingsComponent
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

	// Crate fan-out R1b emits one command per workspace MEMBER, and a member
	// command collects only its own package -- which leaves a `path = "..."`
	// dependency outside the workspace belonging to no command at all, so it is
	// never indexed. Turn this on to have each member command also collect the
	// local crates that live outside the workspace root, i.e. its path
	// dependencies. Off by default: it is extra work per command, and two
	// members sharing one path dependency each collect it (the storage merge
	// dedupes the nodes, but the work is done twice).
	// See context/DESIGN_RUST_CRATE_FANOUT.md.
	bool getRustIndexPathDependencies() const;
	void setRustIndexPathDependencies(bool indexPathDependencies);

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
	bool m_rustIndexPathDependencies = false;
};

#include "SourceGroupSettingsWithCargoOptions.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_CARGO_OPTIONS_H
