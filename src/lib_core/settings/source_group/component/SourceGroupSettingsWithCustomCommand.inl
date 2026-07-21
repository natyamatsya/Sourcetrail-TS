// Inline implementations for SourceGroupSettingsWithCustomCommand.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ConfigManager.h"
#endif

inline const std::string& SourceGroupSettingsWithCustomCommand::getCustomCommand() const
{
	return m_customCommand;
}

inline void SourceGroupSettingsWithCustomCommand::setCustomCommand(const std::string& customCommand)
{
	m_customCommand = customCommand;
}

inline bool SourceGroupSettingsWithCustomCommand::getRunInParallel() const
{
	return m_runInParallel;
}

inline void SourceGroupSettingsWithCustomCommand::setRunInParallel(bool runInParallel)
{
	m_runInParallel = runInParallel;
}

inline bool SourceGroupSettingsWithCustomCommand::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCustomCommand* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCustomCommand*>(other);

	return (otherPtr && m_customCommand == otherPtr->m_customCommand);
}

inline void SourceGroupSettingsWithCustomCommand::load(const ConfigManager* config, const std::string& key)
{
	setCustomCommand(config->getValueOrDefault(key + "/custom_command", std::string()));
	setRunInParallel(config->getValueOrDefault(key + "/run_in_parallel", false));
}

inline void SourceGroupSettingsWithCustomCommand::save(ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/custom_command", getCustomCommand());
	config->setValue(key + "/run_in_parallel", getRunInParallel());
}
