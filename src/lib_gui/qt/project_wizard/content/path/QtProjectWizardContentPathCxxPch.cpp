#include "QtProjectWizardContentPathCxxPch.h"
#include "QtMessageBox.h"

#ifndef SRCTRL_MODULE_BUILD
#include "IndexerCommandCxx.h"
#include "SourceGroupSettingsCxxCdb.h"
#endif
#include "SourceGroupSettingsWithCxxPchOptions.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#include "utilityFile.h"
#endif
#include "utilitySourceGroupCxx.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.cxx;
import srctrl.file;
import srctrl.settings;
import srctrl.utility;
#endif

QtProjectWizardContentPathCxxPch::QtProjectWizardContentPathCxxPch(
	std::shared_ptr<SourceGroupSettings> settings,
	std::shared_ptr<SourceGroupSettingsWithCxxPchOptions> settingsCxxPch,
	QtProjectWizardWindow* window)
	: QtProjectWizardContentPath(window), m_settings(settings), m_settingsCxxPch(settingsCxxPch)
{
	setTitleString(QStringLiteral("Precompiled Header File"));
	setHelpString(
		"Specify the path to the input header file that should be used to generate a precompiled "
		"header before indexing.<br />"
		"If the indexed source code is usually built using precompiled headers, using this option "
		"will speed up your indexing performance.<br />"
		"<br />"
		"If your source files use precompiled headers via \"#include &lt;pch.h&gt;\", specify "
		"\"path/to/pch.h\".<br />"
		"<br />"
		"Leave blank to disable the use of precompiled headers. You can make use of environment "
		"variables with ${ENV_VAR}.");
	setPlaceholderString(QStringLiteral("Not Using Precompiled Header"));
}

void QtProjectWizardContentPathCxxPch::populate(QGridLayout* layout, int& row)
{
	QtProjectWizardContentPath::populate(layout, row);

	m_picker->setPickDirectory(false);
}

void QtProjectWizardContentPathCxxPch::load()
{
	m_picker->setText(QString::fromStdString(m_settingsCxxPch->getPchInputFilePath().str()));
}

void QtProjectWizardContentPathCxxPch::save()
{
	m_settingsCxxPch->setPchInputFilePathFilePath(FilePath(m_picker->getText().toStdString()));
}

bool QtProjectWizardContentPathCxxPch::check()
{
	if (std::shared_ptr<SourceGroupSettingsCxxCdb> cdbSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsCxxCdb>(m_settings))
	{
		const FilePath cdbPath = cdbSettings->getCompilationDatabasePathExpandedAndAbsolute();
		const std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, utility::CdbLoadError>
			loadedCdb = utility::loadCDB(cdbPath);
		if (!loadedCdb)
		{
			QtMessageBox msgBox(m_window);
			msgBox.setText(tr("Unable to open and read the provided compilation database file."));
			msgBox.setInformativeText(QString::fromStdString(
				loadedCdb.error().message.empty() ? std::string(utility::to_std_sv(loadedCdb.error().code))
												  : loadedCdb.error().message));
			msgBox.execModal();
			return false;
		}
		const std::shared_ptr<clang::tooling::CompilationDatabase> cdb = loadedCdb.value();

		if (utility::containsIncludePchFlags(cdb))
		{
			if (m_settingsCxxPch->getPchInputFilePath().empty())
			{
				QtMessageBox msgBox(m_window);
				msgBox.setText(
					"The provided compilation database file uses precompiled headers. If you want "
					"to make use of "
					"precompiled headers to speed up your indexer, please specify an input at "
					"Precompiled Header File.");
				QPushButton* cancelButton = msgBox.addButton(QStringLiteral("Cancel"), QtMessageBox::ButtonRole::RejectRole);
				msgBox.addButton(QStringLiteral("Continue"), QtMessageBox::ButtonRole::AcceptRole);
				return msgBox.execModal() != cancelButton;
			}
		}
		else
		{
			if (!m_settingsCxxPch->getPchInputFilePath().empty())
			{
				QtMessageBox msgBox(m_window);
				msgBox.setText(
					"The provided compilation database file does not use precompiled headers. The "
					"specified input file at "
					"Precompiled Header File will not be used.");
				QPushButton* cancelButton = msgBox.addButton(QStringLiteral("Cancel"), QtMessageBox::ButtonRole::RejectRole);
				msgBox.addButton(QStringLiteral("Continue"), QtMessageBox::ButtonRole::AcceptRole);
				return msgBox.execModal() != cancelButton;
			}
		}
	}
	return true;
}

std::shared_ptr<SourceGroupSettings> QtProjectWizardContentPathCxxPch::getSourceGroupSettings()
{
	return m_settings;
}
