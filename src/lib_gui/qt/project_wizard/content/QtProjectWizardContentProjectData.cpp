#include "QtProjectWizardContentProjectData.h"
#include "QtMessageBox.h"

#include <QRegularExpression>

#ifndef SRCTRL_MODULE_BUILD
#include "FileSystem.h"
#include "ProjectSettings.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.settings;
#endif

QtProjectWizardContentProjectData::QtProjectWizardContentProjectData(
	std::shared_ptr<ProjectSettings> projectSettings,
	QtProjectWizardWindow* window,
	bool disableNameEditing)
	: QtProjectWizardContent(window)
	, m_projectSettings(projectSettings)
	, m_disableNameEditing(disableNameEditing)
{
	setIsRequired(true);
}

void QtProjectWizardContentProjectData::populate(QGridLayout* layout, int& row)
{
	QLabel* nameLabel = createFormLabel(QStringLiteral("Sourcetrail Project Name"));
	m_projectName = new QLineEdit();
	m_projectName->setObjectName(QStringLiteral("name"));
	m_projectName->setAttribute(Qt::WA_MacShowFocusRect, false);
	m_projectName->setEnabled(!m_disableNameEditing);
	connect(
		m_projectName,
		&QLineEdit::textEdited,
		this,
		&QtProjectWizardContentProjectData::onProjectNameEdited);

	layout->addWidget(nameLabel, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);
	layout->addWidget(m_projectName, row, QtProjectWizardWindow::BACK_COL);
	layout->setRowMinimumHeight(row, 30);
	row++;

	QLabel* locationLabel = createFormLabel(QStringLiteral("Sourcetrail Project Location"));
	m_projectFileLocation = new QtLocationPicker(this);
	m_projectFileLocation->setPickDirectory(true);
	m_projectFileLocation->setEnabled(!m_disableNameEditing);

	layout->addWidget(locationLabel, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);
	layout->addWidget(m_projectFileLocation, row, QtProjectWizardWindow::BACK_COL, Qt::AlignTop);
	addHelpButton(
		QStringLiteral("Sourcetrail Project Location"),
		QStringLiteral("The directory the Sourcetrail project file (.srctrl.toml) will be saved to."),
		layout,
		row);
	layout->setRowMinimumHeight(row, 30);
	row++;
}

void QtProjectWizardContentProjectData::load()
{
	m_projectName->setText(QString::fromStdString(m_projectSettings->getProjectName()));
	m_projectFileLocation->setText(
		QString::fromStdString(m_projectSettings->getProjectDirectoryPath().str()));
}

void QtProjectWizardContentProjectData::save()
{
	m_projectSettings->setProjectFilePath(
		m_projectName->text().toStdString(),
		FilePath(m_projectFileLocation->getText().toStdString()));
}

bool QtProjectWizardContentProjectData::check()
{
	if (m_projectName->text().isEmpty())
	{
		m_window->tell(QStringLiteral("Please enter a project name."));
		return false;
	}

	if (!isValidProjectName(m_projectName->text()))
	{
		m_window->tell(
			"The provided project name is not a valid file name. Please adjust the name "
			"accordingly.");
		return false;
	}

	if (m_projectFileLocation->getText().isEmpty())
	{
		m_window->tell(
			QStringLiteral("Please define the location for the Sourcetrail project file."));
		return false;
	}

	std::vector<FilePath> paths =
		FilePath(m_projectFileLocation->getText().toStdString()).expandEnvironmentVariables();
	if (paths.size() != 1)
	{
		m_window->tell(
			"The specified location seems to be invalid. Please make sure that the used "
			"environment variables are unambiguous.");
		return false;
	}
	else if (!paths.front().isAbsolute())
	{
		m_window->tell(
			"The specified location seems to be invalid. Please specify an absolute directory "
			"path.");
		return false;
	}
	else if (!paths.front().isValid())
	{
		m_window->tell(
			"The specified location seems to be invalid. Please check the characters used in the "
			"path.");
		return false;
	}
	else if (!paths[0].exists())
	{
		if (m_window->ask(
				QStringLiteral("The specified location does not exist. Do you want to create the directory?"),
				{"Create", "Abort"}) == 0)
		{
			if (const auto created = FileSystem::createDirectories(paths[0]); !created)
			{
				m_window->tell(QString::fromStdString(
					"The directory \"" + paths[0].str() + "\" could not be created: " +
					created.error().message() + ". Please pick a different location."));
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	return true;
}

bool QtProjectWizardContentProjectData::isValidProjectName(const QString& name)
{
	static const QRegularExpression portableName(QStringLiteral("^[a-zA-Z0-9._][a-zA-Z0-9._-]*$"));
	return portableName.match(name).hasMatch();
}

void QtProjectWizardContentProjectData::onProjectNameEdited(QString text)
{
	const QRegularExpression regex(QStringLiteral("[^A-Za-z0-9_.-]"));

	const int cursorPosition = m_projectName->cursorPosition();

	text.replace(regex, QStringLiteral("_"));

	m_projectName->setText(text);
	m_projectName->setCursorPosition(cursorPosition);
}
