#include "Catch2.hpp"

// classic headers (entities not exported): stay textual
#include "SourceGroupSettingsWithSourcePaths.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ProjectSettings.h"
#include "Settings.h"
#include "SourceGroupSettings.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
import srctrl.file;   // FilePath used directly in the source-path tests
#endif

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
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.toml")));
}

TEST_CASE("settings get not loaded from file")
{
	TestSettings settings;
	REQUIRE(!settings.load(FilePath("data/SettingsTestSuite/wrong_settings.toml")));
}

TEST_CASE("settings get loaded value")
{
	TestSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.toml")));

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
	REQUIRE(!settings.load(FilePath("data/SettingsTestSuite/wrong_settings.toml")));

	REQUIRE(settings.getBool() == false);
	REQUIRE(settings.getInt() == -1);
	REQUIRE(settings.getFloat() == 0.01f);
	REQUIRE(settings.getString() == "<empty>");
}

TEST_CASE("settings get default value after clearing")
{
	TestSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.toml")));

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
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.toml")));

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
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.toml")));

	REQUIRE(settings.getNewBool() == false);
	REQUIRE(settings.setNewBool(true));
	REQUIRE(settings.getNewBool() == true);
}

TEST_CASE("load project settings from file")
{
	ProjectSettings settings;
	REQUIRE(settings.load(FilePath("data/SettingsTestSuite/settings.toml")));
}

TEST_CASE("load source path from file")
{
	ProjectSettings projectSettings;
	REQUIRE(projectSettings.load(FilePath("data/SettingsTestSuite/settings.toml")));

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

TEST_CASE("project settings refuse a project file without a version this Sourcetrail reads")
{
	// version = '999', 'banana', -1 or none loaded without a word, and the wizard's Save rewrote
	// the file as version 8.
	const auto check = [](const std::string& text) {
		ProjectSettings settings;
		settings.loadFromString(text);
		return settings.checkVersion();
	};
	using enum ProjectVersionError::Code;

	REQUIRE(check("version = '8'\n").value() == ProjectSettings::VERSION);
	REQUIRE(check("version = 8\n").value() == ProjectSettings::VERSION);
	REQUIRE(check("version = '3'\n").value() == 3);	// older ones open as before

	const auto newer = check("version = '999'\n");
	REQUIRE(newer.error().code == Newer);
	REQUIRE(newer.error().message.find("newer Sourcetrail") != std::string::npos);
	REQUIRE(newer.error().message.find("999") != std::string::npos);

	REQUIRE(check("version = '99999999999999999999999'\n").error().code == Newer);
	REQUIRE(check("version = 'banana'\n").error().code == NotAVersion);
	REQUIRE(check("version = -1\n").error().code == NotAVersion);
	REQUIRE(check("version = '-1'\n").error().code == NotAVersion);
	REQUIRE(check("version = '0'\n").error().code == NotAVersion);
	REQUIRE(check("version = '8x'\n").error().code == NotAVersion);
	REQUIRE(check("[[source_groups]]\nid = 'a'\n").error().code == Missing);
}
