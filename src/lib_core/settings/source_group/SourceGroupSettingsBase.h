#ifndef SOURCE_GROUP_SETTINGS_BASE_H
#define SOURCE_GROUP_SETTINGS_BASE_H

#include "SrctrlModule.h"

// FilePath is another module's type: fwd-declare only in the classic build (the purview gets it
// from `import srctrl.file`). ProjectSettings is family-internal -- the fwd decl attaches to this
// module in the purview and must stay visible.
#ifndef SRCTRL_MODULE_PURVIEW
class FilePath;
#endif
SRCTRL_EXPORT class ProjectSettings;

SRCTRL_EXPORT class SourceGroupSettingsBase
{
public:
	virtual ~SourceGroupSettingsBase() = default;

	virtual const ProjectSettings* getProjectSettings() const = 0;
	virtual FilePath getSourceGroupDependenciesDirectoryPath() const = 0;
};

#endif	  // SOURCE_GROUP_SETTINGS_BASE_H
