#include "QtProjectWizardContentPathCMakeFileAPI.h"

#include <algorithm>
#include <format>
#include <ranges>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include "CMakeFileAPIReader.h"
#include "ProjectWizardStateMachine.h"
#include "QtLocationPicker.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#endif
#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"
#include "UiPost.h"
#include "WorkPools.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityFile.h"
#include "utilityString.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.settings;
import srctrl.utility;
#endif

namespace
{
// "debug - Debug (Ninja)", as `cmake --list-presets` shows a preset.
QString presetLabel(const CMakeFileAPIReader::PresetInfo& preset)
{
	if (preset.displayName.empty() || preset.displayName == preset.name)
	{
		return QString::fromStdString(preset.name);
	}
	return QString::fromStdString(std::format("{} - {}", preset.name, preset.displayName));
}

// STATIC_LIBRARY -> "static library".
std::string targetTypeName(const std::string& type)
{
	std::string name{utility::toLowerCase(type)};
	std::ranges::replace(name, '_', ' ');
	return name;
}

std::string fileCount(std::size_t count)
{
	return std::format("{} source file{}", count, count == 1 ? "" : "s");
}

QString toHtml(const std::string& text)
{
	return QString::fromStdString(text).toHtmlEscaped();
}
}	 // namespace

QtProjectWizardContentPathCMakeFileAPI::QtProjectWizardContentPathCMakeFileAPI(
	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings, QtProjectWizardWindow* window)
	: QtProjectWizardContent(window), m_settings(settings)
{
	setIsRequired(true);
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
				"<p>After selecting a directory, Sourcetrail lists its configure presets "
				"(as <tt>cmake --list-presets</tt> does) and lets you pick one below.</p>"),
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
		connect(
			m_presetCombo,
			&QComboBox::currentIndexChanged,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onPresetChanged);

		addHelpButton(
			QStringLiteral("Configure Preset"),
			QStringLiteral(
				"<p>The CMake configure preset to use for indexing. Sourcetrail runs</p>"
				"<pre>cmake -S &lt;sourceDir&gt; --preset &lt;name&gt;</pre>"
				"<p>to configure the project and generate the File API reply, when the build "
				"directory has none yet or a CMakeLists.txt, .cmake or presets file changed. "
				"The build directory is the preset's <tt>binaryDir</tt>.</p>"
				"<p>Hidden presets and presets whose <tt>condition</tt> is false on this "
				"computer are not listed.</p>"),
			layout,
			row);
		row++;

		m_presetStatusLabel = new QLabel(QLatin1String(""), this);
		m_presetStatusLabel->setObjectName(QStringLiteral("description"));
		m_presetStatusLabel->setWordWrap(true);
		layout->addWidget(m_presetStatusLabel, row, QtProjectWizardWindow::BACK_COL);
		row++;
	}

	// --- Build directory + buttons ---
	{
		QLabel* label = createFormSubLabel(QStringLiteral("Build Directory"));	// shown, not set
		layout->addWidget(label, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);

		// With a preset its binaryDir, shown; without one a directory to pick.
		QWidget* buildDir = new QWidget(this);
		QVBoxLayout* buildDirLayout = new QVBoxLayout(buildDir);
		buildDirLayout->setContentsMargins(0, 0, 0, 0);
		m_buildDirPicker = new QtLocationPicker(buildDir);
		m_buildDirPicker->setPickDirectory(true);
		m_buildDirPicker->setPlaceholderText(QStringLiteral(
			"A configured build directory, or where CMake should configure the project"));
		buildDirLayout->addWidget(m_buildDirPicker);
		m_buildDirLabel = new QLabel(QLatin1String(""), buildDir);
		m_buildDirLabel->setWordWrap(true);
		m_buildDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		buildDirLayout->addWidget(m_buildDirLabel);
		layout->addWidget(buildDir, row, QtProjectWizardWindow::BACK_COL);
		connect(m_buildDirPicker, &QtLocationPicker::textChanged, this, [this]() { readBuildDirectory(); });
		row++;

		QHBoxLayout* buttons = new QHBoxLayout();
		buttons->setContentsMargins(0, 0, 0, 0);

		QPushButton* refreshButton = new QPushButton(QStringLiteral("Refresh Presets"), this);
		refreshButton->setObjectName(QStringLiteral("refreshButton"));
		connect(
			refreshButton,
			&QPushButton::clicked,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onRefreshPresets);
		buttons->addWidget(refreshButton);

		m_runCMakeButton = new QPushButton(QStringLiteral("Run CMake"), this);
		m_runCMakeButton->setObjectName(QStringLiteral("runCMakeButton"));
		m_runCMakeButton->setToolTip(QStringLiteral(
			"Configures the build directory with the preset now, to list its targets and "
			"configurations here (indexing does it otherwise)"));
		connect(
			m_runCMakeButton,
			&QPushButton::clicked,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onRunCMake);
		buttons->addWidget(m_runCMakeButton);
		buttons->addStretch();

		layout->addLayout(buttons, row, QtProjectWizardWindow::BACK_COL);
		row++;
	}

	// --- Configuration ---
	{
		QLabel* label = createFormSubLabel(QStringLiteral("Configuration"));	// optional
		layout->addWidget(label, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);

		m_configurationCombo = new QComboBox(this);
		m_configurationCombo->setEditable(true);
		m_configurationCombo->setInsertPolicy(QComboBox::NoInsert);
		m_configurationCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		layout->addWidget(m_configurationCombo, row, QtProjectWizardWindow::BACK_COL);
		connect(
			m_configurationCombo,
			&QComboBox::currentTextChanged,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onConfigurationChanged);

		addHelpButton(
			QStringLiteral("Configuration"),
			QStringLiteral(
				"<p>The build configuration whose compile flags are used. A single-config "
				"generator (Ninja, Makefiles) has one, set by <tt>CMAKE_BUILD_TYPE</tt>; a "
				"multi-config generator (Ninja Multi-Config, Xcode, Visual Studio) has "
				"<tt>CMAKE_CONFIGURATION_TYPES</tt>.</p>"
				"<p>Leave it empty to use the first configuration of the build directory.</p>"),
			layout,
			row);
		row++;
	}

	// --- Targets ---
	{
		QLabel* label = createFormSubLabel(QStringLiteral("Targets"));	// optional
		layout->addWidget(label, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);

		m_targetFilterEdit = new QLineEdit(this);
		m_targetFilterEdit->setAccessibleName(QStringLiteral("Target Filter"));
		m_targetFilterEdit->setPlaceholderText(
			QStringLiteral("All targets, or names and patterns like app;core*"));
		m_targetFilterEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
		layout->addWidget(m_targetFilterEdit, row, QtProjectWizardWindow::BACK_COL);
		connect(
			m_targetFilterEdit,
			&QLineEdit::textEdited,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onTargetFilterEdited);

		addHelpButton(
			QStringLiteral("Targets"),
			QStringLiteral(
				"<p>The CMake targets whose source files are indexed: check them in the list "
				"(once the build directory has been configured), or type names and wildcard "
				"patterns separated by <tt>;</tt>, e.g. <tt>app;core*</tt>. Empty means all "
				"targets.</p>"),
			layout,
			row);
		row++;

		m_targetList = new QListWidget(this);
		m_targetList->setAccessibleName(QStringLiteral("Targets"));
		m_targetList->setObjectName(QStringLiteral("targetList"));
		m_targetList->setAttribute(Qt::WA_MacShowFocusRect, false);
		m_targetList->setMaximumHeight(140);
		layout->addWidget(m_targetList, row, QtProjectWizardWindow::BACK_COL);
		connect(
			m_targetList,
			&QListWidget::itemChanged,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onTargetChecked);
		row++;

		m_targetSummaryLabel = new QLabel(QLatin1String(""), this);
		m_targetSummaryLabel->setObjectName(QStringLiteral("description"));
		m_targetSummaryLabel->setWordWrap(true);
		layout->addWidget(m_targetSummaryLabel, row, QtProjectWizardWindow::BACK_COL);
		row++;
	}

	// --- Description ---
	{
		QLabel* description = new QLabel(
			QStringLiteral(
				"Sourcetrail uses the CMake File-based API to obtain per-file compile "
				"commands (include paths, defines, flags) directly from CMake. The project "
				"is re-configured automatically on refresh when CMakeLists.txt, .cmake or "
				"presets files change."),
			this);
		description->setObjectName(QStringLiteral("description"));
		description->setWordWrap(true);
		layout->addWidget(description, row, QtProjectWizardWindow::BACK_COL);
		row++;
	}
}

