// The one member that needs CMakeFileAPIReader (GMF-poison for srctrl.settings: its closure
// textually reaches FilePath.h). Classic-callers-only, so the classic ordinary-mangled definition
// suffices (include-only-member contract); everything else is inline in the .inl.
#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"

#include "CMakeFileAPIReader.h"

FilePath SourceGroupSettingsWithCxxCMakeBuildDirectory::resolveBuildDirectory() const
{
	return CMakeFileAPIReader::resolveBinaryDir(
		getSourceDirectoryExpandedAndAbsolute(), m_presetName);
}
