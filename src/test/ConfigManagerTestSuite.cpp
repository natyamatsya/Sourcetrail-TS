#include "Catch2.hpp"

#include "ConfigManager.h"
#include "TextAccess.h"

using namespace std;

namespace
{
std::shared_ptr<TextAccess> getConfigTextAccess()
{
	std::string text =
		"<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
		"<config>\n"
		"	<path>\n"
		"		<to>\n"
		"			<bool_that_is_false>0</bool_that_is_false>\n"
		"			<bool_that_is_true>1</bool_that_is_true>\n"
		"			<single_value>42</single_value>\n"
		"		</to>\n"
		"	</path>\n"
		"	<paths>\n"
		"		<nopath>4</nopath>\n"
		"		<path>2</path>\n"
		"		<path>5</path>\n"
		"		<path>8</path>\n"
		"	</paths>\n"
		"</config>\n";
	return TextAccess::createFromString(text);
}

const string SPECIAL_CHARACTER_UTF8_LOWER_UE = {'\xC3', '\xBC'};

}	 // namespace

TEST_CASE("config manager returns true when key is found")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());

	float value;
	bool success = config->getValue("path/to/single_value", value);

	REQUIRE(success);
}


TEST_CASE("config manager returns false when key is not found")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());

	float value;
	bool success = config->getValue("path/to/nowhere", value);

	REQUIRE(!success);
}


TEST_CASE("config manager returns correct string for key")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());

	std::string value;
	config->getValue("path/to/single_value", value);

	REQUIRE("42" == value);
}


TEST_CASE("config manager returns correct float for key")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());

	float value;
	config->getValue("path/to/single_value", value);

	REQUIRE(value == Catch2::Approx(42.0f));
}

TEST_CASE("config manager returns correct bool for key if value is true")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());

	float value;
	bool success(config->getValue("path/to/bool_that_is_true", value));

	REQUIRE(success);
	REQUIRE(value);
}

TEST_CASE("config manager returns correct bool for key if value is false")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());

	float value;
	bool success(config->getValue("path/to/bool_that_is_false", value));

	REQUIRE(success);
	REQUIRE(!value);
}

TEST_CASE("config manager adds new key when empty")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createEmpty();

	config->setValue("path/to/true_bool", true);

	bool value = false;
	bool success(config->getValue("path/to/true_bool", value));

	REQUIRE(success);
	REQUIRE(value);
}

TEST_CASE("config manager adds new key when not empty")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());

	config->setValue("path/to/true_bool", true);

	bool value = false;
	bool success(config->getValue("path/to/true_bool", value));

	REQUIRE(success);
	REQUIRE(value);
}

TEST_CASE("config manager returns correct list for key")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());

	std::vector<int> values;

	bool success(config->getValues("paths/path", values));

	REQUIRE(success);
	REQUIRE(values.size() == 3);
	REQUIRE(values[0] == 2);
	REQUIRE(values[1] == 5);
	REQUIRE(values[2] == 8);
}

TEST_CASE("config manager save and load configuration and compare")
{
	const FilePath path("data/ConfigManagerTestSuite/temp.xml");

	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(getConfigTextAccess());
	config->save(path.str());
	std::shared_ptr<ConfigManager> config2 = ConfigManager::createAndLoad(
		TextAccess::createFromFile(path));
	REQUIRE(config->toString() == config2->toString());
}

TEST_CASE("config manager loads special character")
{
	std::shared_ptr<ConfigManager> config = ConfigManager::createAndLoad(
		TextAccess::createFromFile(FilePath("data/ConfigManagerTestSuite/test_data.xml")));
	std::string loadedSpecialCharacter;
	config->getValue("path/to/special_character", loadedSpecialCharacter);

	REQUIRE(loadedSpecialCharacter.size() == 2);
	REQUIRE(loadedSpecialCharacter == SPECIAL_CHARACTER_UTF8_LOWER_UE);
		 // special character needs to be encoded as ASCII (UTF-8?) code because
		 // otherwise cxx compiler may be complaining
}

TEST_CASE("config manager save and load special character and compare")
{
	const FilePath path("data/ConfigManagerTestSuite/temp.xml");
	std::string specialCharacter;
	specialCharacter = SPECIAL_CHARACTER_UTF8_LOWER_UE;

	std::shared_ptr<ConfigManager> config = ConfigManager::createEmpty();
	config->setValue("path/to/special_character", specialCharacter);
	config->save(path.str());

	std::shared_ptr<ConfigManager> config2 = ConfigManager::createAndLoad(
		TextAccess::createFromFile(path));
	REQUIRE(config->toString() == config2->toString());
}