void QtProjectWizardContentPathCMakeFileAPI::load()
{
	const QString sourceDir{QString::fromStdString(m_settings->getSourceDirectory().str())};
	m_sourceDirPicker->blockSignals(true);
	m_sourceDirPicker->setText(sourceDir);
	m_sourceDirPicker->blockSignals(false);

	m_configurationCombo->blockSignals(true);
	m_configurationCombo->setCurrentText(QString::fromStdString(m_settings->getConfiguration()));
	m_configurationCombo->blockSignals(false);
	m_targetFilterEdit->setText(QString::fromStdString(m_settings->getTargetGlob()));

	m_buildDirPicker->blockSignals(true);
	m_buildDirPicker->setText(QString::fromStdString(m_settings->getBuildDirectory().str()));
	m_buildDirPicker->blockSignals(false);

	// A group without a preset but with a build directory keeps using it; otherwise
	// the first preset (in file order) is selected, and the settings say so too, so
	// what an agent reads is what a person sees.
	const bool withoutPreset{
		m_settings->getPresetName().empty() && !m_settings->getBuildDirectory().empty()};
	repopulatePresets(sourceDir, withoutPreset ? std::string{} : m_settings->getPresetName(), withoutPreset);
	if (m_settings->getPresetName().empty())
	{
		m_settings->setPresetName(selectedPreset());
	}
}

