#include "QtProjectWizardContentPathsExclude.h"

#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettings.h"
#endif
#include "SourceGroupSettingsWithExcludeFilters.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#endif
#include "utilityFilePath.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
import srctrl.utility;
#endif

QtProjectWizardContentPathsExclude::QtProjectWizardContentPathsExclude(
	std::shared_ptr<SourceGroupSettings> settings, QtProjectWizardWindow* window)
	: QtProjectWizardContentPaths(
		  settings, window, QtPathListBox::SelectionPolicyType::SELECTION_POLICY_FILES_AND_DIRECTORIES, false)
{
	setTitleString(QStringLiteral("Excluded Files & Directories"));
	setHelpString(
		"<p>These paths define the files and directories that will be left out from indexing.</p>"
		"<p>Hints:"
		"<ul>"
		"<li>You can use the wildcard \"*\" which represents characters except \"\\\" or \"/\" "
		"(e.g. \"src/*/test.h\" matches \"src/app/test.h\" but does not match "
		"\"src/app/widget/test.h\" or \"src/test.h\")</li>"
		"<li>You can use the wildcard \"**\" which represents arbitrary characters (e.g. "
		"\"src**test.h\" matches \"src/app/test.h\" as well as \"src/app/widget/test.h\" or "
		"\"src/test.h\")</li>"
		"<li>You can make use of environment variables with ${ENV_VAR}</li>"
		"</ul></p>");
}

void QtProjectWizardContentPathsExclude::load()
{
	if (std::shared_ptr<SourceGroupSettingsWithExcludeFilters> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsWithExcludeFilters>(
				m_settings))	// FIXME: pass msettings as required type
	{
		m_list->setPaths(utility::convert<std::string, FilePath>(
			settings->getExcludeFilterStrings(), [](const std::string& s) { return FilePath(s); }));
	}
}

void QtProjectWizardContentPathsExclude::save()
{
	if (std::shared_ptr<SourceGroupSettingsWithExcludeFilters> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsWithExcludeFilters>(
				m_settings))	// FIXME: pass msettings as required type
	{
		settings->setExcludeFilterStrings(utility::toStrings(m_list->getPathsAsDisplayed()));
	}
}
