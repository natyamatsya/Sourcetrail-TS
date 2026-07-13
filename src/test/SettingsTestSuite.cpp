#include "Catch2.hpp"

#include "FileSystem.h"
#include "ProjectSettings.h"
#include "Settings.h"
#include "SourceGroupSettings.h"
#include "SourceGroupSettingsWithSourcePaths.h"

namespace
{
class TestSettings: public Settings
{
public:
	bool getBool() const
	{
		return getValue<bool>("Bool", false);
	}

	bool setBool(bool value)
	{
		return setValue<bool>("Bool", value);
	}

	int getInt() const
	{
		return getValue<int>("Int", -1);
	}

	bool setInt(int value)
	{
		return setValue<int>("Int", value);
	}

	float getFloat() const
	{
		return getValue<float>("Float", 0.01f);
	}

	bool setFloat(float value)
	{
		return setValue<float>("Float", value);
	}

	std::string getString() const
	{
		return getValue<std::string>("String", "<empty>");
	}

	bool setString(const std::string& value)
	{
		return setValue<std::string>("String", value);
	}

	bool getNewBool() const
	{
		return getValue<bool>("NewBool", false);
	}

	bool setNewBool(bool value)
	{
		return setValue<bool>("NewBool", value);
	}
};
}	 // namespace

TEST_CASE("settings get loaded from file")
{
	TestSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.xml")));
}

TEST_CASE("settings get not loaded from file")
{
	TestSettings settings;
	REQUIRE(!settings.load(FilePath("data/SettingsTestSuite/wrong_settings.xml")));
}

TEST_CASE("settings get loaded value")
{
	TestSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.xml")));

	REQUIRE(settings.getBool() == true);
	REQUIRE(settings.getInt() == 42);
	REQUIRE(settings.getFloat() == 3.1416f);
	REQUIRE(settings.getString() == "Hello World!");
}

TEST_CASE("settings get default value when not loaded")
{
	TestSettings settings;
	REQUIRE(settings.getBool() == false);
	REQUIRE(settings.getInt() == -1);
	REQUIRE(settings.getFloat() == 0.01f);
	REQUIRE(settings.getString() == "<empty>");
}

TEST_CASE("settings get default value when wrongly loaded")
{
	TestSettings settings;
	REQUIRE(!settings.load(FilePath("data/SettingsTestSuite/wrong_settings.xml")));

	REQUIRE(settings.getBool() == false);
	REQUIRE(settings.getInt() == -1);
	REQUIRE(settings.getFloat() == 0.01f);
	REQUIRE(settings.getString() == "<empty>");
}

TEST_CASE("settings get default value after clearing")
{
	TestSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.xml")));

	settings.clear();
	REQUIRE(settings.getBool() == false);
	REQUIRE(settings.getInt() == -1);
	REQUIRE(settings.getFloat() == 0.01f);
	REQUIRE(settings.getString() == "<empty>");
}

TEST_CASE("settings can be set when not loaded")
{
	TestSettings settings;

	REQUIRE(settings.setBool(false));
	REQUIRE(settings.getBool() == false);

	REQUIRE(settings.setInt(2));
	REQUIRE(settings.getInt() == 2);

	REQUIRE(settings.setFloat(2.5f));
	REQUIRE(settings.getFloat() == 2.5f);

	REQUIRE(settings.setString("foo"));
	REQUIRE(settings.getString() == "foo");
}

TEST_CASE("settings can be replaced when loaded")
{
	TestSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.xml")));

	REQUIRE(settings.setBool(false));
	REQUIRE(settings.getBool() == false);

	REQUIRE(settings.setInt(2));
	REQUIRE(settings.getInt() == 2);

	REQUIRE(settings.setFloat(2.5f));
	REQUIRE(settings.getFloat() == 2.5f);

	REQUIRE(settings.setString("foo"));
	REQUIRE(settings.getString() == "foo");
}

TEST_CASE("settings can be added when loaded")
{
	TestSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.xml")));

	REQUIRE(settings.getNewBool() == false);
	REQUIRE(settings.setNewBool(true));
	REQUIRE(settings.getNewBool() == true);
}

TEST_CASE("load project settings from file")
{
	ProjectSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.xml")));
}

TEST_CASE("load source path from file")
{
	ProjectSettings projectSettings;
	REQUIRE(projectSettings.load(FilePath("data/SettingsTestSuite/settings.xml")));

	const std::vector<std::shared_ptr<SourceGroupSettings>> allSettings =
		projectSettings.getAllSourceGroupSettings();
	REQUIRE(1 == allSettings.size());

	// If the type string in the fixture no longer maps, the factory returns a
	// SourceGroupSettingsUnloadable and this cast yields null -- fail, don't crash.
	std::shared_ptr<SourceGroupSettingsWithSourcePaths> sourceGroupSettings =
		std::dynamic_pointer_cast<SourceGroupSettingsWithSourcePaths>(allSettings.front());
	REQUIRE(sourceGroupSettings != nullptr);

	std::vector<FilePath> paths = sourceGroupSettings->getSourcePaths();

	REQUIRE(paths.size() == 2);
	REQUIRE(paths[0].str() == "src");
	REQUIRE(paths[1].str() == "test");
}

TEST_CASE("migrate legacy project file to toml")
{
	// Stage a legacy .srctrlprj (XML) from the fixture; the fixture's source
	// group has two source paths, so this also proves repeated-value keys
	// survive the TOML round-trip.
	const FilePath legacyPath("data/SettingsTestSuite/migrate_me.srctrlprj");
	const FilePath tomlPath("data/SettingsTestSuite/migrate_me.srctrl.toml");
	FileSystem::remove(legacyPath);
	FileSystem::remove(tomlPath);
	FileSystem::copyFile(FilePath("data/SettingsTestSuite/settings.xml"), legacyPath);

	const FilePath migratedPath = ProjectSettings::migrateLegacyProjectFile(legacyPath);

	REQUIRE(migratedPath == tomlPath);
	// Fresh FilePath instances: FilePath caches exists(), and both paths were
	// queried above (via FileSystem::remove) while the files were absent.
	REQUIRE(FilePath(tomlPath.str()).exists());
	REQUIRE(!FilePath(legacyPath.str()).exists());	  // migration runs exactly once

	ProjectSettings projectSettings;
	REQUIRE(projectSettings.load(migratedPath));

	const std::vector<std::shared_ptr<SourceGroupSettings>> allSettings =
		projectSettings.getAllSourceGroupSettings();
	REQUIRE(1 == allSettings.size());

	std::shared_ptr<SourceGroupSettingsWithSourcePaths> sourceGroupSettings =
		std::dynamic_pointer_cast<SourceGroupSettingsWithSourcePaths>(allSettings.front());
	REQUIRE(sourceGroupSettings != nullptr);

	std::vector<FilePath> paths = sourceGroupSettings->getSourcePaths();
	REQUIRE(paths.size() == 2);
	REQUIRE(paths[0].str() == "src");
	REQUIRE(paths[1].str() == "test");

	// A stale legacy path (already migrated) resolves to the TOML sibling.
	REQUIRE(ProjectSettings::migrateLegacyProjectFile(legacyPath) == tomlPath);

	// Non-legacy paths pass through unchanged.
	REQUIRE(ProjectSettings::migrateLegacyProjectFile(tomlPath) == tomlPath);

	FileSystem::remove(tomlPath);
}