TEST_CASE("config manager loads simple TOML values")
{
	const std::string toml =
		"version = 8\n"
		"description = \"My Project\"\n";

	std::shared_ptr<ConfigManager> config =
		ConfigManager::createAndLoad(TextAccess::createFromString(toml));

	int version = 0;
	REQUIRE(config->getValue("version", version));
	REQUIRE(version == 8);

	std::string description;
	REQUIRE(config->getValue("description", description));
	REQUIRE(description == "My Project");
}

TEST_CASE("config manager loads TOML nested table")
{
	const std::string toml =
		"[cross_compilation.target]\n"
		"arch = \"x86_64\"\n"
		"sys = \"linux\"\n";

	std::shared_ptr<ConfigManager> config =
		ConfigManager::createAndLoad(TextAccess::createFromString(toml));

	std::string arch;
	REQUIRE(config->getValue("cross_compilation/target/arch", arch));
	REQUIRE(arch == "x86_64");

	std::string sys;
	REQUIRE(config->getValue("cross_compilation/target/sys", sys));
	REQUIRE(sys == "linux");
}

TEST_CASE("config manager loads TOML plain array")
{
	const std::string toml =
		"[group]\n"
		"source_paths = [\"src/\", \"include/\"]\n";

	std::shared_ptr<ConfigManager> config =
		ConfigManager::createAndLoad(TextAccess::createFromString(toml));

	std::vector<std::string> paths;
	REQUIRE(config->getValues("group/source_paths/source_path", paths));
	REQUIRE(paths.size() == 2);
	REQUIRE(paths[0] == "src/");
	REQUIRE(paths[1] == "include/");
}

TEST_CASE("config manager loads TOML source_groups array of tables with UUID key mapping")
{
	const std::string toml =
		"version = 8\n"
		"\n"
		"[[source_groups]]\n"
		"id = \"aaaaaaaa-0000-0000-0000-000000000001\"\n"
		"name = \"C++ Source Group\"\n"
		"type = \"C++ Source Group\"\n"
		"status = \"enabled\"\n"
		"cpp_standard = \"c++20\"\n"
		"\n"
		"[source_groups.cross_compilation]\n"
		"target_options_enabled = \"0\"\n"
		"\n"
		"[source_groups.cross_compilation.target]\n"
		"arch = \"x86_64\"\n"
		"sys = \"unknown\"\n";

	std::shared_ptr<ConfigManager> config =
		ConfigManager::createAndLoad(TextAccess::createFromString(toml));

	const std::string prefix =
		"source_groups/source_group_aaaaaaaa-0000-0000-0000-000000000001";

	std::string name;
	REQUIRE(config->getValue(prefix + "/name", name));
	REQUIRE(name == "C++ Source Group");

	std::string type;
	REQUIRE(config->getValue(prefix + "/type", type));
	REQUIRE(type == "C++ Source Group");

	std::string cppStd;
	REQUIRE(config->getValue(prefix + "/cpp_standard", cppStd));
	REQUIRE(cppStd == "c++20");

	std::string arch;
	REQUIRE(config->getValue(prefix + "/cross_compilation/target/arch", arch));
	REQUIRE(arch == "x86_64");
}

TEST_CASE("config manager getSublevelKeys works for TOML source_groups")
{
	const std::string toml =
		"[[source_groups]]\n"
		"id = \"aaaaaaaa-0000-0000-0000-000000000001\"\n"
		"name = \"Group A\"\n"
		"type = \"C++ Source Group\"\n"
		"status = \"enabled\"\n"
		"\n"
		"[[source_groups]]\n"
		"id = \"bbbbbbbb-0000-0000-0000-000000000002\"\n"
		"name = \"Group B\"\n"
		"type = \"C++ Source Group\"\n"
		"status = \"enabled\"\n";

	std::shared_ptr<ConfigManager> config =
		ConfigManager::createAndLoad(TextAccess::createFromString(toml));

	const std::vector<std::string> keys = config->getSublevelKeys("source_groups");
	REQUIRE(keys.size() == 2);
}

