#include "SourceGroupFactory.h"

#include <algorithm>
#include <format>

#include "SourceGroup.h"
#include "SourceGroupFactoryModule.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettings.h"
#endif
#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

std::shared_ptr<SourceGroupFactory> SourceGroupFactory::getInstance()
{
	if (!s_instance)
	{
		s_instance = std::shared_ptr<SourceGroupFactory>(new SourceGroupFactory());
	}
	return s_instance;
}

void SourceGroupFactory::addModule(std::shared_ptr<SourceGroupFactoryModule> module)
{
	m_modules.push_back(module);
}

namespace
{
// Whether `other` configures the same source group as `settings`: equal in all but id and name.
bool configuresSameGroup(
	const std::shared_ptr<SourceGroupSettings>& settings, const std::shared_ptr<SourceGroupSettings>& other)
{
	if (settings->getType() != other->getType())
	{
		return false;
	}
	const std::shared_ptr<SourceGroupSettings> otherWithSameId = other->createCopy();
	otherWithSameId->setId(settings->getId());
	return settings->equalsSettings(otherWithSameId.get());
}
}	 // namespace

std::vector<std::shared_ptr<SourceGroup>> SourceGroupFactory::createSourceGroups(
	std::vector<std::shared_ptr<SourceGroupSettings>> allSourceGroupSettings)
{
	std::vector<std::shared_ptr<SourceGroup>> sourceGroups;
	// A group configured twice (a copied block in the .srctrl.toml, the same group added again) is
	// one group: 100 identical groups indexed every file 100 times.
	std::vector<std::shared_ptr<SourceGroupSettings>> createdSettings;
	for (const std::shared_ptr<SourceGroupSettings>& sourceGroupSettings: allSourceGroupSettings)
	{
		if (const auto same = std::ranges::find_if(
				createdSettings,
				[&](const std::shared_ptr<SourceGroupSettings>& created) {
					return configuresSameGroup(created, sourceGroupSettings);
				});
			same != createdSettings.end())
		{
			LOG_WARNING(std::format(
				"Source group \"{}\" ({}) is configured like \"{}\" ({}): it is indexed once.",
				sourceGroupSettings->getName(),
				sourceGroupSettings->getId(),
				(*same)->getName(),
				(*same)->getId()));
			continue;
		}

		std::shared_ptr<SourceGroup> sourceGroup = createSourceGroup(sourceGroupSettings);
		if (sourceGroup)
		{
			sourceGroups.push_back(sourceGroup);
			createdSettings.push_back(sourceGroupSettings);
		}
	}
	return sourceGroups;
}

std::shared_ptr<SourceGroup> SourceGroupFactory::createSourceGroup(
	std::shared_ptr<SourceGroupSettings> settings)
{
	std::shared_ptr<SourceGroup> sourceGroup;

	for (const std::shared_ptr<SourceGroupFactoryModule>& module: m_modules)
	{
		if (module->supports(settings->getType()))
		{
			sourceGroup = module->createSourceGroup(settings);
			break;
		}
	}

	return sourceGroup;
}

std::shared_ptr<SourceGroupFactory> SourceGroupFactory::s_instance;

SourceGroupFactory::SourceGroupFactory() = default;
