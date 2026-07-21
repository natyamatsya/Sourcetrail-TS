// Inline implementations for SourceGroupSettingsUnloadable.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ConfigManager.h"
#include "utility.h"
#endif

inline SourceGroupSettingsUnloadable::SourceGroupSettingsUnloadable(
	const std::string& id, const ProjectSettings* projectSettings)
	: SourceGroupSettings(SourceGroupType::UNKNOWN, id, projectSettings)
{
}

inline std::string SourceGroupSettingsUnloadable::getTypeString()
{
	return m_typeString;
}

inline std::shared_ptr<SourceGroupSettings> SourceGroupSettingsUnloadable::createCopy() const
{
	return std::make_shared<SourceGroupSettingsUnloadable>(*this);
}

inline void SourceGroupSettingsUnloadable::loadSettings(const ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();

	SourceGroupSettings::load(config, key);
	setStatus(SourceGroupStatusType::DISABLED);

	m_typeString = config->getValueOrDefault<std::string>(key + "/type", "");

	m_content.clear();

	std::vector<std::string> unprocessedKeys = {key};

	while (!unprocessedKeys.empty())
	{
		const std::string unprocessedKey = unprocessedKeys.back();
		unprocessedKeys.pop_back();
		for (const std::string& memberKey: config->getSublevelKeys(unprocessedKey))
		{
			const std::vector<std::string> values = config->getValuesOrDefaults<std::string>(
				memberKey, {});
			if (!values.empty())
			{
				m_content[memberKey] = values;
			}
			else
			{
				unprocessedKeys.push_back(memberKey);
			}
		}
	}
}

inline void SourceGroupSettingsUnloadable::saveSettings(ConfigManager* config)
{
	for (const auto &it: m_content)
	{
		config->setValues(it.first, it.second);
	}
}

inline bool SourceGroupSettingsUnloadable::equalsSettings(const SourceGroupSettingsBase* other)
{
	if (!SourceGroupSettings::equals(other))
	{
		return false;
	}

	if (const SourceGroupSettingsUnloadable* otherUnloadable =
			dynamic_cast<const SourceGroupSettingsUnloadable*>(other))
	{
		for (const auto &it: m_content)
		{
			auto otherIt = otherUnloadable->m_content.find(it.first);
			if (otherIt == otherUnloadable->m_content.end() ||
				!utility::isPermutation(it.second, otherIt->second))
			{
				return false;
			}
		}
		return true;
	}
	return false;
}
