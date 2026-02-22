#include "QtProjectWizardContentPathCMakeFileAPI.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>

#include "CMakeFileAPIReader.h"
#include "QtLocationPicker.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"
#include "utilityFile.h"

QtProjectWizardContentPathCMakeFileAPI::QtProjectWizardContentPathCMakeFileAPI(
	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings, QtProjectWizardWindow* window)
	: QtProjectWizardContent(window), m_settings(settings)
{
}

void QtProjectWizardContentPathCMakeFileAPI::populate(QGridLayout* layout, int& row)
{
	// --- Source directory ---
	{
		QLabel* label = createFormLabel(QStringLiteral("CMake Source Directory"));
		layout->addWidget(label, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);

		m_sourceDirPicker = new QtLocationPicker(this);
		m_sourceDirPicker->setPickDirectory(true);
		m_sourceDirPicker->setPlaceholderText(
			QStringLiteral("Directory containing CMakeLists.txt and CMakePresets.json"));
		layout->addWidget(m_sourceDirPicker, row, QtProjectWizardWindow::BACK_COL);

		addHelpButton(
			QStringLiteral("CMake Source Directory"),
			QStringLiteral(
				"<p>The root directory of the CMake project — the folder that contains "
				"<b>CMakeLists.txt</b> and <b>CMakePresets.json</b>.</p>"
				"<p>After selecting a directory, Sourcetrail will discover all non-hidden "
				"configure presets and let you pick one below.</p>"),
			layout,
			row);
		row++;

		connect(
			m_sourceDirPicker,
			&QtLocationPicker::textChanged,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onSourceDirChanged);
	}

	// --- Preset combo ---
	{
		QLabel* label = createFormLabel(QStringLiteral("Configure Preset"));
		layout->addWidget(label, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);

		m_presetCombo = new QComboBox(this);
		m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		layout->addWidget(m_presetCombo, row, QtProjectWizardWindow::BACK_COL);

		addHelpButton(
			QStringLiteral("Configure Preset"),
			QStringLiteral(
				"<p>The CMake configure preset to use for indexing. Sourcetrail will run</p>"
				"<pre>cmake -S &lt;sourceDir&gt; --preset &lt;name&gt;</pre>"
				"<p>to configure the project and generate the File API reply. The binary "
				"directory is resolved automatically from the preset definition.</p>"),
			layout,
			row);
		row++;
	}

	// --- Refresh button + status label ---
	{
		QPushButton* refreshButton = new QPushButton(QStringLiteral("Refresh Presets"), this);
		refreshButton->setObjectName(QStringLiteral("refreshButton"));
		layout->addWidget(refreshButton, row, QtProjectWizardWindow::BACK_COL, Qt::AlignLeft);
		connect(
			refreshButton,
			&QPushButton::clicked,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onRefreshPresets);

		m_presetStatusLabel = new QLabel(QLatin1String(""), this);
		m_presetStatusLabel->setWordWrap(true);
		layout->addWidget(m_presetStatusLabel, row, QtProjectWizardWindow::BACK_COL, Qt::AlignLeft);
		row++;
	}

	// --- Description ---
	{
		QLabel* description = new QLabel(
			QStringLiteral(
				"Sourcetrail will use the CMake File-based API to obtain per-file compile "
				"commands (include paths, defines, flags) directly from CMake. The project "
				"will be re-configured automatically when CMakeLists.txt or .cmake files "
				"change on refresh."),
			this);
		description->setObjectName(QStringLiteral("description"));
		description->setWordWrap(true);
		layout->addWidget(description, row, QtProjectWizardWindow::BACK_COL);
		row++;
	}
}

void QtProjectWizardContentPathCMakeFileAPI::load()
{
	const std::string sourceDir = m_settings->getSourceDirectory().str();
	m_sourceDirPicker->setText(QString::fromStdString(sourceDir));

	repopulatePresets(QString::fromStdString(sourceDir));

	const std::string preset = m_settings->getPresetName();
	if (!preset.empty())
		m_presetCombo->setCurrentText(QString::fromStdString(preset));
}

void QtProjectWizardContentPathCMakeFileAPI::save()
{
	m_settings->setSourceDirectory(FilePath(m_sourceDirPicker->getText().toStdString()));
	m_settings->setPresetName(m_presetCombo->currentText().toStdString());
}

bool QtProjectWizardContentPathCMakeFileAPI::check()
{
	const FilePath sourceDir = utility::getExpandedAndAbsolutePath(
		FilePath(m_sourceDirPicker->getText().toStdString()),
		m_settings->getProjectDirectoryPath());

	if (sourceDir.empty() || !sourceDir.exists())
	{
		m_presetStatusLabel->setText(
			QStringLiteral("<span style=\"color:red\">The source directory does not exist.</span>"));
		return false;
	}

	if (m_presetCombo->currentText().isEmpty())
	{
		m_presetStatusLabel->setText(
			QStringLiteral("<span style=\"color:red\">Please select a configure preset.</span>"));
		return false;
	}

	return true;
}

void QtProjectWizardContentPathCMakeFileAPI::onSourceDirChanged(const QString& text)
{
	repopulatePresets(text);
}

void QtProjectWizardContentPathCMakeFileAPI::onRefreshPresets()
{
	repopulatePresets(m_sourceDirPicker->getText());
}

void QtProjectWizardContentPathCMakeFileAPI::repopulatePresets(const QString& sourceDir)
{
	if (!m_presetCombo || !m_presetStatusLabel)
		return;

	const FilePath dir = utility::getExpandedAndAbsolutePath(
		FilePath(sourceDir.toStdString()), m_settings->getProjectDirectoryPath());

	if (dir.empty() || !dir.exists())
	{
		m_presetCombo->clear();
		m_presetStatusLabel->setText(QLatin1String(""));
		return;
	}

	const std::string previousPreset = m_presetCombo->currentText().toStdString();
	const std::vector<std::string> presets = CMakeFileAPIReader::discoverPresets(dir);

	m_presetCombo->clear();
	for (const auto& name : presets)
		m_presetCombo->addItem(QString::fromStdString(name));

	if (!previousPreset.empty())
		m_presetCombo->setCurrentText(QString::fromStdString(previousPreset));

	if (presets.empty())
	{
		m_presetStatusLabel->setText(
			QStringLiteral("No presets found. Make sure CMakePresets.json exists in the source directory."));
	}
	else
	{
		m_presetStatusLabel->setText(
			QString::number(static_cast<int>(presets.size())) +
			QStringLiteral(" preset(s) found."));
	}
}
