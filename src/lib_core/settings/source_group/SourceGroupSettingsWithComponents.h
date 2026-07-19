#ifndef SOURCE_GROUP_SETTINGS_WITH_COMPONENTS_H
#define SOURCE_GROUP_SETTINGS_WITH_COMPONENTS_H

#include "SourceGroupSettings.h"

/*
To see the effect of C++17 'Variadic template fold expressions',
copy/paste this snippet into cppinsights.io:

#include <stdio.h>

template <typename... Args>
struct Printer : Args...
{
	void print()
	{
		(..., printArg<Args>());
	}

	template <typename T>
	void printArg()
	{
		T::print();
	}
};

struct Car
{
	void print()
	{
		printf("Car ");
	}
};

struct Plane
{
	void print()
	{
		printf("Plane ");
	}
};

int main()
{
	Printer<Car, Plane> printer;

	printer.print();
}
*/

template <typename... ComponentTypes>
class SourceGroupSettingsWithComponents
	: public SourceGroupSettings
	, public ComponentTypes...
{
public:
	SourceGroupSettingsWithComponents(
		SourceGroupType type, const std::string& id, const ProjectSettings* projectSettings)
		: SourceGroupSettings(type, id, projectSettings)
	{
	}

	~SourceGroupSettingsWithComponents() override = default;

	void loadSettings(const ConfigManager* config) override
	{
		const std::string key = s_keyPrefix + getId();

		SourceGroupSettings::load(config, key);

		(... , loadHelper<ComponentTypes>(config, key));
	}

	void saveSettings(ConfigManager* config) override
	{
		const std::string key = s_keyPrefix + getId();

		SourceGroupSettings::save(config, key);

		(... , saveHelper<ComponentTypes>(config, key));
	}

	bool equalsSettings(const SourceGroupSettingsBase* other) override
	{
		if (!SourceGroupSettings::equals(other))
		{
			return false;
		}

		const SourceGroupSettingsWithComponents<ComponentTypes...>* otherPtr =
			dynamic_cast<const SourceGroupSettingsWithComponents<ComponentTypes...>*>(other);
		if (getComponentCount() != otherPtr->getComponentCount())
		{
			return false;
		}

		return (... && equalsHelper<ComponentTypes>(other));
	}

private:
	size_t getComponentCount() const
	{
		return sizeof...(ComponentTypes);
	}

	template <typename T>
	void loadHelper(const ConfigManager* config, const std::string& key)
	{
		T::load(config, key);
	}

	template <typename T>
	void saveHelper(ConfigManager* config, const std::string& key)
	{
		T::save(config, key);
	}

	template <typename T>
	bool equalsHelper(const SourceGroupSettingsBase* other)
	{
		return T::equals(other);
	}
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_COMPONENTS_H
