// Inline implementations for SourceGroupSettingsWithCxxMakefile.h (included at its end). All definitions inline: the family
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

inline FilePath SourceGroupSettingsWithCxxMakefile::getMakefileDirectory() const
{
	return m_makefileDirectory;
}

inline FilePath SourceGroupSettingsWithCxxMakefile::getMakefileDirectoryExpandedAndAbsolute() const
{
	return utility::getExpandedAndAbsolutePath(
		getMakefileDirectory(), getProjectSettings()->getProjectDirectoryPath());
}

inline void SourceGroupSettingsWithCxxMakefile::setMakefileDirectory(const FilePath& directory)
{
	m_makefileDirectory = directory;
}

inline std::string SourceGroupSettingsWithCxxMakefile::getMakeArguments() const
{
	return m_makeArguments;
}

inline void SourceGroupSettingsWithCxxMakefile::setMakeArguments(const std::string& arguments)
{
	m_makeArguments = arguments;
}

inline std::string SourceGroupSettingsWithCxxMakefile::getMakeProgram() const
{
	return m_makeProgram;
}

inline void SourceGroupSettingsWithCxxMakefile::setMakeProgram(const std::string& program)
{
	m_makeProgram = program;
}

inline bool SourceGroupSettingsWithCxxMakefile::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCxxMakefile* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCxxMakefile*>(other);

	return (
		otherPtr && m_makefileDirectory == otherPtr->m_makefileDirectory &&
		m_makeArguments == otherPtr->m_makeArguments && m_makeProgram == otherPtr->m_makeProgram);
}

inline void SourceGroupSettingsWithCxxMakefile::load(const ConfigManager* config, const std::string& key)
{
	setMakefileDirectory(config->getValueOrDefault(key + "/make/directory", FilePath("")));
	setMakeArguments(config->getValueOrDefault(key + "/make/arguments", std::string()));
	setMakeProgram(config->getValueOrDefault(key + "/make/program", std::string()));
}

inline void SourceGroupSettingsWithCxxMakefile::save(ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/make/directory", getMakefileDirectory().str());
	config->setValue(key + "/make/arguments", getMakeArguments());
	config->setValue(key + "/make/program", getMakeProgram());
}
