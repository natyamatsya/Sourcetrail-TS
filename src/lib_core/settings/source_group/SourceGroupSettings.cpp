// Classic seam for SourceGroupSettings' ProjectSettings-forwarding accessors.
//
// These forward to the ProjectSettings the group belongs to, so their bodies need ProjectSettings
// COMPLETE. They used to be defined `inline` in ProjectSettings.inl (where ProjectSettings is
// complete), but SourceGroupSettings.h pulls only SourceGroupSettings.inl -- so a classic consumer
// that has SourceGroupSettings.h without ProjectSettings.inl saw the declarations but not the inline
// definitions and failed to link (LNK2019, e.g. QtProjectWizardContentPaths.cpp in the classic
// Windows build). Defining them out-of-line here -- with both classes complete -- makes them ordinary
// linked symbols that any consumer of SourceGroupSettings.h resolves. Under the module build's
// global-module attachment these ordinary-mangled definitions link for importer callers too, so the
// module build is unaffected. Dual-build safe; a no-op for the clang build's behaviour.

#include "SourceGroupSettings.h"

#include "FilePath.h"
#include "ProjectSettings.h"

FilePath SourceGroupSettings::getSourceGroupDependenciesDirectoryPath() const
{
	return getProjectSettings()->getDependenciesDirectoryPath().concatenate(getId());
}

FilePath SourceGroupSettings::getProjectDirectoryPath() const
{
	return m_projectSettings->getProjectDirectoryPath();
}

std::vector<FilePath> SourceGroupSettings::makePathsExpandedAndAbsolute(const std::vector<FilePath>& paths) const
{
	return m_projectSettings->makePathsExpandedAndAbsolute(paths);
}