void QtProjectWizardContentPathCMakeFileAPI::save()
{
	m_settings->setSourceDirectory(FilePath(m_sourceDirPicker->getText().toStdString()));
	m_settings->setPresetName(selectedPreset());
	m_settings->setBuildDirectory(FilePath(m_buildDirPicker->getText().trimmed().toStdString()));
	m_settings->setConfiguration(m_configurationCombo->currentText().trimmed().toStdString());
	m_settings->setTargetGlob(m_targetFilterEdit->text().trimmed().toStdString());
}

bool QtProjectWizardContentPathCMakeFileAPI::check()
{
	if (m_sourceDirPicker->getText().trimmed().isEmpty())
	{
		return fail(QStringLiteral("Please choose the CMake source directory."));
	}
	if (m_sourceDir.empty() || !m_sourceDir.exists())
	{
		return fail(QString::fromStdString(
			std::format("The CMake source directory \"{}\" does not exist.", m_sourceDir.str())));
	}

	const std::string presetName{selectedPreset()};
	if (presetName.empty() && m_buildDir.empty())
	{
		return fail(QStringLiteral(
			"Please choose a build directory: one CMake has configured already, or where "
			"Sourcetrail lets CMake configure the project."));
	}
	if (!presetName.empty() && !std::ranges::contains(m_presets, presetName, &CMakeFileAPIReader::PresetInfo::name))
	{
		std::vector<std::string> names{};
		for (const CMakeFileAPIReader::PresetInfo& preset: m_presets)
		{
			names.push_back(preset.name);
		}
		return fail(QString::fromStdString(std::format(
			"The configure preset \"{}\" is not one of the source directory's presets (it is "
			"missing, hidden, or its condition is false here). The presets are: {}.",
			presetName,
			utility::join(names, std::string{", "}))));
	}

	if (m_buildDir.empty())	// a preset's: without one the build directory was checked above
	{
		return fail(QString::fromStdString(std::format(
			"The configure preset \"{}\" has no binaryDir, so its build directory is unknown. "
			"Give it a binaryDir in CMakePresets.json, or pick another preset.",
			presetName)));
	}

	// What the build directory's reply says against the configuration and targets.
	if (m_hasReply)
	{
		const std::string configuration{m_configurationCombo->currentText().trimmed().toStdString()};
		if (!configuration.empty() && !std::ranges::contains(m_configurations, configuration))
		{
			return fail(QString::fromStdString(std::format(
				"The build directory has no configuration \"{}\". Its configurations are: {}.",
				configuration,
				utility::join(m_configurations, std::string{", "}))));
		}
		const std::string filter{m_targetFilterEdit->text().trimmed().toStdString()};
		if (!m_targets.empty() && std::ranges::none_of(m_targets, [&](const Target& target) {
				return CMakeFileAPIReader::matchesTargetFilter(target.name, filter);
			}))
		{
			return fail(QString::fromStdString(
				std::format("No target matches the target filter \"{}\".", filter)));
		}
	}
	return true;
}

