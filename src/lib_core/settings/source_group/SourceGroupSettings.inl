// Inline implementations for SourceGroupSettings.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// The three ProjectSettings-touching members live in ProjectSettings.inl (keeps this inl free
// of ProjectSettings.h, which breaks the WithComponents include cycle).
// Family-internal includes stay unguarded: same module either way; include guards
// + inl-after-class ordering make the cross-references resolve in both builds.

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ConfigManager.h"
#endif

inline const size_t SourceGroupSettings::s_version = 1;
inline const std::string SourceGroupSettings::s_keyPrefix = "source_groups/source_group_";

inline SourceGroupSettings::SourceGroupSettings(
	SourceGroupType type, const std::string& id, const ProjectSettings* projectSettings)
	: m_projectSettings(projectSettings)
	, m_type(type)
	, m_id(id)
	, m_name(sourceGroupTypeToString(type))
	 
{
}

inline bool SourceGroupSettings::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettings* otherPtr = dynamic_cast<const SourceGroupSettings*>(other);

	return (
		m_id == otherPtr->m_id &&
		// m_name == otherPtr->m_name && // Ignore name, since not significant regarding index state
		m_type == otherPtr->m_type && m_status == otherPtr->m_status);
}

inline void SourceGroupSettings::load(const ConfigManager* config, const std::string& key)
{
	const std::string name = config->getValueOrDefault<std::string>(key + "/name", "");
	if (!name.empty())
	{
		setName(name);
	}

	setStatus(stringToSourceGroupStatusType(config->getValueOrDefault(
		key + "/status", sourceGroupStatusTypeToString(SourceGroupStatusType::ENABLED))));
}

inline void SourceGroupSettings::save(ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/status", sourceGroupStatusTypeToString(getStatus()));
	config->setValue(key + "/name", getName());
}

inline std::string SourceGroupSettings::getId() const
{
	return m_id;
}

inline void SourceGroupSettings::setId(const std::string& id)
{
	m_id = id;
}

inline SourceGroupType SourceGroupSettings::getType() const
{
	return m_type;
}

inline LanguageType SourceGroupSettings::getLanguage() const
{
	return getLanguageTypeForSourceGroupType(getType());
}

inline std::string SourceGroupSettings::getName() const
{
	return m_name;
}

inline void SourceGroupSettings::setName(const std::string& name)
{
	m_name = name;
}

inline SourceGroupStatusType SourceGroupSettings::getStatus() const
{
	return m_status;
}

inline void SourceGroupSettings::setStatus(SourceGroupStatusType status)
{
	m_status = status;
}

inline const ProjectSettings* SourceGroupSettings::getProjectSettings() const
{
	return m_projectSettings;
}

