#include "QtProjectWizardContentPathsIndexedHeaders.h"
#include "QtMessageBox.h"

#include <format>

#include "CMakeFileAPIReader.h"

#ifndef SRCTRL_MODULE_BUILD
#include "CompilationDatabase.h"
#include "IndexerCommandCxx.h"
#include "OrderedCache.h"
#endif
#include "QtPathListDialog.h"
#include "QtSelectPathsDialog.h"
#include "QtTextEditDialog.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsCxxCdb.h"
#endif
#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"
#include "logging.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#include "utilityFile.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.cxx;
import srctrl.file;
import srctrl.settings;
import srctrl.utility;
#endif

std::vector<FilePath> QtProjectWizardContentPathsIndexedHeaders::getIndexedPathsDerivedFromCDB(
	std::shared_ptr<const SourceGroupSettingsCxxCdb> settings)
{
	std::set<FilePath> indexedHeaderPaths;
	{
		const FilePath cdbPath = settings->getCompilationDatabasePathExpandedAndAbsolute();
		if (!cdbPath.empty() && cdbPath.exists())
		{
			for (const FilePath& path: IndexerCommandCxx::getSourceFilesFromCDB(cdbPath))
			{
				indexedHeaderPaths.insert(path.getCanonical().getParentDirectory());
			}
			for (const FilePath& path: utility::CompilationDatabase(cdbPath).getAllHeaderPaths())
			{
				if (path.exists())
				{
					indexedHeaderPaths.insert(path.getCanonical());
				}
			}
		}
		else
		{
			LOG_WARNING(
				"Unable to fetch indexed header paths. The provided Compilation Database path does "
				"not exist.");
		}
	}

	std::vector<FilePath> topLevelPaths;
	for (const FilePath& path: utility::getTopLevelPaths(indexedHeaderPaths))
	{
		if (path.exists())
		{
			topLevelPaths.push_back(path);
		}
	}

	return topLevelPaths;
}

std::vector<FilePath> QtProjectWizardContentPathsIndexedHeaders::getIndexedPathsDerivedFromCMake(
	const SourceGroupSettingsWithCxxCMakeBuildDirectory& settings)
{
	// resolveBuildDirectory() spelled out: it is a classic-only member (module builds).
	const FilePath buildDir{
		settings.getPresetName().empty()
			? settings.getBuildDirectoryExpandedAndAbsolute()
			: CMakeFileAPIReader::resolveBinaryDir(
				  settings.getSourceDirectoryExpandedAndAbsolute(), settings.getPresetName())};
	if (buildDir.empty())
	{
		return {};
	}
	const auto sources{CMakeFileAPIReader{buildDir}.getSourcesDetailed(
		settings.getConfiguration(), settings.getTargetGlob())};
	if (!sources)
	{
		return {};
	}

	std::set<FilePath> indexedHeaderPaths;
	for (const CMakeFileAPIReader::SourceEntry& entry: sources->entries)
	{
		if (!entry.compileGroup || entry.isGenerated)
		{
			continue;
		}
		indexedHeaderPaths.insert(entry.path.getParentDirectory());
		for (const FilePath& include: entry.compileGroup->includes)
		{
			if (include.exists())
			{
				indexedHeaderPaths.insert(include.getCanonical());
			}
		}
	}

	std::vector<FilePath> topLevelPaths;
	for (const FilePath& path: utility::getTopLevelPaths(indexedHeaderPaths))
	{
		if (path.exists())
		{
			topLevelPaths.push_back(path);
		}
	}
	return topLevelPaths;
}

QtProjectWizardContentPathsIndexedHeaders::QtProjectWizardContentPathsIndexedHeaders(
	std::shared_ptr<SourceGroupSettings> settings,
	QtProjectWizardWindow* window,
	const std::string& projectKindName)
	: QtProjectWizardContentPaths(
		  settings, window, QtPathListBox::SelectionPolicyType::SELECTION_POLICY_FILES_AND_DIRECTORIES, true)
	, m_projectKindName(projectKindName)
{
	m_showFilesString = QLatin1String("");

	setTitleString(QStringLiteral("Header Files & Directories to Index"));
	setHelpString(QString::fromStdString(
		"The " + m_projectKindName +
		" already specifies which source files are part of your project. But Sourcetrail still "
		"needs to know which header files to index as part of your project and which to skip. "
		"Choosing to skip indexing "
		"your system headers or external frameworks will significantly improve the overall "
		"indexing performance.<br />"
		"<br />"
		"Use this list to define which header files should be indexed by Sourcetrail. Provide a "
		"directory to recursively "
		"add all contained files.<br />"
		"<br />"
		"You can make use of environment variables with ${ENV_VAR}.<br />"
		"<br />"
		"<b>Hint</b>: Just enter the root path of your project if you want Sourcetrail to index "
		"all contained headers it "
		"encounters.<br />"));
}

void QtProjectWizardContentPathsIndexedHeaders::populate(QGridLayout* layout, int& row)
{
	QtProjectWizardContentPaths::populate(layout, row);

	QPushButton* button = new QPushButton(QString::fromStdString("Select from " + m_projectKindName));
	button->setObjectName(QStringLiteral("windowButton"));
	connect(
		button, &QPushButton::clicked, this, &QtProjectWizardContentPathsIndexedHeaders::buttonClicked);

	layout->addWidget(button, row, QtProjectWizardWindow::BACK_COL, Qt::AlignRight | Qt::AlignTop);
	row++;
}