TEST_CASE("config manager TOML save and reload round-trip for simple values")
{
	const FilePath path("data/ConfigManagerTestSuite/temp.srctrl.toml");

	std::shared_ptr<ConfigManager> config = ConfigManager::createEmpty();
	config->setValue("version", 8);
	config->setValue("description", std::string("My Project"));
	config->setValue("nested/key", std::string("value"));
	REQUIRE(config->saveToml(path.str()));

	std::shared_ptr<ConfigManager> config2 =
		ConfigManager::createAndLoad(TextAccess::createFromFile(path));

	int version = 0;
	REQUIRE(config2->getValue("version", version));
	REQUIRE(version == 8);

	std::string desc;
	REQUIRE(config2->getValue("description", desc));
	REQUIRE(desc == "My Project");

	std::string nested;
	REQUIRE(config2->getValue("nested/key", nested));
	REQUIRE(nested == "value");
}

TEST_CASE("config manager TOML save and reload round-trip for source_groups")
{
	const FilePath path("data/ConfigManagerTestSuite/temp_sg.srctrl.toml");

	std::shared_ptr<ConfigManager> config = ConfigManager::createEmpty();
	const std::string prefix =
		"source_groups/source_group_aaaaaaaa-0000-0000-0000-000000000001";
	config->setValue(prefix + "/name", std::string("C++ Source Group"));
	config->setValue(prefix + "/type", std::string("C++ Source Group"));
	config->setValue(prefix + "/status", std::string("enabled"));
	config->setValue(prefix + "/cpp_standard", std::string("c++20"));
	config->setValue(prefix + "/cross_compilation/target/arch", std::string("x86_64"));
	REQUIRE(config->saveToml(path.str()));

	std::shared_ptr<ConfigManager> config2 =
		ConfigManager::createAndLoad(TextAccess::createFromFile(path));

	std::string name;
	REQUIRE(config2->getValue(prefix + "/name", name));
	REQUIRE(name == "C++ Source Group");

	std::string arch;
	REQUIRE(config2->getValue(prefix + "/cross_compilation/target/arch", arch));
	REQUIRE(arch == "x86_64");

	const std::vector<std::string> keys = config2->getSublevelKeys("source_groups");
	REQUIRE(keys.size() == 1);
}

TEST_CASE("config manager JSON save and reload round-trip for simple and nested values")
{
	const FilePath path("data/ConfigManagerTestSuite/temp.json");

	std::shared_ptr<ConfigManager> config = ConfigManager::createEmpty();
	config->setValue("application/font_name", std::string("Source Code Pro"));
	config->setValue("application/font_size", 14);
	config->setValue("screen/scale_factor", 1.5f);
	config->setValue("application/logging_enabled", true);
	REQUIRE(config->saveJson(path.str()));

	std::shared_ptr<ConfigManager> config2 =
		ConfigManager::createAndLoad(TextAccess::createFromFile(path));

	std::string fontName;
	REQUIRE(config2->getValue("application/font_name", fontName));
	REQUIRE(fontName == "Source Code Pro");

	int fontSize = 0;
	REQUIRE(config2->getValue("application/font_size", fontSize));
	REQUIRE(fontSize == 14);

	bool logging = false;
	REQUIRE(config2->getValue("application/logging_enabled", logging));
	REQUIRE(logging == true);
}

TEST_CASE("config manager JSON save and reload round-trip for plain list")
{
	// Mirrors how ApplicationSettings stores lists such as recent_projects: a plural
	// container key ("recent_projects") holding a repeated singular child
	// ("recent_project"). This must survive a save+reload cycle unchanged (the TOML
	// serializer does not, which is why ApplicationSettings uses JSON).
	const FilePath path("data/ConfigManagerTestSuite/temp_list.json");

	std::shared_ptr<ConfigManager> config = ConfigManager::createEmpty();
	config->setValues(
		"user/recent_projects/recent_project",
		std::vector<std::string>{"/a/first.srctrl.toml", "/b/second.srctrl.toml"});
	REQUIRE(config->saveJson(path.str()));

	std::shared_ptr<ConfigManager> config2 =
		ConfigManager::createAndLoad(TextAccess::createFromFile(path));

	std::vector<std::string> projects;
	REQUIRE(config2->getValues("user/recent_projects/recent_project", projects));
	REQUIRE(projects.size() == 2);
	REQUIRE(projects[0] == "/a/first.srctrl.toml");
	REQUIRE(projects[1] == "/b/second.srctrl.toml");
}
