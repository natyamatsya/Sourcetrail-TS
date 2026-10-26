#include "QtProjectWizardContentPaths.h"
#include "QtMessageBox.h"

#include <QComboBox>

#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettings.h"
#include "utility.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
import srctrl.utility;
#endif

QtProjectWizardContentPaths::QtProjectWizardContentPaths(
	std::shared_ptr<SourceGroupSettings> settings,
	QtProjectWizardWindow* window,
	QtPathListBox::SelectionPolicyType selectionPolicy,
	bool checkMissingPaths)
	: QtProjectWizardContent(window)
	, m_settings(settings)
	, m_selectionPolicy(selectionPolicy)
	, m_checkMissingPaths(checkMissingPaths)
{
}

void QtProjectWizardContentPaths::populate(QGridLayout* layout, int& row)
{
	QLabel* label = createFormLabel(m_titleString);
	layout->addWidget(label, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignTop);

	if (m_helpString.size() > 0)
	{
		addHelpButton(m_titleString, m_helpString, layout, row);
	}

	m_list = new QtPathListBox(this, m_titleString, m_selectionPolicy);

	if (m_makePathsRelativeToProjectFileLocation && m_settings)
	{
		m_list->setRelativeRootDirectory(m_settings->getProjectDirectoryPath());
	}

	layout->addWidget(m_list, row, QtProjectWizardWindow::BACK_COL);
	row++;

	if (m_showFilesString.size() > 0)
	{
		addFilesButton(m_showFilesString, layout, row);
		row++;
	}

	if (m_pathDetector)
	{
		addDetection(layout, row);
		row++;
	}
}

bool QtProjectWizardContentPaths::check()
{
	if (m_checkMissingPaths)
	{
		QString missingPaths;
		std::vector<FilePath> existingPaths;

		for (const FilePath& path: m_list->getPathsAsDisplayed())
		{
			std::vector<FilePath> expandedPaths(1, path);
			if (m_settings)
			{
				expandedPaths = m_settings->makePathsExpandedAndAbsolute(expandedPaths);
			}

			size_t existingCount = 0;
			for (const FilePath& expandedPath: expandedPaths)
			{
				if (!expandedPath.exists())
				{
					missingPaths.append(QString::fromStdString(expandedPath.str() + "\n"));
				}
				else
				{
					existingCount++;
				}
			}

			if (!expandedPaths.empty() && expandedPaths.size() == existingCount)
			{
				existingPaths.push_back(path);
			}
		}

		if (!missingPaths.isEmpty())
		{
			const int answer = m_window->ask(
				QString("Some provided paths do not exist at \"%1\". Do you want to remove them before "
						"continuing?\n\n%2")
					.arg(m_titleString, missingPaths),
				{"Remove", "Keep", "Cancel"});
			if (answer == 0)
			{
				m_list->setPaths(existingPaths);
				save();
			}
			else if (answer == 1)
			{
				return true;
			}
			else
			{
				return false;
			}
		}
	}
	return true;
}

std::vector<FilePath> QtProjectWizardContentPaths::listPaths() const
{
	return m_list->getPathsAsDisplayed();
}

void QtProjectWizardContentPaths::setListPaths(const std::vector<FilePath>& paths)
{
	m_list->setPaths(paths);
}

void QtProjectWizardContentPaths::setTitleString(const QString& title)
{
	m_titleString = title;
}

void QtProjectWizardContentPaths::setHelpString(const QString& help)
{
	m_helpString = help;
}

void QtProjectWizardContentPaths::addDetection(QGridLayout* layout, int row)
{
	std::vector<std::string> detectorNames = m_pathDetector->getWorkingDetectorNames();
	if (!detectorNames.size())
	{
		return;
	}

	QLabel* label = new QLabel(QStringLiteral("Auto detection from:"));

	m_detectorBox = new QComboBox();

	for (const std::string& detectorName: detectorNames)
	{
		m_detectorBox->addItem(detectorName.c_str());
	}

	QPushButton* button = new QPushButton(QStringLiteral("detect"));
	button->setObjectName(QStringLiteral("windowButton"));

	// The row has no label of its own to name them after (the combo box would be called
	// what it shows, "gcc"), and a page can have two of them.
	m_detectorBox->setAccessibleName(m_titleString + QStringLiteral(": auto detection from"));
	button->setAccessibleName(m_titleString + QStringLiteral(": detect"));
	connect(button, &QPushButton::clicked, this, &QtProjectWizardContentPaths::detectionClicked);

	QHBoxLayout* hlayout = new QHBoxLayout();
	hlayout->setContentsMargins(0, 0, 0, 0);
	hlayout->addWidget(label);
	hlayout->addWidget(m_detectorBox);
	hlayout->addWidget(button);

	QWidget* detectionWidget = new QWidget();
	detectionWidget->setLayout(hlayout);

	layout->addWidget(
		detectionWidget, row, QtProjectWizardWindow::BACK_COL, Qt::AlignLeft | Qt::AlignTop);
}

void QtProjectWizardContentPaths::detectionClicked()
{
	std::vector<FilePath> paths = m_pathDetector->getPathsForDetector(
		m_detectorBox->currentText().toStdString());
	std::vector<FilePath> oldPaths = m_list->getPathsAsDisplayed();

	paths = utility::unique(utility::concat(oldPaths, paths));

	detectedPaths(paths);
}

void QtProjectWizardContentPaths::detectedPaths(const std::vector<FilePath>& paths)
{
	m_list->setPaths(paths);
}
