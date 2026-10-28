#include "QtProjectWizardContentPathCMakeFileAPI.h"

#include <algorithm>
#include <array>
#include <set>
#include <tuple>
#include <format>
#include <ranges>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include "CMakeFileAPIReader.h"
#include "EditorCommand.h"
#include "ProjectDetector.h"
#include "ProjectWizardStateMachine.h"
#include "SourceGroupType.h"
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

// What a preset configures with: generator, toolchain, build directory, cache variables.
// One line each, for the combo's tooltip and below it.
std::vector<std::string> presetDetails(const CMakeFileAPIReader::PresetInfo& preset)
{
	std::vector<std::string> details;
	details.push_back(std::format("Generator: {}", preset.generator.empty() ? "CMake's default" : preset.generator));
	if (!preset.toolchainFile.empty())
	{
		details.push_back(std::format("Toolchain: {}", preset.toolchainFile));
	}
	if (!preset.binaryDir.empty())
	{
		details.push_back(std::format("Build directory: {}", preset.binaryDir));
	}
	if (!preset.cacheVariables.empty())
	{
		std::string variables;
		for (const auto& [name, value]: preset.cacheVariables)
		{
			variables += std::format("{}-D{}={}", variables.empty() ? "" : " ", name, value);
		}
		details.push_back(std::format("Cache variables: {}", variables));
	}
	return details;
}