void QtProjectWizardContentPathCMakeFileAPI::describe(ProjectWizardView& view) const
{
	if (m_sourceDir.empty() || !m_sourceDir.exists())
	{
		return;
	}

	std::vector<ProjectWizardView::Choice>& presets{view.choices["cmake_preset"]};
	for (const CMakeFileAPIReader::PresetInfo& preset: m_presets)
	{
		std::string label{preset.displayName};
		if (!preset.description.empty())
		{
			label += (label.empty() ? "" : ": ") + preset.description;
		}
		presets.push_back({.value = preset.name, .label = label});
	}
	presets.push_back(
		{.value = "", .label = "no preset: CMake configures cmake_build_directory with -S/-B"});
	view.notes["cmake_preset"] = !noPresetSelected()
		? std::format("build directory: {}", buildDirectoryNote().toStdString())
		: m_buildDir.empty()
		? std::string{"no preset, and no cmake_build_directory chosen yet"}
		: std::format("no preset; build directory {}: {}", m_buildDir.str(), buildDirectoryNote().toStdString());

	if (!m_hasReply)
	{
		view.notes["cmake_target_glob"] =
			"the targets and configurations are listed once CMake has answered Sourcetrail's File API "
			"query in the build directory: the wizard's \"Run CMake\" button, or indexing";
		return;
	}

	std::vector<ProjectWizardView::Choice>& configurations{view.choices["cmake_configuration"]};
	for (const std::string& configuration: m_configurations)
	{
		configurations.push_back({.value = configuration, .label = {}});
	}

	std::vector<ProjectWizardView::Choice>& targets{view.choices["cmake_target_glob"]};
	for (const Target& target: m_targets)
	{
		targets.push_back(
			{.value = target.name,
			 .label = std::format("{}, {}", targetTypeName(target.type), fileCount(target.sourceFileCount))});
	}
	view.notes["cmake_target_glob"] =
		"names and wildcard patterns separated by ';' (empty: all targets); " +
		targetSummary().toStdString();
}

void QtProjectWizardContentPathCMakeFileAPI::onSourceDirChanged(const QString& text)
{
	repopulatePresets(text, selectedPreset(), noPresetSelected());
}

void QtProjectWizardContentPathCMakeFileAPI::onRefreshPresets()
{
	repopulatePresets(m_sourceDirPicker->getText(), selectedPreset(), noPresetSelected());
}

void QtProjectWizardContentPathCMakeFileAPI::onPresetChanged()
{
	const int index{m_presetCombo->currentIndex()};
	const std::string presetName{selectedPreset()};
	const auto preset{std::ranges::find(m_presets, presetName, &CMakeFileAPIReader::PresetInfo::name)};
	if (noPresetSelected())
	{
		m_presetStatusLabel->setText(
			m_presets.empty()
				? QStringLiteral("The source directory has no CMakePresets.json: Sourcetrail uses "
								 "the build directory below. It runs <tt>cmake -S &lt;source&gt; -B "
								 "&lt;build&gt;</tt> when the build directory has no File API reply.")
				: QStringLiteral("Sourcetrail uses the build directory below and runs <tt>cmake -S "
								 "&lt;source&gt; -B &lt;build&gt;</tt> when it has no File API reply."));
	}
	else if (index >= 0 && preset == m_presets.end())
	{
		m_presetStatusLabel->setText(QStringLiteral(
			"<span style=\"color:red\">Not one of the presets of the source directory.</span>"));
	}
	else
	{
		m_presetStatusLabel->setText(
			preset != m_presets.end() ? toHtml(preset->description) : QString{});
	}
	readBuildDirectory();
}

