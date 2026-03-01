#include "QtProjectWizardContentPathsSource.h"
#include "QtMessageBox.h"

#include "language_package_flags.h"

#include "SourceGroupCustomCommand.h"
#include "SourceGroupSettingsCustomCommand.h"
#include "SourceGroupSettingsWithSourcePaths.h"
#include "utility.h"
#include "utilityFile.h"

#include "SourceGroupCxxEmpty.h"
#include "SourceGroupSettingsWithCxxPathsAndFlags.h"

QtProjectWizardContentPathsSource::QtProjectWizardContentPathsSource(
	std::shared_ptr<SourceGroupSettings> settings, QtProjectWizardWindow* window)
	: QtProjectWizardContentPaths(
		  settings, window, QtPathListBox::SelectionPolicyType::SELECTION_POLICY_FILES_AND_DIRECTORIES, true)
{
	m_showFilesString = QStringLiteral("show files");

	setTitleString(QStringLiteral("Files & Directories to Index"));
	setHelpString(QStringLiteral(
		"These paths define the files and directories that will be indexed by Sourcetrail. Provide "
		"a directory to recursively "
		"add all contained source and header files.<br />"
		"<br />"
		"If your project's source code resides in one location, but generated source files are "
		"kept at a different location, "
		"you will also need to add that directory.<br />"
		"<br />"
		"You can make use of environment variables with ${ENV_VAR}."));
	setIsRequired(true);
}

void QtProjectWizardContentPathsSource::load()
{
	if (std::shared_ptr<SourceGroupSettingsWithSourcePaths> pathSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsWithSourcePaths>(
				m_settings))	// FIXME: pass msettings as required type
	{
		m_list->setPaths(pathSettings->getSourcePaths());
	}
}

void QtProjectWizardContentPathsSource::save()
{
	if (std::shared_ptr<SourceGroupSettingsWithSourcePaths> pathSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsWithSourcePaths>(
				m_settings))	// FIXME: pass msettings as required type
	{
		pathSettings->setSourcePaths(m_list->getPathsAsDisplayed());
	}
}

bool QtProjectWizardContentPathsSource::check()
{
	if (m_list->getPathsAsDisplayed().empty())
	{
		QtMessageBox msgBox(m_window);
		msgBox.setText(QStringLiteral("You didn't specify any 'Files & Directories to Index'."));
		msgBox.setInformativeText(
			QStringLiteral("Sourcetrail will not index any files for this Source Group. Please add "
						   "paths to files or directories "
						   "that should be indexed."));
		QPushButton *continueButton = msgBox.addButton(QStringLiteral("Continue"), QtMessageBox::ButtonRole::YesRole);
		msgBox.addButton(QStringLiteral("Cancel"), QtMessageBox::ButtonRole::NoRole);
		msgBox.setDefaultButton(continueButton);

		if (msgBox.execModal() != continueButton)
		{
			return false;
		}
	}

	return QtProjectWizardContentPaths::check();
}

std::vector<FilePath> QtProjectWizardContentPathsSource::getFilePaths() const
{
	std::set<FilePath> allSourceFilePaths;

	if constexpr (language_packages::buildCxxLanguagePackage)
	if (std::dynamic_pointer_cast<SourceGroupSettingsWithCxxPathsAndFlags>(m_settings))
		allSourceFilePaths = SourceGroupCxxEmpty(m_settings).getAllSourceFilePaths();

	if (std::shared_ptr<SourceGroupSettingsCustomCommand> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsCustomCommand>(m_settings))
	{
		allSourceFilePaths = SourceGroupCustomCommand(settings).getAllSourceFilePaths();
	}

	return utility::getAsRelativeIfShorter(
		utility::toVector(allSourceFilePaths), m_settings->getProjectDirectoryPath());
}

QString QtProjectWizardContentPathsSource::getFileNamesTitle() const
{
	return QStringLiteral("Indexed Files");
}

QString QtProjectWizardContentPathsSource::getFileNamesDescription() const
{
	return QStringLiteral(" files will be indexed.");
}
