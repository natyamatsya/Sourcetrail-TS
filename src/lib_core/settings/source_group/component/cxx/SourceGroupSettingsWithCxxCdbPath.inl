// Inline implementations for SourceGroupSettingsWithCxxCdbPath.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Family-internal includes stay unguarded: same module either way; include guards
// + inl-after-class ordering make the cross-references resolve in both builds.
#include "ProjectSettings.h"

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityFile.h"
#endif

inline FilePath SourceGroupSettingsWithCxxCdbPath::getCompilationDatabasePath() const
{
	return m_compilationDatabasePath;
}

inline FilePath SourceGroupSettingsWithCxxCdbPath::getCompilationDatabasePathExpandedAndAbsolute() const
{
	return utility::getExpandedAndAbsolutePath(
		getCompilationDatabasePath(), getProjectSettings()->getProjectDirectoryPath());
}

inline void SourceGroupSettingsWithCxxCdbPath::setCompilationDatabasePath(const FilePath& compilationDatabasePath)
{
	m_compilationDatabasePath = compilationDatabasePath;
}

inline bool SourceGroupSettingsWithCxxCdbPath::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCxxCdbPath* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCxxCdbPath*>(other);

	return (otherPtr && m_compilationDatabasePath == otherPtr->m_compilationDatabasePath);
}

inline void SourceGroupSettingsWithCxxCdbPath::load(const ConfigManager* config, const std::string& key)
{
	setCompilationDatabasePath(
		config->getValueOrDefault(key + "/build_file_path/compilation_db_path", FilePath("")));
}

inline void SourceGroupSettingsWithCxxCdbPath::save(ConfigManager* config, const std::string& key)
{
	config->setValue(
		key + "/build_file_path/compilation_db_path", getCompilationDatabasePath().str());
}
