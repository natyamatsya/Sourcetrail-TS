#ifndef SOURCE_GROUP_SETTINGS_CXX_MAKE_H
#define SOURCE_GROUP_SETTINGS_CXX_MAKE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsCxxCdb.h"
#include "SourceGroupSettingsWithCxxMakefile.h"

// A compilation database group whose database Sourcetrail writes from what make prints for a
// dry run (make::runDryRun): the Makefile's directory, goals and variables are what is set; the
// database is the group's own, in its dependencies directory, and written again on refresh.
SRCTRL_EXPORT class SourceGroupSettingsCxxMake
	: public SourceGroupSettingsCxxCdb
	, public SourceGroupSettingsWithCxxMakefile
{
public:
	SourceGroupSettingsCxxMake(const std::string& id, const ProjectSettings* projectSettings)
		: SourceGroupSettingsCxxCdb(SourceGroupType::CXX_MAKE, id, projectSettings)
	{
		useGeneratedCompilationDatabase();
	}

	std::shared_ptr<SourceGroupSettings> createCopy() const override
	{
		return std::make_shared<SourceGroupSettingsCxxMake>(*this);
	}

	// Where the dry run's compile commands go.
	FilePath getGeneratedCompilationDatabasePath() const
	{
		return getSourceGroupDependenciesDirectoryPath().concatenate("compile_commands.json");
	}

	// Points the group's compilation database at the generated one (the project's location
	// decides where that is, so again when it changes).
	void useGeneratedCompilationDatabase()
	{
		if (getProjectSettings() != nullptr)
		{
			setCompilationDatabasePath(getGeneratedCompilationDatabasePath());
		}
	}

	void loadSettings(const ConfigManager* config) override
	{
		SourceGroupSettingsCxxCdb::loadSettings(config);
		SourceGroupSettingsWithCxxMakefile::load(config, s_keyPrefix + getId());
		useGeneratedCompilationDatabase();
	}

	void saveSettings(ConfigManager* config) override
	{
		SourceGroupSettingsCxxCdb::saveSettings(config);
		SourceGroupSettingsWithCxxMakefile::save(config, s_keyPrefix + getId());
		// The database is generated where the project is, not a setting.
		config->removeValues(s_keyPrefix + getId() + "/build_file_path");
	}

	bool equalsSettings(const SourceGroupSettingsBase* other) override
	{
		const SourceGroupSettingsCxxMake* otherMake = dynamic_cast<const SourceGroupSettingsCxxMake*>(other);
		if (otherMake == nullptr)
		{
			return false;
		}
		// The database is where the project is, not a setting: settings read back without a
		// project file (those an index stores) are the same.
		SourceGroupSettingsCxxMake same(*otherMake);
		same.setCompilationDatabasePath(getCompilationDatabasePath());
		return SourceGroupSettingsCxxCdb::equalsSettings(&same) && SourceGroupSettingsWithCxxMakefile::equals(&same);
	}
};

#endif	  // SOURCE_GROUP_SETTINGS_CXX_MAKE_H