void QtProjectWizardContentPathsIndexedHeaders::load()
{
	if (std::shared_ptr<SourceGroupSettingsWithIndexedHeaderPaths> cdbSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsWithIndexedHeaderPaths>(m_settings))
	{
		m_list->setPaths(cdbSettings->getIndexedHeaderPaths());
	}
}

void QtProjectWizardContentPathsIndexedHeaders::save()
{
	if (std::shared_ptr<SourceGroupSettingsWithIndexedHeaderPaths> cdbSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsWithIndexedHeaderPaths>(m_settings))
	{
		cdbSettings->setIndexedHeaderPaths(m_list->getPathsAsDisplayed());
	}
}

bool QtProjectWizardContentPathsIndexedHeaders::check()
{
	if (m_list->getPathsAsDisplayed().empty())
	{
		// A CMake group has the directory to offer: its source directory, where its own
		// headers are.
		FilePath sourceDirectory;
		if (const auto cmakeSettings =
				std::dynamic_pointer_cast<SourceGroupSettingsWithCxxCMakeBuildDirectory>(m_settings))
		{
			sourceDirectory = cmakeSettings->getSourceDirectory().empty() ? FilePath{} : cmakeSettings->getSourceDirectoryExpandedAndAbsolute();
		}
		const std::string question{
			"You didn't specify any Header Files & Directories to Index. Sourcetrail will only "
			"index the source files the " +
			m_projectKindName + " lists and none of the included header files."};
		if (sourceDirectory.empty())
		{
			if (m_window->ask(QString::fromStdString(question), {"Continue", "Cancel"}) != 0)
			{
				return false;
			}
		}
		else
		{
			const int answer{m_window->ask(
				QString::fromStdString(
					question + " Index the headers in the source directory " + sourceDirectory.str() + "?"),
				{"Add Source Directory", "Continue", "Cancel"})};
			if (answer == 2 || answer < 0)
			{
				return false;
			}
			if (answer == 0)
			{
				m_list->setPaths({sourceDirectory});
				save();
			}
		}
	}

	return QtProjectWizardContentPaths::check();
}

void QtProjectWizardContentPathsIndexedHeaders::buttonClicked()
{
	save();

	if (m_filesDialog)
	{
		m_filesDialog->show();
		m_filesDialog->raise();
		return;
	}

	if (std::shared_ptr<SourceGroupSettingsCxxCdb> cdbSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsCxxCdb>(m_settings))
	{
		const FilePath cdbPath = cdbSettings->getCompilationDatabasePathExpandedAndAbsolute();
		if (!cdbPath.exists())
		{
			QtMessageBox msgBox(m_window);
			msgBox.setText(QStringLiteral("The provided Compilation Database path does not exist."));
			msgBox.setDetailedText(QString::fromStdString(cdbPath.str()));
			msgBox.execModal();
			return;
		}

		showSelectPathsDialog(
			QStringLiteral("Select from Include Paths"),
			QStringLiteral(
				"The list contains all Include Paths found in the Compilation Database. Red paths "
				"do not exist. Select the paths containing the header files you want to index with "
				"Sourcetrail."),
			getIndexedPathsDerivedFromCDB(cdbSettings));
	}
	else if (
		std::shared_ptr<SourceGroupSettingsWithCxxCMakeBuildDirectory> cmakeSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsWithCxxCMakeBuildDirectory>(m_settings))
	{
		// The page above was just saved too: its source directory, preset and targets.
		m_window->saveContent();
		const std::vector<FilePath> paths{getIndexedPathsDerivedFromCMake(*cmakeSettings)};
		if (paths.empty())
		{
			m_window->tell(QStringLiteral(
				"The build directory has no CMake File API reply to select from yet. Press \"Run "
				"CMake\" above (or index the project once), then select again."));
			return;
		}
		showSelectPathsDialog(
			QStringLiteral("Select from CMake File API"),
			QStringLiteral(
				"The list contains the directories of the selected targets' source files and "
				"their include paths, as CMake reports them. Select the paths containing the "
				"header files you want to index with Sourcetrail."),
			paths);
	}
}

void QtProjectWizardContentPathsIndexedHeaders::showSelectPathsDialog(
	const QString& title, const QString& description, const std::vector<FilePath>& paths)
{
	auto* dialog = new QtSelectPathsDialog(title, description, m_window);
	m_filesDialog = dialog;
	dialog->setup();

	connect(dialog, &QtSelectPathsDialog::finished, this, &QtProjectWizardContentPathsIndexedHeaders::savedFilesDialog);
	connect(dialog, &QtSelectPathsDialog::canceled, this, &QtProjectWizardContentPathsIndexedHeaders::closedFilesDialog);

	const FilePath projectPath = m_settings->getProjectDirectoryPath();
	std::vector<FilePath> checkedPaths;
	if (const auto headerSettings = std::dynamic_pointer_cast<SourceGroupSettingsWithIndexedHeaderPaths>(m_settings))
	{
		checkedPaths = headerSettings->getIndexedHeaderPaths();
	}
	dialog->setPathsList(
		utility::convert<FilePath, FilePath>(
			paths, [&](const FilePath& path) { return utility::getAsRelativeIfShorter(path, projectPath); }),
		checkedPaths,
		projectPath);

	dialog->show();
	dialog->raise();
}

void QtProjectWizardContentPathsIndexedHeaders::savedFilesDialog()
{
	m_list->setPaths(dynamic_cast<QtSelectPathsDialog*>(m_filesDialog)->getPathsList());
	closedFilesDialog();
}