void QtProjectWizardContentPathCMakeFileAPI::onConfigurationChanged()
{
	if (m_hasReply)
	{
		readTargets();
	}
}

void QtProjectWizardContentPathCMakeFileAPI::onTargetFilterEdited()
{
	showTargets();
}

void QtProjectWizardContentPathCMakeFileAPI::onTargetChecked()
{
	// The checked targets become the filter; all of them, the empty filter.
	std::vector<std::string> checked{};
	int checkable{0};
	for (int i = 0; i < m_targetList->count(); i++)
	{
		const QListWidgetItem* item{m_targetList->item(i)};
		if (!item->flags().testFlag(Qt::ItemIsEnabled))
		{
			continue;
		}
		checkable++;
		if (item->checkState() == Qt::Checked)
		{
			checked.push_back(item->data(Qt::UserRole).toString().toStdString());
		}
	}
	m_targetFilterEdit->setText(
		static_cast<int>(checked.size()) == checkable ? QString{}
													  : QString::fromStdString(utility::join(checked, ';')));
	updateTargetSummary();
}

void QtProjectWizardContentPathCMakeFileAPI::onRunCMake()
{
	if (m_runningCMake || m_buildDir.empty())
	{
		return;
	}
	m_runningCMake = true;
	m_runCMakeButton->setEnabled(false);
	m_buildDirLabel->setText(buildDirectoryNote().toHtmlEscaped());

	execution::runOnWorkPool([this,
							  buildDir = m_buildDir,
							  sourceDir = m_sourceDir,
							  presetName = selectedPreset()]() {
		CMakeFileAPIReader reader{buildDir};
		const auto configured{reader.ensureReply({}, sourceDir, presetName, true)};
		const std::string error{
			configured ? std::string{}
					   : std::format(
							 "Running CMake{} failed: {}.\n\n{}",
							 presetName.empty() ? std::string{} : std::format(" with preset \"{}\"", presetName),
							 to_std_sv(configured.error().code),
							 configured.error().message)};
		execution::qt::onUi(this, [this, buildDir, error]() {
			m_runningCMake = false;
			m_runCMakeButton->setEnabled(true);
			m_failedBuildDir = error.empty() ? FilePath{} : buildDir;
			readBuildDirectory();
			m_window->contentChanged();
			if (!error.empty())
			{
				m_window->tell(QString::fromStdString(error));
			}
		});
	});
}

void QtProjectWizardContentPathCMakeFileAPI::repopulatePresets(
	const QString& sourceDir, const std::string& presetToKeep, const bool withoutPreset)
{
	m_sourceDir = utility::getExpandedAndAbsolutePath(
		FilePath(sourceDir.toStdString()), m_settings->getProjectDirectoryPath());
	m_presets = m_sourceDir.empty() || !m_sourceDir.exists()
		? std::vector<CMakeFileAPIReader::PresetInfo>{}
		: CMakeFileAPIReader::discoverPresetInfos(m_sourceDir);

	m_presetCombo->blockSignals(true);
	m_presetCombo->clear();
	for (const CMakeFileAPIReader::PresetInfo& preset: m_presets)
	{
		m_presetCombo->addItem(presetLabel(preset), QString::fromStdString(preset.name));
		m_presetCombo->setItemData(
			m_presetCombo->count() - 1, QString::fromStdString(preset.description), Qt::ToolTipRole);
	}
	// A preset that is not (any more) among them stays selected, marked, so the check
	// says so instead of another preset being used without a word.
	if (!presetToKeep.empty() &&
		!std::ranges::contains(m_presets, presetToKeep, &CMakeFileAPIReader::PresetInfo::name))
	{
		m_presetCombo->addItem(
			QString::fromStdString(std::format("{} (not found)", presetToKeep)),
			QString::fromStdString(presetToKeep));
	}
	if (!m_sourceDir.empty() && m_sourceDir.exists())
	{
		m_presetCombo->addItem(QStringLiteral("No preset: the build directory below"), QString{});
	}
	m_presetCombo->setCurrentIndex(
		withoutPreset || m_presets.empty() ? m_presetCombo->count() - 1
		: presetToKeep.empty()             ? 0
							   : m_presetCombo->findData(QString::fromStdString(presetToKeep)));
	m_presetCombo->blockSignals(false);

	if (m_sourceDir.empty() || !m_sourceDir.exists())
	{
		m_presetStatusLabel->setText(QLatin1String(""));
		m_buildDir = FilePath{};
		m_hasReply = false;
		m_configured = false;
		m_configureFailed = false;
		m_configurations.clear();
		m_targets.clear();
		readBuildDirectory();
		return;
	}
	onPresetChanged();
}