// The arguments as one line to edit: a word with blanks or quotes in double quotes.
QString joinArguments(const std::vector<std::string>& arguments)
{
	QStringList words;
	for (const std::string& argument: arguments)
	{
		QString word{QString::fromStdString(argument)};
		if (word.isEmpty() || word.contains(QLatin1Char(' ')) || word.contains(QLatin1Char('"')) ||
			word.contains(QLatin1Char('\'')))
		{
			word.replace(QLatin1String("\\"), QLatin1String("\\\\"));
			word.replace(QLatin1String("\""), QLatin1String("\\\""));
			word = QLatin1Char('"') + word + QLatin1Char('"');
		}
		words << word;
	}
	return words.join(QLatin1Char(' '));
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

		// What was proposed from the source directory, as New Project from Folder does.
		m_detectedLabel = new QLabel(QLatin1String(""), this);
		m_detectedLabel->setObjectName(QStringLiteral("description"));
		m_detectedLabel->setAccessibleName(QStringLiteral("Detected"));
		m_detectedLabel->setWordWrap(true);
		m_detectedLabel->hide();
		layout->addWidget(m_detectedLabel, row, QtProjectWizardWindow::BACK_COL);
		row++;

		m_detectTimer = new QTimer(this);
		m_detectTimer->setSingleShot(true);
		m_detectTimer->setInterval(400);
		connect(m_detectTimer, &QTimer::timeout, this, &QtProjectWizardContentPathCMakeFileAPI::onDetect);
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
		m_refreshPresetsButton = refreshButton;
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

		// What CMake said when it failed, until it configures the build directory.
		m_configureErrorRow = new QWidget(this);
		m_configureErrorRow->setObjectName(QStringLiteral("cmakeError"));
		QVBoxLayout* errorLayout = new QVBoxLayout(m_configureErrorRow);
		errorLayout->setContentsMargins(0, 0, 0, 0);
		m_configureErrorLabel = new QLabel(QLatin1String(""), m_configureErrorRow);
		m_configureErrorLabel->setAccessibleName(QStringLiteral("CMake Error"));
		m_configureErrorLabel->setWordWrap(true);
		m_configureErrorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		errorLayout->addWidget(m_configureErrorLabel);
		QPushButton* showLogButton = new QPushButton(QStringLiteral("Show Log"), m_configureErrorRow);
		showLogButton->setObjectName(QStringLiteral("showCMakeLogButton"));
		showLogButton->setToolTip(QStringLiteral("All CMake printed when it configured the build directory last"));
		connect(showLogButton, &QPushButton::clicked, this, &QtProjectWizardContentPathCMakeFileAPI::onShowCMakeLog);
		QHBoxLayout* logButtons = new QHBoxLayout();
		logButtons->setContentsMargins(0, 0, 0, 0);
		logButtons->addWidget(showLogButton);
		logButtons->addStretch();
		errorLayout->addLayout(logButtons);
		layout->addWidget(m_configureErrorRow, row, QtProjectWizardWindow::BACK_COL);
		m_configureErrorRow->hide();
		row++;
	}

	// --- CMake arguments (without a preset) ---
	{
		m_cmakeArgumentsLabel = createFormSubLabel(QStringLiteral("CMake Arguments"));	// optional
		layout->addWidget(m_cmakeArgumentsLabel, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);

		m_cmakeArgumentsRow = new QWidget(this);
		QHBoxLayout* argumentsLayout = new QHBoxLayout(m_cmakeArgumentsRow);
		argumentsLayout->setContentsMargins(0, 0, 0, 0);
		m_cmakeArgumentsEdit = new QLineEdit(m_cmakeArgumentsRow);
		m_cmakeArgumentsEdit->setObjectName(QStringLiteral("cmakeArguments"));
		m_cmakeArgumentsEdit->setAccessibleName(QStringLiteral("CMake Arguments"));
		m_cmakeArgumentsEdit->setPlaceholderText(
			QStringLiteral("Cache variables and options, e.g. -DBUILD_TESTS=OFF -G Ninja"));
		m_cmakeArgumentsEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
		argumentsLayout->addWidget(m_cmakeArgumentsEdit);
		layout->addWidget(m_cmakeArgumentsRow, row, QtProjectWizardWindow::BACK_COL);

		m_cmakeArgumentsHelp = addHelpButton(
			QStringLiteral("CMake Arguments"),
			QStringLiteral(
				"<p>What Sourcetrail adds when it configures the build directory without a "
				"preset:</p><pre>cmake -S &lt;source&gt; -B &lt;build&gt; &lt;arguments&gt;</pre>"
				"<p>Cache variables like <tt>-DLEVELDB_BUILD_TESTS=OFF</tt>, a generator "
				"(<tt>-G Ninja</tt>), a toolchain file. Separated by blanks; put an argument with "
				"blanks in quotes. They are used by Run CMake and when indexing re-configures the "
				"project; they are not passed through a shell.</p>"
				"<p>With a preset, set them in the preset (CMakeUserPresets.json) instead.</p>"),
			layout,
			row);
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

		// A long list (CLI11 has 92 targets, 30 of them CTest's): narrowed by name, the
		// targets without sources hidden unless asked for.
		QWidget* listTools = new QWidget(this);
		QHBoxLayout* listToolsLayout = new QHBoxLayout(listTools);
		listToolsLayout->setContentsMargins(0, 0, 0, 0);
		m_targetListFilterEdit = new QLineEdit(listTools);
		m_targetListFilterEdit->setObjectName(QStringLiteral("targetListFilter"));
		m_targetListFilterEdit->setAccessibleName(QStringLiteral("Find Targets"));
		m_targetListFilterEdit->setPlaceholderText(QStringLiteral("Find targets by name"));
		m_targetListFilterEdit->setClearButtonEnabled(true);
		m_targetListFilterEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
		listToolsLayout->addWidget(m_targetListFilterEdit, 1);
		m_showEmptyTargetsCheck = new QCheckBox(QStringLiteral("Show targets without sources"), listTools);
		m_showEmptyTargetsCheck->setObjectName(QStringLiteral("showEmptyTargets"));
		m_showEmptyTargetsCheck->setAccessibleName(QStringLiteral("Show targets without sources"));
		listToolsLayout->addWidget(m_showEmptyTargetsCheck);
		layout->addWidget(listTools, row, QtProjectWizardWindow::BACK_COL);
		connect(
			m_targetListFilterEdit,
			&QLineEdit::textChanged,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onTargetListFilterEdited);
		connect(
			m_showEmptyTargetsCheck,
			&QCheckBox::toggled,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onTargetListFilterEdited);
		row++;

		m_targetList = new QListWidget(this);
		m_targetList->setAccessibleName(QStringLiteral("Targets"));
		m_targetList->setObjectName(QStringLiteral("targetList"));
		m_targetList->setAttribute(Qt::WA_MacShowFocusRect, false);
		m_targetList->setMaximumHeight(220);
		layout->addWidget(m_targetList, row, QtProjectWizardWindow::BACK_COL);
		connect(
			m_targetList,
			&QListWidget::itemChanged,
			this,
			&QtProjectWizardContentPathCMakeFileAPI::onTargetChecked);
		row++;

		m_indexLinkedTargetsCheck = new QCheckBox(QStringLiteral("Also index the targets these link to"), this);
		m_indexLinkedTargetsCheck->setObjectName(QStringLiteral("indexLinkedTargets"));
		m_indexLinkedTargetsCheck->setAccessibleName(QStringLiteral("Also index the targets these link to"));
		m_indexLinkedTargetsCheck->setToolTip(QStringLiteral(
			"With some targets checked: also index the targets they link to or depend on, transitively. "
			"A program checked alone otherwise misses the definitions its libraries compile (with "
			"CLI11_PRECOMPILED, every one of CLI11's)."));
		layout->addWidget(m_indexLinkedTargetsCheck, row, QtProjectWizardWindow::BACK_COL);
		connect(m_indexLinkedTargetsCheck, &QCheckBox::toggled, this, [this]() { showTargets(); });
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
	m_indexLinkedTargetsCheck->blockSignals(true);
	m_indexLinkedTargetsCheck->setChecked(m_settings->getIndexLinkedTargets());
	m_indexLinkedTargetsCheck->blockSignals(false);

	m_buildDirPicker->blockSignals(true);
	m_buildDirPicker->setText(QString::fromStdString(m_settings->getBuildDirectory().str()));
	m_buildDirPicker->blockSignals(false);
	m_cmakeArgumentsEdit->setText(joinArguments(m_settings->getCMakeArguments()));

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
	m_settings->setIndexLinkedTargets(m_indexLinkedTargetsCheck->isChecked());
	m_settings->setCMakeArguments(cmakeArguments().value_or(std::vector<std::string>{}));
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
	if (const auto arguments = cmakeArguments(); !arguments)
	{
		return fail(QString::fromStdString(arguments.error()));
	}
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
	std::string presetNote;
	if (const auto preset{std::ranges::find(m_presets, selectedPreset(), &CMakeFileAPIReader::PresetInfo::name)};
		preset != m_presets.end())
	{
		for (const std::string& line: presetDetails(*preset))
		{
			presetNote += "; " + line;
		}
	}
	view.notes["cmake_preset"] = !noPresetSelected()
		? std::format("build directory: {}{}", buildDirectoryNote().toStdString(), presetNote)
		: m_buildDir.empty()
		? std::string{"no preset, and no cmake_build_directory chosen yet"}
		: std::format("no preset; build directory {}: {}", m_buildDir.str(), buildDirectoryNote().toStdString());

	if (m_configureFailed)
	{
		view.notes["cmake_error"] = m_configureError.empty()
			? std::string{"CMake failed to configure the build directory"}
			: m_configureError;
		if (m_missesSubmodules)
		{
			view.notes["cmake_error"] +=
				" (a directory it adds is missing: run 'git submodule update --init --recursive' in the "
				"source directory)";
		}
	}
	if (noPresetSelected())
	{
		view.notes["cmake_arguments"] =
			"without a preset: what the configure adds to cmake -S <source> -B <build>, one argument each "
			"(e.g. -DBUILD_TESTS=OFF)";
	}

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
			 .label = std::format(
				 "{}, {}{}",
				 targetTypeName(target.type),
				 fileCount(target.sourceFileCount),
				 target.standard.empty() ? std::string{} : ", " + target.standard)});
	}
	view.notes["cmake_target_glob"] =
		"names and wildcard patterns separated by ';' (empty: all targets); " +
		targetSummary().toStdString();
}

