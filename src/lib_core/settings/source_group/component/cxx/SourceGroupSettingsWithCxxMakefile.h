#ifndef SOURCE_GROUP_SETTINGS_WITH_CXX_MAKEFILE_H
#define SOURCE_GROUP_SETTINGS_WITH_CXX_MAKEFILE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsComponent.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "FilePath.h"
#endif

// How a Makefile project is built: the directory make runs in, what it is given (goals and
// variables, as typed after make), and the make program (empty: make).
SRCTRL_EXPORT class SourceGroupSettingsWithCxxMakefile: public SourceGroupSettingsComponent
{
public:
	~SourceGroupSettingsWithCxxMakefile() override = default;

	FilePath getMakefileDirectory() const;
	FilePath getMakefileDirectoryExpandedAndAbsolute() const;
	void setMakefileDirectory(const FilePath& directory);

	std::string getMakeArguments() const;
	void setMakeArguments(const std::string& arguments);

	std::string getMakeProgram() const;
	void setMakeProgram(const std::string& program);

protected:
	bool equals(const SourceGroupSettingsBase* other) const override;

	void load(const ConfigManager* config, const std::string& key) override;
	void save(ConfigManager* config, const std::string& key) override;

private:
	FilePath m_makefileDirectory;
	std::string m_makeArguments;
	std::string m_makeProgram;
};

#include "SourceGroupSettingsWithCxxMakefile.inl"

#endif	  // SOURCE_GROUP_SETTINGS_WITH_CXX_MAKEFILE_H