std::string QtProjectWizardContentPathCMakeFileAPI::selectedPreset() const
{
	return m_presetCombo->currentData().toString().toStdString();
}

bool QtProjectWizardContentPathCMakeFileAPI::noPresetSelected() const
{
	return m_presetCombo->currentIndex() >= 0 && selectedPreset().empty();
}

void QtProjectWizardContentPathCMakeFileAPI::readBuildDirectory()
{
	const std::string presetName{selectedPreset()};
	const QString pickedBuildDir{m_buildDirPicker->getText().trimmed()};
	if (noPresetSelected())
	{
		m_buildDir = pickedBuildDir.isEmpty()
			? FilePath{}
			: utility::getExpandedAndAbsolutePath(
				  FilePath(pickedBuildDir.toStdString()), m_settings->getProjectDirectoryPath());
	}
	else
	{
		m_buildDir = presetName.empty() || m_sourceDir.empty()
			? FilePath{}
			: CMakeFileAPIReader::resolveBinaryDir(m_sourceDir, presetName);
	}
	m_buildDirPicker->setVisible(noPresetSelected());
	const CMakeFileAPIReader reader{m_buildDir};
	m_hasReply = !m_buildDir.empty() && reader.hasReply();
	// A configure that failed leaves CMakeCache.txt behind: configured means CMake generated
	// the build directory, too, as ProjectDetector counts it.
	m_configured = !m_buildDir.empty() && reader.isConfigured();
	m_configureFailed =
		!m_buildDir.empty() && (reader.lastConfigureFailed() || m_failedBuildDir == m_buildDir);

	m_buildDirLabel->setText(buildDirectoryNote().toHtmlEscaped());
	m_runCMakeButton->setEnabled(!m_buildDir.empty() && !m_runningCMake);

	m_configurations = m_hasReply ? CMakeFileAPIReader{m_buildDir}.getConfigurations()
								  : std::vector<std::string>{};
	const QString configuration{m_configurationCombo->currentText()};
	m_configurationCombo->blockSignals(true);
	m_configurationCombo->clear();
	for (const std::string& name: m_configurations)
	{
		m_configurationCombo->addItem(QString::fromStdString(name));
	}
	m_configurationCombo->setCurrentText(configuration);
	m_configurationCombo->lineEdit()->setPlaceholderText(
		m_configurations.empty()
			? QStringLiteral("The first configuration of the build directory")
			: QString::fromStdString(
				  std::format("The first configuration: {}", m_configurations.front())));
	m_configurationCombo->blockSignals(false);

	readTargets();
}

void QtProjectWizardContentPathCMakeFileAPI::readTargets()
{
	m_targets.clear();
	if (m_hasReply)
	{
		const std::string configuration{m_configurationCombo->currentText().trimmed().toStdString()};
		if (const auto sources{CMakeFileAPIReader{m_buildDir}.getSourcesDetailed(configuration, {})})
		{
			for (const CMakeFileAPIReader::TargetEntry& target: sources->targets)
			{
				// CMake's helper targets for C++20 module collation are not the project's.
				if (!target.name.contains("@synth_"))
				{
					m_targets.push_back({.name = target.name, .type = target.type, .sourceFileCount = 0});
				}
			}
			for (const CMakeFileAPIReader::SourceEntry& entry: sources->entries)
			{
				const auto target{std::ranges::find(m_targets, entry.targetName, &Target::name)};
				if (target != m_targets.end() && entry.compileGroup && !entry.isGenerated)
				{
					target->sourceFileCount++;
				}
			}
		}
	}
	showTargets();
}