void QtProjectWizardContentPathCMakeFileAPI::onSourceDirChanged(const QString& text)
{
	repopulatePresets(text, selectedPreset(), noPresetSelected());
	// A source directory picked (not loaded): propose the rest from it.
	m_detectTimer->start();
}

void QtProjectWizardContentPathCMakeFileAPI::onDetect()
{
	if (m_sourceDir.empty() || !m_sourceDir.exists() || m_detecting)
	{
		return;
	}
	m_detecting = true;
	const std::string folder{m_sourceDir.str()};
	const std::string projectLocation{m_settings->getProjectDirectoryPath().str()};
	execution::runOnWorkPool([this, folder, projectLocation]() {
		const auto proposal{detectProject(folder, {.projectLocation = projectLocation, .hasExecutable = {}})};
		execution::qt::onUi(this, [this, folder, proposal]() {
			m_detecting = false;
			// Picked another meanwhile: that one's detection follows.
			if (proposal && m_sourceDir.str() == folder)
			{
				applyDetection(*proposal);
			}
		});
	});
}

void QtProjectWizardContentPathCMakeFileAPI::applyDetection(const ProjectProposal& proposal)
{
	if (proposal.sourceGroupType != SourceGroupType::CXX_CMAKE_FILE_API)
	{
		m_detectedLabel->hide();
		return;
	}
	const auto text = [&](const std::string& field) -> std::string {
		const auto it{proposal.fields.find(field)};
		return it != proposal.fields.end() && std::holds_alternative<std::string>(it->second)
			? std::get<std::string>(it->second)
			: std::string{};
	};
	const auto list = [&](const std::string& field) -> std::vector<std::string> {
		const auto it{proposal.fields.find(field)};
		return it != proposal.fields.end() && std::holds_alternative<std::vector<std::string>>(it->second)
			? std::get<std::vector<std::string>>(it->second)
			: std::vector<std::string>{};
	};

	// What is on the page so far, then what the folder suggests where nothing is set.
	m_window->saveContent();
	std::vector<std::string> filled;
	m_settings->setPresetName(text("cmake_preset"));
	if (const std::string buildDir{text("cmake_build_directory")};
		!buildDir.empty() && m_settings->getBuildDirectory().empty())
	{
		m_settings->setBuildDirectory(FilePath(buildDir));
		filled.push_back("the build directory");
	}
	if (const std::vector<std::string> headers{list("indexed_header_paths")};
		!headers.empty() && m_settings->getIndexedHeaderPaths().empty())
	{
		std::vector<FilePath> paths;
		for (const std::string& header: headers)
		{
			paths.emplace_back(header);
		}
		m_settings->setIndexedHeaderPaths(paths);
		filled.push_back("the headers to index");
	}
	if (const std::string name{m_settings->getName()};
		!proposal.projectName.empty() &&
		(name.empty() || name == sourceGroupTypeToString(SourceGroupType::CXX_CMAKE_FILE_API)))
	{
		m_settings->setName(proposal.projectName);
		filled.push_back("the name");
	}
	m_window->loadContent();

	std::string detected{proposal.reasons.empty() ? std::string{} : proposal.reasons.front()};
	if (!filled.empty())
	{
		detected += std::format(" (filled in: {})", utility::join(filled, std::string{", "}));
	}
	m_detectedLabel->setText(toHtml(detected));
	m_detectedLabel->setVisible(!detected.empty());
	m_window->contentChanged();
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
	else if (preset != m_presets.end())
	{
		// What the preset means, not only its name: its description, then how it configures.
		QString text{toHtml(preset->description)};
		for (const std::string& line: presetDetails(*preset))
		{
			text += (text.isEmpty() ? QString{} : QStringLiteral("<br>")) + toHtml(line);
		}
		m_presetStatusLabel->setText(text);
	}
	else
	{
		m_presetStatusLabel->setText(QString{});
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

void QtProjectWizardContentPathCMakeFileAPI::onTargetListFilterEdited()
{
	const QString text{m_targetListFilterEdit->text().trimmed()};
	for (int i = 0; i < m_targetList->count(); i++)
	{
		QListWidgetItem* item{m_targetList->item(i)};
		if (!item->data(Qt::UserRole).isValid())	// a group's heading: shown with its rows
		{
			continue;
		}
		const bool empty{!item->flags().testFlag(Qt::ItemIsEnabled)};
		item->setHidden(
			(empty && !m_showEmptyTargetsCheck->isChecked()) ||
			(!text.isEmpty() && !item->data(Qt::UserRole).toString().contains(text, Qt::CaseInsensitive)));
	}
	// A heading goes with the last of its rows.
	for (int i = 0; i < m_targetList->count(); i++)
	{
		QListWidgetItem* heading{m_targetList->item(i)};
		if (heading->data(Qt::UserRole).isValid())
		{
			continue;
		}
		bool anyShown{false};
		for (int j = i + 1; j < m_targetList->count() && m_targetList->item(j)->data(Qt::UserRole).isValid(); j++)
		{
			anyShown = anyShown || !m_targetList->item(j)->isHidden();
		}
		heading->setHidden(!anyShown);
	}
}

void QtProjectWizardContentPathCMakeFileAPI::onTargetChecked()
{
	// The checked targets become the filter; all of them, the empty filter.
	std::vector<std::string> checked{};
	int checkable{0};
	for (int i = 0; i < m_targetList->count(); i++)
	{
		const QListWidgetItem* item{m_targetList->item(i)};
		if (!item->data(Qt::UserRole).isValid() || !item->flags().testFlag(Qt::ItemIsEnabled))
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
	updateTargetTexts();
	updateTargetSummary();
}

void QtProjectWizardContentPathCMakeFileAPI::onRunCMake()
{
	if (m_runningCMake || m_buildDir.empty())
	{
		return;
	}
	const auto arguments = cmakeArguments();
	if (!arguments)
	{
		fail(QString::fromStdString(arguments.error()));
		return;
	}
	m_runningCMake = true;
	m_runCMakeButton->setEnabled(false);
	m_buildDirLabel->setText(buildDirectoryNote().toHtmlEscaped());

	execution::runOnWorkPool([this,
							  buildDir = m_buildDir,
							  sourceDir = m_sourceDir,
							  presetName = selectedPreset(),
							  arguments = noPresetSelected() ? *arguments : std::vector<std::string>{}]() {
		CMakeFileAPIReader reader{buildDir};
		const auto configured{reader.ensureReply({}, sourceDir, presetName, true, arguments)};
		const std::string error{
			configured ? std::string{}
					   : std::format(
							 "Running CMake with preset \"{}\" failed: {}.\n\n{}",
							 presetName,
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
		std::string tooltip{preset.description};
		for (const std::string& line: presetDetails(preset))
		{
			tooltip += (tooltip.empty() ? "" : "\n") + line;
		}
		m_presetCombo->setItemData(m_presetCombo->count() - 1, QString::fromStdString(tooltip), Qt::ToolTipRole);
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
	m_cmakeArgumentsLabel->setVisible(noPresetSelected());
	m_cmakeArgumentsRow->setVisible(noPresetSelected());
	m_cmakeArgumentsHelp->setVisible(noPresetSelected());
	// Presets come from a presets file: without one there is nothing to refresh.
	m_refreshPresetsButton->setVisible(
		!m_sourceDir.empty() && !CMakeFileAPIReader::getPresetFiles(m_sourceDir).empty());
	showConfigureError();

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
	// Empty means the configuration built last (CMakeFileAPIReader::builtConfiguration),
	// else the first.
	const std::string built{m_hasReply ? CMakeFileAPIReader{m_buildDir}.builtConfiguration(m_configurations) : std::string{}};
	m_configurationCombo->lineEdit()->setPlaceholderText(
		m_configurations.empty() ? QStringLiteral("The first configuration of the build directory")
			: !built.empty()     ? QString::fromStdString(std::format("The configuration built last: {}", built))
								 : QString::fromStdString(std::format("The first configuration: {}", m_configurations.front())));
	m_configurationCombo->blockSignals(false);

	readTargets();
}

void QtProjectWizardContentPathCMakeFileAPI::readTargets()
{
	m_targets.clear();
	m_targetEntries.clear();
	if (m_hasReply)
	{
		const std::string configuration{m_configurationCombo->currentText().trimmed().toStdString()};
		if (const auto sources{CMakeFileAPIReader{m_buildDir}.getSourcesDetailed(configuration, {})})
		{
			m_targetEntries = sources->targets;
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
				if (target == m_targets.end() || !entry.compileGroup)
				{
					continue;
				}
				if (entry.isGenerated)
				{
					target->generatedCount++;
					continue;
				}
				target->sourceFileCount++;
				target->sources.push_back(entry.path);
				const CMakeFileAPIReader::CompileGroup& group{*entry.compileGroup};
				for (const FilePath& include: group.includes)
				{
					if (!std::ranges::contains(target->includes, include.str()))
					{
						target->includes.push_back(include.str());
					}
				}
				for (const std::string& define: group.defines)
				{
					if (!std::ranges::contains(target->defines, define))
					{
						target->defines.push_back(define);
					}
				}
				for (const std::string& flag: group.compileFlags)
				{
					if (flag.starts_with("-std=") || flag.starts_with("/std:"))
					{
						target->standard = flag.substr(5);
					}
				}
			}
		}
	}
	showTargets();
}

std::string QtProjectWizardContentPathCMakeFileAPI::targetKind(const Target& target)
{
	if (target.type.ends_with("_LIBRARY"))
	{
		return "library";
	}
	if (target.type == "EXECUTABLE")
	{
		// What CTest runs: named so by convention.
		const std::string name{utility::toLowerCase(target.name)};
		return name.contains("test") ? "test" : "executable";
	}
	return "utility";
}

QString QtProjectWizardContentPathCMakeFileAPI::targetDetails(const Target& target)
{
	// A few of each, and how many there are.
	const auto some = [](const std::vector<std::string>& values, std::size_t shown) {
		std::string text;
		for (std::size_t i = 0; i < values.size() && i < shown; i++)
		{
			text += "\n    " + values[i];
		}
		if (values.size() > shown)
		{
			text += std::format("\n    ... and {} more", values.size() - shown);
		}
		return text;
	};
	std::vector<std::string> files;
	for (const FilePath& source: target.sources)
	{
		files.push_back(source.fileName());
	}
	std::string text{std::format("{} ({})", target.name, targetTypeName(target.type))};
	text += std::format("\nStandard: {}", target.standard.empty() ? "the compiler's default" : target.standard);
	text += std::format("\nSources: {}{}", target.sourceFileCount, some(files, 8));
	if (target.generatedCount > 0)
	{
		text += std::format("\nGenerated sources: {} (not indexed until built)", target.generatedCount);
	}
	text += std::format("\nInclude paths: {}{}", target.includes.size(), some(target.includes, 6));
	text += std::format("\nDefines: {}{}", target.defines.size(), some(target.defines, 6));
	return QString::fromStdString(text);
}

QString QtProjectWizardContentPathCMakeFileAPI::targetText(const Target& target, bool checked) const
{
	std::string text{std::format("{}  ({}, {})", target.name, targetTypeName(target.type), fileCount(target.sourceFileCount))};
	if (checked && target.sourceFileCount > 0)
	{
		// What it adds: its files no other checked target has.
		const std::string filter{m_targetFilterEdit->text().trimmed().toStdString()};
		std::size_t own{0};
		for (const FilePath& source: target.sources)
		{
			const bool elsewhere{std::ranges::any_of(m_targets, [&](const Target& other) {
				return &other != &target && CMakeFileAPIReader::matchesTargetFilter(other.name, filter) &&
					std::ranges::contains(other.sources, source);
			})};
			own += elsewhere ? 0 : 1;
		}
		if (own != target.sourceFileCount)
		{
			text += std::format(" \u2013 adds {}", own);
		}
	}
	return QString::fromStdString(text);
}

std::string QtProjectWizardContentPathCMakeFileAPI::effectiveTargetFilter() const
{
	const std::string filter{m_targetFilterEdit->text().trimmed().toStdString()};
	return m_indexLinkedTargetsCheck->isChecked()
		? CMakeFileAPIReader::filterWithDependencies(m_targetEntries, filter)
		: filter;
}

std::size_t QtProjectWizardContentPathCMakeFileAPI::checkedFileCount() const
{
	const std::string filter{effectiveTargetFilter()};
	std::set<FilePath> files;
	for (const Target& target: m_targets)
	{
		if (CMakeFileAPIReader::matchesTargetFilter(target.name, filter))
		{
			files.insert(target.sources.begin(), target.sources.end());
		}
	}
	return files.size();
}

void QtProjectWizardContentPathCMakeFileAPI::showTargets()
{
	const std::string filter{m_targetFilterEdit->text().trimmed().toStdString()};
	const std::string effective{effectiveTargetFilter()};
	m_indexLinkedTargetsCheck->setVisible(m_hasReply && !m_targets.empty());
	m_indexLinkedTargetsCheck->setEnabled(!filter.empty());

	// The checked targets first, then by kind (libraries, executables, tests, utilities),
	// then by name: what is indexed is at the top. Not re-sorted on a check, so a row
	// stays under the mouse.
	static constexpr std::array kinds {"library", "executable", "test", "utility"};
	std::vector<const Target*> ordered;
	for (const Target& target: m_targets)
	{
		ordered.push_back(&target);
	}
	const bool allChecked{filter.empty()};
	std::ranges::stable_sort(ordered, [&](const Target* a, const Target* b) {
		const bool aChecked{!allChecked && CMakeFileAPIReader::matchesTargetFilter(a->name, filter)};
		const bool bChecked{!allChecked && CMakeFileAPIReader::matchesTargetFilter(b->name, filter)};
		const auto kindIndex = [](const Target* target) {
			return std::ranges::find(kinds, targetKind(*target)) - kinds.begin();
		};
		return std::tuple(!aChecked, kindIndex(a), a->name) < std::tuple(!bChecked, kindIndex(b), b->name);
	});

	m_targetList->blockSignals(true);
	m_targetList->clear();
	std::string group;
	for (const Target* target: ordered)
	{
		const bool checked{CMakeFileAPIReader::matchesTargetFilter(target->name, filter)};
		const std::string targetGroup{!allChecked && checked ? std::string("checked") : targetKind(*target)};
		if (targetGroup != group)
		{
			group = targetGroup;
			QListWidgetItem* heading{new QListWidgetItem(
				QString::fromStdString(group == "checked" ? "Checked" : group == "library" ? "Libraries"
										   : group == "executable"                      ? "Executables"
										   : group == "test"                            ? "Tests"
																						: "Utilities"),
				m_targetList)};
			heading->setFlags(Qt::NoItemFlags);
			QFont font{heading->font()};
			font.setBold(true);
			heading->setFont(font);
		}

		// Indexed because a checked target links to it: half checked, said so.
		const bool linked{!checked && CMakeFileAPIReader::matchesTargetFilter(target->name, effective)};
		QString text{targetText(*target, checked && !allChecked)};
		if (linked)
		{
			text += QStringLiteral(" \u2013 linked");
		}
		QListWidgetItem* item{new QListWidgetItem(text, m_targetList)};
		item->setData(Qt::UserRole, QString::fromStdString(target->name));
		item->setToolTip(targetDetails(*target));
		item->setData(Qt::AccessibleDescriptionRole, targetDetails(*target));	// what agents read
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		if (target->sourceFileCount == 0)
		{
			item->setFlags(item->flags() & ~Qt::ItemIsEnabled);	// nothing to index either way
		}
		item->setCheckState(checked ? Qt::Checked : linked ? Qt::PartiallyChecked : Qt::Unchecked);
	}
	m_targetList->blockSignals(false);
	m_targetList->setVisible(!m_targets.empty());
	m_targetListFilterEdit->parentWidget()->setVisible(m_targets.size() > 8);

	const std::size_t empty{static_cast<std::size_t>(
		std::ranges::count(m_targets, std::size_t{0}, &Target::sourceFileCount))};
	m_showEmptyTargetsCheck->setText(QString::fromStdString(std::format("Show {} without sources", empty)));
	m_showEmptyTargetsCheck->setVisible(empty > 0);
	onTargetListFilterEdited();
	updateTargetSummary();
}

void QtProjectWizardContentPathCMakeFileAPI::updateTargetTexts()
{
	const std::string filter{m_targetFilterEdit->text().trimmed().toStdString()};
	m_targetList->blockSignals(true);
	for (int i = 0; i < m_targetList->count(); i++)
	{
		QListWidgetItem* item{m_targetList->item(i)};
		if (!item->data(Qt::UserRole).isValid())
		{
			continue;
		}
		const auto target{std::ranges::find(m_targets, item->data(Qt::UserRole).toString().toStdString(), &Target::name)};
		if (target != m_targets.end())
		{
			item->setText(targetText(*target, !filter.empty() && item->checkState() == Qt::Checked));
		}
	}
	m_targetList->blockSignals(false);
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
	const std::string effective{effectiveTargetFilter()};
	std::size_t matched{0};
	std::size_t linked{0};
	std::vector<std::string> names;
	for (const Target& target: m_targets)
	{
		names.push_back(target.name);
		if (CMakeFileAPIReader::matchesTargetFilter(target.name, filter))
		{
			matched++;
		}
		else if (CMakeFileAPIReader::matchesTargetFilter(target.name, effective))
		{
			linked++;
		}
	}
	// Each file once: targets share sources (a library's, its tests').
	const std::size_t files{checkedFileCount()};
	// A pattern that matches nothing is likely a typo: said, per pattern.
	std::string unmatched;
	for (const std::string& pattern: CMakeFileAPIReader::unmatchedPatterns(names, filter))
	{
		unmatched += std::format(" {}: no target.", pattern);
	}
	return QString::fromStdString(std::format(
		"{} of {} target{}{}, {} to index.{}",
		matched,
		m_targets.size(),
		m_targets.size() == 1 ? "" : "s",
		linked > 0 ? std::format(" and {} they link to", linked) : std::string{},
		fileCount(files),
		unmatched));
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

void QtProjectWizardContentPathCMakeFileAPI::showConfigureError()
{
	m_configureError.clear();
	m_missesSubmodules = false;
	if (!m_configureFailed || m_runningCMake)
	{
		m_configureErrorRow->hide();
		return;
	}

	const std::string log{CMakeFileAPIReader{m_buildDir}.lastConfigureLog()};
	m_configureError = CMakeFileAPIReader::firstConfigureError(log);
	m_missesSubmodules = CMakeFileAPIReader::missesSubmodules(log, m_sourceDir);

	QString text{QStringLiteral("<span style=\"color:red\">CMake failed to configure the build directory")};
	text += m_configureError.empty() ? QStringLiteral(".") : QStringLiteral(": ") + toHtml(m_configureError);
	text += QStringLiteral("</span>");
	if (m_missesSubmodules)
	{
		text += QStringLiteral(
			"<br>A directory it adds is missing: the source directory is a git checkout without its "
			"submodules. Run <tt>git submodule update --init --recursive</tt> there, then Run CMake.");
	}
	else if (noPresetSelected())
	{
		text += QStringLiteral("<br>Options the project needs (<tt>-D...=OFF</tt>) go in CMake Arguments.");
	}
	m_configureErrorLabel->setText(text);
	m_configureErrorRow->show();
}

void QtProjectWizardContentPathCMakeFileAPI::onShowCMakeLog()
{
	const std::string log{CMakeFileAPIReader{m_buildDir}.lastConfigureLog()};

	QDialog dialog(this);
	dialog.setWindowTitle(QStringLiteral("CMake Log"));
	dialog.setObjectName(QStringLiteral("cmakeLogDialog"));
	QVBoxLayout* dialogLayout = new QVBoxLayout(&dialog);
	QPlainTextEdit* text = new QPlainTextEdit(&dialog);
	text->setAccessibleName(QStringLiteral("CMake Log"));
	text->setReadOnly(true);
	text->setLineWrapMode(QPlainTextEdit::NoWrap);
	text->setPlainText(
		log.empty() ? QStringLiteral("Sourcetrail has not run CMake in this build directory.")
					: QString::fromStdString(log));
	dialogLayout->addWidget(text);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialogLayout->addWidget(buttons);
	dialog.resize(800, 500);
	dialog.exec();
}

std::expected<std::vector<std::string>, std::string> QtProjectWizardContentPathCMakeFileAPI::cmakeArguments() const
{
	const std::string text{m_cmakeArgumentsEdit->text().trimmed().toStdString()};
	if (text.empty())
	{
		return std::vector<std::string>{};
	}
	return editor_command::split(text).transform_error([&](editor_command::EditorCommandError) {
		return std::format("The CMake Arguments \"{}\" have a quote that is not closed.", text);
	});
}

bool QtProjectWizardContentPathCMakeFileAPI::fail(const QString& message)
{
	// Next to the fields, and as the dialog every other page shows (which agent control reports).
	m_presetStatusLabel->setText(
		QStringLiteral("<span style=\"color:red\">") + message.toHtmlEscaped() + QStringLiteral("</span>"));
	m_window->tell(message);
	return false;
}