void QtProjectWizardContentPathCMakeFileAPI::showTargets()
{
	const std::string filter{m_targetFilterEdit->text().trimmed().toStdString()};
	m_targetList->blockSignals(true);
	m_targetList->clear();
	for (const Target& target: m_targets)
	{
		QListWidgetItem* item{new QListWidgetItem(
			QString::fromStdString(std::format(
				"{}  ({}, {})",
				target.name,
				targetTypeName(target.type),
				fileCount(target.sourceFileCount))),
			m_targetList)};
		item->setData(Qt::UserRole, QString::fromStdString(target.name));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		if (target.sourceFileCount == 0)
		{
			item->setFlags(item->flags() & ~Qt::ItemIsEnabled);	// nothing to index either way
		}
		item->setCheckState(
			CMakeFileAPIReader::matchesTargetFilter(target.name, filter) ? Qt::Checked : Qt::Unchecked);
	}
	m_targetList->blockSignals(false);
	m_targetList->setVisible(!m_targets.empty());
	updateTargetSummary();
}

void QtProjectWizardContentPathCMakeFileAPI::updateTargetSummary()
{
	m_targetSummaryLabel->setText(targetSummary().toHtmlEscaped());
}

QString QtProjectWizardContentPathCMakeFileAPI::targetSummary() const
{
	if (!m_hasReply)
	{
		return m_buildDir.empty()
			? QString{}
			: QStringLiteral(
				  "The targets are listed once CMake has answered Sourcetrail's query in the build "
				  "directory: Run CMake, or index the project.");
	}
	const std::string filter{m_targetFilterEdit->text().trimmed().toStdString()};
	std::size_t matched{0};
	std::size_t files{0};
	for (const Target& target: m_targets)
	{
		if (CMakeFileAPIReader::matchesTargetFilter(target.name, filter))
		{
			matched++;
			files += target.sourceFileCount;
		}
	}
	return QString::fromStdString(std::format(
		"{} of {} target{}, {} to index.",
		matched,
		m_targets.size(),
		m_targets.size() == 1 ? "" : "s",
		fileCount(files)));
}

QString QtProjectWizardContentPathCMakeFileAPI::buildDirectoryNote() const
{
	const std::string presetName{selectedPreset()};
	if (presetName.empty())
	{
		if (!noPresetSelected() || m_buildDir.empty())
		{
			return {};
		}
		return m_runningCMake  ? QStringLiteral("Running CMake...")
			: m_configureFailed ? QStringLiteral("CMake failed to configure it: Run CMake shows its errors.")
			: m_hasReply        ? QStringLiteral("Configured.")
			: m_configured      ? QStringLiteral("Configured by CMake; Sourcetrail asks it for the targets when indexing.")
								: QStringLiteral("Not configured yet: Sourcetrail runs CMake when indexing.");
	}
	if (!std::ranges::contains(m_presets, presetName, &CMakeFileAPIReader::PresetInfo::name))
	{
		return QStringLiteral("Unknown: the preset is not one of the source directory's presets.");
	}
	if (m_buildDir.empty())
	{
		return QStringLiteral("Unknown: the preset has no binaryDir.");
	}
	std::string state{
		m_runningCMake    ? "running CMake..."
		: m_configureFailed ? "CMake failed to configure it; Run CMake shows its errors"
		: m_hasReply        ? "configured"
		: m_configured      ? "configured by CMake; Sourcetrail asks it for the targets when indexing"
							: "not configured yet; Sourcetrail runs CMake when indexing"};
	return QString::fromStdString(std::format("{} ({})", m_buildDir.str(), state));
}

bool QtProjectWizardContentPathCMakeFileAPI::fail(const QString& message)
{
	// Next to the fields, and as the dialog every other page shows (which agent control reports).
	m_presetStatusLabel->setText(
		QStringLiteral("<span style=\"color:red\">") + message.toHtmlEscaped() + QStringLiteral("</span>"));
	m_window->tell(message);
	return false;
}
