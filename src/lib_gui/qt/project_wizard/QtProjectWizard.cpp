#include "QtProjectWizard.h"

#include "Application.h"
#ifndef SRCTRL_MODULE_BUILD
#include "MessageLoadProject.h"
#include "MessageStatus.h"
#endif
#include "Platform.h"
#include "QtMessageBox.h"
#include "QtProjectWizardContent.h"
#include "QtProjectWizardContentCustomCommand.h"
#include "QtProjectWizardContentCargoOptions.h"
#include "QtProjectWizardContentSwiftOptions.h"
#include "QtProjectWizardContentExtensions.h"
#include "QtProjectWizardContentGroup.h"
#include "QtProjectWizardContentPathsExclude.h"
#include "QtProjectWizardContentPathsSource.h"
#include "QtProjectWizardContentProjectData.h"
#include "QtProjectWizardContentRequiredLabel.h"
#include "QtProjectWizardContentSelect.h"
#include "QtProjectWizardContentSourceGroupData.h"
#include "QtProjectWizardContentSourceGroupInfoText.h"
#include "QtProjectWizardContentUnloadable.h"
#include "QtResources.h"
#include "SourceGroupFields.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsCustomCommand.h"
#include "SourceGroupSettingsUnloadable.h"
#endif
#include "ToolChain.h"
#include "language_package_flags.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#endif
#include "utilityPathDetection.h"
#include "utilityUuid.h"

#include <QFileDialog>
#include <QListWidget>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSysInfo>
#include <QTimer>

#include "QtProjectWizardContentCStandard.h"
#include "QtProjectWizardContentCppStandard.h"
#include "QtProjectWizardContentCrossCompilationOptions.h"
#include "QtProjectWizardContentCxxPchFlags.h"
#include "QtProjectWizardContentFlags.h"
#include "QtProjectWizardContentPathCMakeFileAPI.h"
#include "QtProjectWizardContentPathsFrameworkSearch.h"
#include "QtProjectWizardContentPathsFrameworkSearchGlobal.h"
#include "QtProjectWizardContentPathsHeaderSearchGlobal.h"
#if BUILD_CXX_LANGUAGE_PACKAGE
	// These wizard contents depend on lib_cxx and are only compiled when the
	// C/C++ language package is enabled:
	#include "QtProjectWizardContentPathCDB.h"
	#include "QtProjectWizardContentPathCxxPch.h"
	#include "QtProjectWizardContentPathsHeaderSearch.h"
	#include "QtProjectWizardContentPathsIndexedHeaders.h"
#endif
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsCEmpty.h"
#include "SourceGroupSettingsCppEmpty.h"
#include "SourceGroupSettingsCxxCdb.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#endif

#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsRustEmpty.h"
#include "SourceGroupSettingsSwiftEmpty.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.settings;
import srctrl.utility;
#endif

using namespace std;

namespace
{
bool applicationSettingsContainVisualStudioHeaderSearchPaths()
{
	std::vector<FilePath> expandedPaths;
	const std::shared_ptr<CombinedPathDetector> headerPathDetector =
		utility::getCxxVsHeaderPathDetector();
	for (const std::string& detectorName: headerPathDetector->getWorkingDetectorNames())
	{
		for (const FilePath& path: headerPathDetector->getPathsForDetector(detectorName))
		{
			utility::append(expandedPaths, path.expandEnvironmentVariables());
		}
	}

	std::vector<FilePath> usedExpandedGlobalHeaderSearchPaths =
		ApplicationSettings::getInstance()->getHeaderSearchPathsExpanded();
	for (const FilePath& usedExpandedPath: usedExpandedGlobalHeaderSearchPaths)
	{
		for (const FilePath& expandedPath: expandedPaths)
		{
			if (expandedPath == usedExpandedPath)
			{
				return true;
			}
		}
	}

	return false;
}

void addMsvcCompatibilityFlagsOnDemand(const std::shared_ptr<SourceGroupSettingsWithCxxPathsAndFlags>& settings)
{
	if (applicationSettingsContainVisualStudioHeaderSearchPaths())
	{
		std::vector<std::string> flags = settings->getCompilerFlags();
		flags.push_back(ClangCompiler::msExtensionsOption());
		flags.push_back(ClangCompiler::msCompatibilityOption());
		flags.push_back(ClangCompiler::msCompatibilityVersionOption(VisualStudio::getLatestMsvcVersion()));
		settings->setCompilerFlags(flags);
	}
}

template <typename SettingsType>
void addSourceGroupContents(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SettingsType> settings,
	QtProjectWizardWindow* window);

#if BUILD_CXX_LANGUAGE_PACKAGE

template <>
void addSourceGroupContents<SourceGroupSettingsCEmpty>(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SourceGroupSettingsCEmpty> settings,
	QtProjectWizardWindow* window)
{
	group->addContent(new QtProjectWizardContentCStandard(settings, window));
	group->addContent(new QtProjectWizardContentCrossCompilationOptions(settings, window));
	group->addSpace();

	group->addContent(new QtProjectWizardContentPathsSource(settings, window));
	group->addContent(new QtProjectWizardContentPathsExclude(settings, window));
	group->addContent(new QtProjectWizardContentExtensions(settings, window));
	group->addSpace();

	group->addContent(new QtProjectWizardContentPathsHeaderSearch(settings, window));
	group->addContent(new QtProjectWizardContentPathsHeaderSearchGlobal(window));
	group->addSpace();

	if constexpr (utility::Platform::isMac())
	{
		group->addContent(new QtProjectWizardContentPathsFrameworkSearch(settings, window));
		group->addContent(new QtProjectWizardContentPathsFrameworkSearchGlobal(window));
		group->addSpace();
	}

	group->addContent(new QtProjectWizardContentFlags(settings, window));
	group->addContent(new QtProjectWizardContentPathCxxPch(settings, settings, window));
	group->addContent(new QtProjectWizardContentCxxPchFlags(settings, window, false));
}

template <>
void addSourceGroupContents<SourceGroupSettingsCppEmpty>(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SourceGroupSettingsCppEmpty> settings,
	QtProjectWizardWindow* window)
{
	group->addContent(new QtProjectWizardContentCppStandard(settings, window));
	group->addContent(new QtProjectWizardContentCrossCompilationOptions(settings, window));
	group->addSpace();

	group->addContent(new QtProjectWizardContentPathsSource(settings, window));
	group->addContent(new QtProjectWizardContentPathsExclude(settings, window));
	group->addContent(new QtProjectWizardContentExtensions(settings, window));
	group->addSpace();

	group->addContent(new QtProjectWizardContentPathsHeaderSearch(settings, window));
	group->addContent(new QtProjectWizardContentPathsHeaderSearchGlobal(window));
	group->addSpace();

	if constexpr (utility::Platform::isMac())
	{
		group->addContent(new QtProjectWizardContentPathsFrameworkSearch(settings, window));
		group->addContent(new QtProjectWizardContentPathsFrameworkSearchGlobal(window));
		group->addSpace();
	}

	group->addContent(new QtProjectWizardContentFlags(settings, window));
	group->addContent(new QtProjectWizardContentPathCxxPch(settings, settings, window));
	group->addContent(new QtProjectWizardContentCxxPchFlags(settings, window, false));
}

template <>
void addSourceGroupContents<SourceGroupSettingsCxxCMakeFileAPI>(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings,
	QtProjectWizardWindow* window)
{
	group->addContent(new QtProjectWizardContentPathCMakeFileAPI(settings, window));
	group->addContent(new QtProjectWizardContentPathsIndexedHeaders(settings, window, "CMake File API"));
	group->addContent(new QtProjectWizardContentPathsExclude(settings, window));
	group->addSpace();

	group->addContent(new QtProjectWizardContentPathsHeaderSearch(settings, window, true));
	group->addContent(new QtProjectWizardContentPathsHeaderSearchGlobal(window));
	group->addSpace();

	if constexpr (utility::Platform::isMac())
	{
		group->addContent(new QtProjectWizardContentPathsFrameworkSearch(settings, window, true));
		group->addContent(new QtProjectWizardContentPathsFrameworkSearchGlobal(window));
		group->addSpace();
	}

	group->addContent(new QtProjectWizardContentFlags(settings, window, true));
}

template <>
void addSourceGroupContents<SourceGroupSettingsCxxCdb>(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SourceGroupSettingsCxxCdb> settings,
	QtProjectWizardWindow* window)
{
	group->addContent(new QtProjectWizardContentPathCDB(settings, window));
	group->addContent(
		new QtProjectWizardContentPathsIndexedHeaders(settings, window, "Compilation Database"));
	group->addContent(new QtProjectWizardContentPathsExclude(settings, window));
	group->addSpace();

	group->addContent(new QtProjectWizardContentPathsHeaderSearch(settings, window, true));
	group->addContent(new QtProjectWizardContentPathsHeaderSearchGlobal(window));
	group->addSpace();

	if constexpr (utility::Platform::isMac())
	{
		group->addContent(new QtProjectWizardContentPathsFrameworkSearch(settings, window, true));
		group->addContent(new QtProjectWizardContentPathsFrameworkSearchGlobal(window));
		group->addSpace();
	}

	group->addContent(new QtProjectWizardContentFlags(settings, window, true));
	group->addContent(new QtProjectWizardContentPathCxxPch(settings, settings, window));
	group->addContent(new QtProjectWizardContentCxxPchFlags(settings, window, true));
}

#endif	  // BUILD_CXX_LANGUAGE_PACKAGE

template <>
void addSourceGroupContents<SourceGroupSettingsRustEmpty>(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SourceGroupSettingsRustEmpty> settings,
	QtProjectWizardWindow* window)
{
	group->addContent(new QtProjectWizardContentPathsSource(settings, window));
	group->addContent(new QtProjectWizardContentPathsExclude(settings, window));
	group->addContent(new QtProjectWizardContentExtensions(settings, window));
	group->addContent(new QtProjectWizardContentCargoOptions(settings, window));
}

template <>
void addSourceGroupContents<SourceGroupSettingsSwiftEmpty>(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SourceGroupSettingsSwiftEmpty> settings,
	QtProjectWizardWindow* window)
{
	group->addContent(new QtProjectWizardContentPathsSource(settings, window));
	group->addContent(new QtProjectWizardContentPathsExclude(settings, window));
	group->addContent(new QtProjectWizardContentExtensions(settings, window));
	group->addContent(new QtProjectWizardContentSwiftOptions(settings, window));
}

template <>
void addSourceGroupContents<SourceGroupSettingsCustomCommand>(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SourceGroupSettingsCustomCommand> settings,
	QtProjectWizardWindow* window)
{
	group->addContent(new QtProjectWizardContentCustomCommand(settings, window));
	group->addContent(new QtProjectWizardContentPathsSource(settings, window));
	group->addContent(new QtProjectWizardContentPathsExclude(settings, window));
	group->addContent(new QtProjectWizardContentExtensions(settings, window));
}

template <>
void addSourceGroupContents<SourceGroupSettingsUnloadable>(
	QtProjectWizardContentGroup* group,
	std::shared_ptr<SourceGroupSettingsUnloadable> settings,
	QtProjectWizardWindow* window)
{
	group->addContent(new QtProjectWizardContentUnloadable(settings, window));
}
}	 // namespace

QtProjectWizard::QtProjectWizard(QWidget* parent)
	: QtProjectWizardWindow(parent, false)
	, m_windowStack(this)
{
	setScrollAble(true);

	connect(&m_windowStack, &QtWindowStack::push, this, &QtProjectWizard::windowStackChanged);
	connect(&m_windowStack, &QtWindowStack::pop, this, &QtProjectWizard::windowStackChanged);
	connect(&m_windowStack, &QtWindowStack::empty, this, &QtProjectWizard::windowStackChanged);

	// save old application settings so they can be compared later
	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
	m_appSettings.setHeaderSearchPaths(appSettings->getHeaderSearchPaths());
	m_appSettings.setFrameworkSearchPaths(appSettings->getFrameworkSearchPaths());

	// Closed by a person (its close button, Escape) or an agent.
	connect(this, &QtWindow::canceled, this, []() { ProjectWizardStateMachine::instance()->canceled(); });
}

QtProjectWizard::~QtProjectWizard()
{
	// Gone without closing (the start screen replaced it): the machine is closed too.
	ProjectWizardStateMachine::instance()->canceled();
}

QSize QtProjectWizard::sizeHint() const
{
	return QSize(930, 600);
}

void QtProjectWizard::newProject()
{
	m_projectSettings = std::make_shared<ProjectSettings>();
	setup();
}

void QtProjectWizard::newProjectFromCDB(const FilePath& filePath)
{
	if constexpr (!language_packages::buildCxxLanguagePackage)
		return;

	if (!m_projectSettings)
		m_projectSettings = std::make_shared<ProjectSettings>();

	if (m_projectSettings->getProjectFilePath().empty())
	{
		m_projectSettings->setProjectFilePath(
			filePath.withoutExtension().fileName(), filePath.getParentDirectory());
	}

	if (!m_contentWidget)
		setup();
	else
		loadContent();

	cancelSourceGroup();

	std::shared_ptr<SourceGroupSettingsCxxCdb> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCxxCdb>(
			utility::getUuidString(), m_projectSettings.get());
	sourceGroupSettings->setCompilationDatabasePath(filePath);

	createSourceGroup(sourceGroupSettings);
}

void QtProjectWizard::editProject(const FilePath& settingsPath)
{
	std::shared_ptr<ProjectSettings> settings = std::make_shared<ProjectSettings>(settingsPath);
	settings->reload();
	editProject(settings);
}

void QtProjectWizard::editProject(std::shared_ptr<ProjectSettings> settings)
{
	m_projectSettings = settings;
	m_allSourceGroupSettings = settings->getAllSourceGroupSettings();
	m_editing = true;

	setup();
}

void QtProjectWizard::populateWindow(QWidget* widget)
{
	QHBoxLayout* layout = new QHBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	{
		QWidget* menu = new QWidget();
		menu->setObjectName(QStringLiteral("project_menu"));
		menu->setFixedWidth(230);
		layout->addWidget(menu);

		QVBoxLayout* menuLayout = new QVBoxLayout(menu);
		menuLayout->setContentsMargins(0, 0, 0, 0);
		menuLayout->setSpacing(0);

		m_generalButton = new QPushButton(QStringLiteral("General"));
		m_generalButton->setObjectName(QStringLiteral("general_button"));
		m_generalButton->setCheckable(true);
		connect(m_generalButton, &QPushButton::clicked, this, &QtProjectWizard::generalButtonClicked);
		menuLayout->addWidget(m_generalButton);

		QLabel* sourceGroupLabel = new QLabel(QStringLiteral("Source Groups:"));
		menuLayout->addWidget(sourceGroupLabel);

		m_sourceGroupList = new QListWidget();
		m_sourceGroupList->setAttribute(Qt::WA_MacShowFocusRect, false);
		m_sourceGroupList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		m_sourceGroupList->setTextElideMode(Qt::ElideRight);
		connect(
			m_sourceGroupList,
			&QListWidget::currentRowChanged,
			this,
			&QtProjectWizard::selectedSourceGroupChanged);
		menuLayout->addWidget(m_sourceGroupList);

		QHBoxLayout* buttonsLayout = new QHBoxLayout();
		buttonsLayout->setContentsMargins(0, 0, 0, 0);
		buttonsLayout->setSpacing(0);

		QPushButton* addButton = new QtIconButton(QtResources::WINDOW_SOURCE_GROUP_ADD, QtResources::WINDOW_SOURCE_GROUP_ADD_HOVER);

		m_removeButton = new QtIconButton(QtResources::WINDOW_SOURCE_GROUP_DELETE, QtResources::WINDOW_SOURCE_GROUP_DELETE_HOVER);

		m_duplicateButton = new QtIconButton(QtResources::WINDOW_SOURCE_GROUP_COPY, QtResources::WINDOW_SOURCE_GROUP_COPY_HOVER);

		addButton->setIconSize(QSize(20, 20));
		m_removeButton->setIconSize(QSize(20, 20));
		m_duplicateButton->setIconSize(QSize(20, 20));

		addButton->setToolTip(QStringLiteral("Add Source Group"));
		m_removeButton->setToolTip(QStringLiteral("Remove Source Group"));
		m_duplicateButton->setToolTip(QStringLiteral("Copy Source Group"));

		addButton->setObjectName(QStringLiteral("action_button"));
		m_removeButton->setObjectName(QStringLiteral("action_button"));
		m_duplicateButton->setObjectName(QStringLiteral("action_button"));

		m_removeButton->setEnabled(false);
		m_duplicateButton->setEnabled(false);

		connect(addButton, &QPushButton::clicked, this, &QtProjectWizard::newSourceGroup);
		connect(
			m_removeButton, &QPushButton::clicked, this, &QtProjectWizard::removeSelectedSourceGroup);
		connect(
			m_duplicateButton,
			&QPushButton::clicked,
			this,
			&QtProjectWizard::duplicateSelectedSourceGroup);

		buttonsLayout->addWidget(addButton);
		buttonsLayout->addWidget(m_removeButton);
		buttonsLayout->addWidget(m_duplicateButton);
		buttonsLayout->addStretch();

		menuLayout->addLayout(buttonsLayout);
	}

	QFrame* separator = new QFrame();
	separator->setFrameShape(QFrame::VLine);

	QPalette palette = separator->palette();
	palette.setColor(QPalette::WindowText, Qt::lightGray);
	separator->setPalette(palette);

	layout->addWidget(separator);
	layout->addSpacing(10);

	m_contentWidget = new QWidget();
	m_contentWidget->setObjectName(QStringLiteral("form"));

	m_formArea = new QScrollArea();
	m_formArea->setFrameShadow(QFrame::Plain);
	m_formArea->setObjectName(QStringLiteral("formArea"));
	m_formArea->setWidgetResizable(true);

	m_formArea->setWidget(m_contentWidget);
	layout->addWidget(m_formArea);

	widget->setLayout(layout);
}

void QtProjectWizard::windowReady()
{
	if (m_editing)
	{
		updateTitle(QStringLiteral("Edit Project"));
		updateNextButton(QStringLiteral("Save"));
	}
	else
	{
		updateTitle(QStringLiteral("New Project"));
		updateNextButton(QStringLiteral("Create"));
		setNextEnabled(false);
	}

	updateSubTitle(QStringLiteral("Overview"));
	updatePreviousButton(QStringLiteral("Add Source Group"));

	connect(this, &QtProjectWizard::previous, this, &QtProjectWizard::newSourceGroup);
	connect(this, &QtProjectWizard::next, this, &QtProjectWizard::createProject);

	updateSourceGroupList();

	if (!m_allSourceGroupSettings.empty())
	{
		m_sourceGroupList->setCurrentRow(0);
	}
	else
	{
		generalButtonClicked();
	}

	ProjectWizardStateMachine::instance()->opened(view());
}

void QtProjectWizard::handlePrevious()
{
	QtProjectWizardWindow::handlePrevious();
}

QtProjectWizardWindow* QtProjectWizard::createWindowWithContent(
	std::function<QtProjectWizardContent*(QtProjectWizardWindow*)> func)
{
	QtProjectWizardWindow* window = new QtProjectWizardWindow(parentWidget());

	connect(window, &QtProjectWizardWindow::previous, &m_windowStack, &QtWindowStack::popWindow);
	connect(window, &QtProjectWizardWindow::canceled, this, &QtProjectWizard::cancelSourceGroup);

	window->setPreferredSize(QSize(580, 340));
	window->setContent(func(window));
	window->setScrollAble(true);
	window->setup();

	m_windowStack.pushWindow(window);

	return window;
}

void QtProjectWizard::updateSourceGroupList()
{
	m_sourceGroupList->blockSignals(true);
	m_sourceGroupList->clear();

	for (const std::shared_ptr<SourceGroupSettings>& group: m_allSourceGroupSettings)
	{
		QString name = QString::fromStdString(group->getName());
		if (group->getStatus() == SourceGroupStatusType::DISABLED)
		{
			name = "(" + name + ")";
		}

		QListWidgetItem* item = new QListWidgetItem(name);
		item->setToolTip(name);
		m_sourceGroupList->addItem(item);
	}
	m_sourceGroupList->blockSignals(false);
}

bool QtProjectWizard::canExitContent()
{
	if (content())
	{
		if (!content()->check())
		{
			return false;
		}

		saveContent();
	}

	return true;
}

void QtProjectWizard::generalButtonClicked()
{
	if (!canExitContent())
	{
		m_generalButton->setChecked(false);
		return;
	}

	QtProjectWizardContentGroup* contentGroup = new QtProjectWizardContentGroup(this);
	contentGroup->addContent(
		new QtProjectWizardContentProjectData(m_projectSettings, this, m_editing));

	contentGroup->addSpace();
	contentGroup->addContent(new QtProjectWizardContentRequiredLabel(this));

	if (m_allSourceGroupSettings.empty())
	{
		contentGroup->addSpace();
		contentGroup->addContent(new QtProjectWizardContentSourceGroupInfoText(this));
	}

	setContent(contentGroup);

	qDeleteAll(m_contentWidget->children());
	QtProjectWizardWindow::populateWindow(m_contentWidget);
	m_formArea->verticalScrollBar()->setValue(0);	 // a new page starts at its top

	loadContent();

	m_previouslySelectedIndex = -1;

	m_generalButton->setChecked(true);
	m_sourceGroupList->setCurrentRow(-1);

	reportChanged();
}

void QtProjectWizard::selectedSourceGroupChanged(int index)
{
	if (index < 0 || index >= int(m_allSourceGroupSettings.size()))
	{
		m_removeButton->setEnabled(false);
		m_duplicateButton->setEnabled(false);
		return;
	}

	if (index == m_previouslySelectedIndex)
	{
		return;
	}

	if (!canExitContent())
	{
		QTimer::singleShot(1, this, [this]() { m_sourceGroupList->setCurrentRow(m_previouslySelectedIndex); });
		return;
	}

	m_generalButton->setChecked(false);
	m_removeButton->setEnabled(true);
	m_duplicateButton->setEnabled(true);

	std::shared_ptr<SourceGroupSettings> group = m_allSourceGroupSettings[index];

	QtProjectWizardContentGroup* summary = new QtProjectWizardContentGroup(this);

	QtProjectWizardContentSourceGroupData* content = new QtProjectWizardContentSourceGroupData(
		group, this);
	connect(
		content,
		&QtProjectWizardContentSourceGroupData::nameUpdated,
		this,
		&QtProjectWizard::selectedSourceGroupNameChanged);
	summary->addContent(content);
	summary->addSpace();

	if (std::shared_ptr<SourceGroupSettingsCustomCommand> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsCustomCommand>(group))
	{
		addSourceGroupContents(summary, settings, this);
	}
	else if (
		std::shared_ptr<SourceGroupSettingsUnloadable> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsUnloadable>(group))
	{
		addSourceGroupContents(summary, settings, this);
	}

#if BUILD_CXX_LANGUAGE_PACKAGE
	if (
		std::shared_ptr<SourceGroupSettingsCEmpty> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsCEmpty>(group))
	{
		addSourceGroupContents(summary, settings, this);
	}
	else if (
		std::shared_ptr<SourceGroupSettingsCppEmpty> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsCppEmpty>(group))
	{
		addSourceGroupContents(summary, settings, this);
	}
	else if (
		std::shared_ptr<SourceGroupSettingsCxxCdb> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsCxxCdb>(group))
	{
		addSourceGroupContents(summary, settings, this);
	}
	else if (
		std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsCxxCMakeFileAPI>(group))
	{
		addSourceGroupContents(summary, settings, this);
	}
#endif	  // BUILD_CXX_LANGUAGE_PACKAGE


	if (
		std::shared_ptr<SourceGroupSettingsRustEmpty> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsRustEmpty>(group))
	{
		addSourceGroupContents(summary, settings, this);
	}
	else if (
		std::shared_ptr<SourceGroupSettingsSwiftEmpty> settings =
			std::dynamic_pointer_cast<SourceGroupSettingsSwiftEmpty>(group))
	{
		addSourceGroupContents(summary, settings, this);
	}

	summary->addSpace();
	summary->addContent(new QtProjectWizardContentRequiredLabel(this));

	setContent(summary);

	qDeleteAll(m_contentWidget->children());
	QtProjectWizardWindow::populateWindow(m_contentWidget);
	m_formArea->verticalScrollBar()->setValue(0);	 // a new page starts at its top

	loadContent();

	m_previouslySelectedIndex = index;

	reportChanged();
}

void QtProjectWizard::selectedSourceGroupNameChanged(const QString& name)
{
	QListWidgetItem* item = m_sourceGroupList->item(m_sourceGroupList->currentRow());
	item->setText(name);
	item->setToolTip(name);
}

void QtProjectWizard::removeSelectedSourceGroup()
{
	if (!m_allSourceGroupSettings.size() || m_sourceGroupList->currentRow() < 0)
	{
		return;
	}

	if (ask(QStringLiteral("Do you really want to remove this source group from the project?"), {"Yes", "No"}) == 0)
	{
		removeSourceGroup(m_sourceGroupList->currentRow());
	}
}

void QtProjectWizard::removeSourceGroup(int row)
{
	m_allSourceGroupSettings.erase(m_allSourceGroupSettings.begin() + row);

	updateSourceGroupList();
	setContent(nullptr);

	if (m_allSourceGroupSettings.empty())
	{
		setNextEnabled(false);
		generalButtonClicked();
		return;
	}

	if (row >= int(m_allSourceGroupSettings.size()))
	{
		row--;
	}

	m_previouslySelectedIndex = -1;

	m_sourceGroupList->setCurrentRow(row);
}

void QtProjectWizard::duplicateSelectedSourceGroup()
{
	if (!m_allSourceGroupSettings.size() || m_sourceGroupList->currentRow() < 0 || !canExitContent())
	{
		return;
	}

	std::shared_ptr<const SourceGroupSettings> oldSourceGroup =
		m_allSourceGroupSettings[m_sourceGroupList->currentRow()];

	std::shared_ptr<SourceGroupSettings> newSourceGroup = oldSourceGroup->createCopy();

	newSourceGroup->setId(utility::getUuidString());

	{
		const std::string newNameBase = oldSourceGroup->getName() + " - Copy";

		int id = 0;
		std::string newName;

		while (true)
		{
			newName = newNameBase + (id > 0 ? " (" + std::to_string(id) + ")" : "");

			bool nameAlreadyExists = false;
			for (const std::shared_ptr<SourceGroupSettings>& sourceGroupSettings:
				 m_allSourceGroupSettings)
			{
				if (sourceGroupSettings && sourceGroupSettings->getName() == newName)
				{
					nameAlreadyExists = true;
					break;
				}
			}

			if (!nameAlreadyExists)
			{
				break;
			}

			id++;
		}

		newSourceGroup->setName(newName);
	}

	const int newRow = m_sourceGroupList->currentRow() + 1;
	m_allSourceGroupSettings.insert(m_allSourceGroupSettings.begin() + newRow, newSourceGroup);

	updateSourceGroupList();
	setContent(nullptr);

	m_previouslySelectedIndex = -1;

	m_sourceGroupList->setCurrentRow(newRow);
}

void QtProjectWizard::cancelSourceGroup()
{
	m_windowStack.clearWindows();
}

void QtProjectWizard::cancelWizard()
{
	m_windowStack.clearWindows();
	emit canceled();
}

void QtProjectWizard::finishWizard()
{
	m_windowStack.clearWindows();
	emit finished();
}

void QtProjectWizard::windowStackChanged()
{
	QWidget* window = m_windowStack.getTopWindow();

	if (window)
	{
		dynamic_cast<QtProjectWizardWindow*>(window)->content()->load();
		setEnabled(false);
	}
	else
	{
		setEnabled(true);
		raise();

		// The type selection closed without a type chosen.
		if (std::exchange(m_choosingType, false) && !m_typeSelected && !m_applying)
		{
			ProjectWizardStateMachine::instance()->back(view());
		}
	}
}

void QtProjectWizard::newSourceGroup()
{
	if (!canExitContent())
	{
		return;
	}

	QtProjectWizardWindow* window = createWindowWithContent([](QtProjectWizardWindow* window) {
		window->setPreferredSize(QSize(560, 520));
		return new QtProjectWizardContentSelect(window);
	});
	window->resize(QSize(560, 520));

	connect(
		dynamic_cast<QtProjectWizardContentSelect*>(window->content()),
		&QtProjectWizardContentSelect::selected,
		this,
		&QtProjectWizard::selectedProjectType);

	window->show();
	window->setNextEnabled(false);
	window->setPreviousEnabled(false);
	window->updateSubTitle(QStringLiteral("Type Selection"));

	m_choosingType = true;
	m_typeSelected = false;
	ProjectWizardStateMachine::instance()->choosing(view());
}


void QtProjectWizard::selectedProjectType(SourceGroupType sourceGroupType)
{
	m_typeSelected = true;
	const std::string sourceGroupId = utility::getUuidString();
	std::shared_ptr<SourceGroupSettings> settings;

	switch (sourceGroupType)
	{
	case SourceGroupType::C_EMPTY:
		if constexpr (language_packages::buildCxxLanguagePackage)
		{
			std::shared_ptr<SourceGroupSettingsCEmpty> cxxSettings =
				std::make_shared<SourceGroupSettingsCEmpty>(sourceGroupId, m_projectSettings.get());
			addMsvcCompatibilityFlagsOnDemand(cxxSettings);
			settings = cxxSettings;
		}
		break;
	case SourceGroupType::CXX_EMPTY:
		if constexpr (language_packages::buildCxxLanguagePackage)
		{
			std::shared_ptr<SourceGroupSettingsCppEmpty> cxxSettings =
				std::make_shared<SourceGroupSettingsCppEmpty>(sourceGroupId, m_projectSettings.get());
			addMsvcCompatibilityFlagsOnDemand(cxxSettings);
			settings = cxxSettings;
		}
		break;
	case SourceGroupType::CXX_CDB:
		if constexpr (language_packages::buildCxxLanguagePackage)
			settings = std::make_shared<SourceGroupSettingsCxxCdb>(
				sourceGroupId, m_projectSettings.get());
		break;
	case SourceGroupType::CXX_CMAKE_FILE_API:
		if constexpr (language_packages::buildCxxLanguagePackage)
			settings = std::make_shared<SourceGroupSettingsCxxCMakeFileAPI>(
				sourceGroupId, m_projectSettings.get());
		break;

	case SourceGroupType::RUST_EMPTY:
		if constexpr (language_packages::buildRustLanguagePackage)
			settings = std::make_shared<SourceGroupSettingsRustEmpty>(
				sourceGroupId, m_projectSettings.get());
		break;
	case SourceGroupType::SWIFT_EMPTY:
		if constexpr (language_packages::buildSwiftLanguagePackage)
			settings = std::make_shared<SourceGroupSettingsSwiftEmpty>(
				sourceGroupId, m_projectSettings.get());
		break;

	case SourceGroupType::CUSTOM_COMMAND:
		settings = std::make_shared<SourceGroupSettingsCustomCommand>(
			sourceGroupId, m_projectSettings.get());
		break;
	case SourceGroupType::UNKNOWN:
		break;
	}

	if (settings)
	{
		createSourceGroup(settings);
	}
	if (!m_applying)
	{
		ProjectWizardStateMachine::instance()->selected(view());
	}
}

void QtProjectWizard::createSourceGroup(std::shared_ptr<SourceGroupSettings> settings)
{
	m_allSourceGroupSettings.push_back(settings);

	updateSourceGroupList();
	cancelSourceGroup();

	setNextEnabled(true);

	// The page shown was checked and saved before the type was chosen; selecting the new group
	// must not check it (and ask its questions) a second time.
	setContent(nullptr);
	m_previouslySelectedIndex = -1;

	m_sourceGroupList->setCurrentRow(static_cast<int>(m_allSourceGroupSettings.size()) - 1);
}

bool QtProjectWizard::createProject()
{
	FilePath path = m_projectSettings->getFilePath();

	m_projectSettings->setVersion(ProjectSettings::VERSION);
	m_projectSettings->setAllSourceGroupSettings(m_allSourceGroupSettings);
	if (!m_projectSettings->save(path))
	{
		MessageStatus("Unable to save project to location: " + path.str()).dispatch();

		tell(QString::fromStdString(
			"Could not create the project: Sourcetrail was unable to save it to " + path.str() +
			". Please pick a different project location."));
		return false;
	}

	bool settingsChanged = false;
	if (m_editing)
	{
		std::shared_ptr<const Project> currentProject =
			Application::getInstance()->getCurrentProject();
		if (currentProject)
		{
			settingsChanged = !(
				currentProject->settingsEqualExceptNameAndLocation(*m_projectSettings));
		}

		settingsChanged |= !(m_appSettings == *ApplicationSettings::getInstance());
	}
	else
	{
		MessageStatus("Created project: " + path.str()).dispatch();
	}

	MessageLoadProject(path, settingsChanged).dispatch();

	ProjectWizardStateMachine::instance()->created(path.str());
	finishWizard();
	return true;
}

ProjectWizardView QtProjectWizard::view() const
{
	ProjectWizardView view;
	view.editing = m_editing;
	view.projectName = m_projectSettings->getProjectName();
	view.projectLocation = m_projectSettings->getProjectDirectoryPath().str();
	for (const std::shared_ptr<SourceGroupSettings>& group: m_allSourceGroupSettings)
	{
		view.sourceGroups.push_back(
			{group->getName(),
			 std::string(sourceGroupTypeId(group->getType())),
			 group->getStatus() == SourceGroupStatusType::ENABLED});
	}
	if (m_previouslySelectedIndex >= 0 && m_previouslySelectedIndex < int(m_allSourceGroupSettings.size()))
	{
		view.sourceGroup = static_cast<std::size_t>(m_previouslySelectedIndex);
		view.fields = readSourceGroupFields(*m_allSourceGroupSettings[m_previouslySelectedIndex]);
	}
	if constexpr (language_packages::buildCxxLanguagePackage)
	{
		view.types.insert(view.types.end(), {"cdb", "cmake", "cpp", "c"});
	}
	if constexpr (language_packages::buildRustLanguagePackage)
	{
		view.types.emplace_back("rust");
	}
	if constexpr (language_packages::buildSwiftLanguagePackage)
	{
		view.types.emplace_back("swift");
	}
	view.types.emplace_back("custom");
	return view;
}

void QtProjectWizard::reportChanged() const
{
	if (!m_applying)
	{
		ProjectWizardStateMachine::instance()->changed(view());
	}
}

std::string QtProjectWizard::failure(const std::string& otherwise) const
{
	return lastMessage().isEmpty() ? otherwise : lastMessage().toStdString();
}

void QtProjectWizard::apply(const ProjectWizardRequest& request)
{
	using Kind = ProjectWizardRequest::Kind;
	const std::shared_ptr<ProjectWizardStateMachine> machine = ProjectWizardStateMachine::instance();
	const QScopedValueRollback<bool> applying(m_applying, true);
	clearLastMessage();

	switch (request.kind)
	{
	case Kind::Set:
	{
		if (m_editing && (request.projectName || request.projectLocation))
		{
			machine->failed(view(), "the name and location of a project are fixed once it exists");
			return;
		}
		if (request.projectName &&
			!QtProjectWizardContentProjectData::isValidProjectName(QString::fromStdString(*request.projectName)))
		{
			machine->failed(
				view(),
				"the project name is not a valid file name: letters, digits, '.', '_' and '-' only, "
				"not starting with '-'");
			return;
		}
		if (content())
		{
			saveContent();	// what a person typed meanwhile stays
		}
		if (request.projectName || request.projectLocation)
		{
			m_projectSettings->setProjectFilePath(
				request.projectName.value_or(m_projectSettings->getProjectName()),
				FilePath(request.projectLocation.value_or(m_projectSettings->getProjectDirectoryPath().str())));
		}
		std::string errors;
		if (!request.fields.empty() && m_previouslySelectedIndex >= 0)
		{
			SourceGroupSettings& group = *m_allSourceGroupSettings[m_previouslySelectedIndex];
			for (const auto& [name, value]: request.fields)
			{
				if (const auto written = writeSourceGroupField(group, name, value); !written)
				{
					errors += (errors.empty() ? "" : "; ") + written.error();
				}
			}
			updateSourceGroupList();
			m_sourceGroupList->blockSignals(true);
			m_sourceGroupList->setCurrentRow(m_previouslySelectedIndex);
			m_sourceGroupList->blockSignals(false);
		}
		if (content())
		{
			loadContent();
		}
		errors.empty() ? machine->applied(view()) : machine->failed(view(), errors);
		return;
	}
	case Kind::Show:
	{
		if (request.sourceGroup)
		{
			m_sourceGroupList->setCurrentRow(static_cast<int>(*request.sourceGroup));
		}
		else if (m_previouslySelectedIndex != -1)
		{
			generalButtonClicked();
		}
		const bool shown = request.sourceGroup ? m_previouslySelectedIndex == static_cast<int>(*request.sourceGroup)
											   : m_previouslySelectedIndex == -1;
		shown ? machine->applied(view())
			  : machine->failed(view(), failure("the page shown could not be left"));
		return;
	}
	case Kind::AddSourceGroup:
	{
		if (request.type == SourceGroupType::UNKNOWN)
		{
			newSourceGroup();	// the type selection: choosing_type
			if (!m_choosingType)
			{
				machine->failed(view(), failure("the page shown could not be left"));
			}
			return;
		}
		if (!canExitContent())
		{
			machine->failed(view(), failure("the page shown could not be left"));
			return;
		}
		const std::size_t groups = m_allSourceGroupSettings.size();
		selectedProjectType(request.type);
		m_allSourceGroupSettings.size() > groups
			? machine->applied(view())
			: machine->failed(view(), "this build has no source groups of that type");
		return;
	}
	case Kind::SelectType:
	{
		if (!m_choosingType)
		{
			machine->failed(view(), "the type selection is not open");
			return;
		}
		selectedProjectType(request.type);
		machine->applied(view());
		return;
	}
	case Kind::RemoveSourceGroup:
	{
		const int row = request.sourceGroup ? static_cast<int>(*request.sourceGroup) : m_previouslySelectedIndex;
		if (row < 0 || row >= int(m_allSourceGroupSettings.size()))
		{
			machine->failed(view(), "no source group to remove");
			return;
		}
		removeSourceGroup(row);
		machine->applied(view());
		return;
	}
	case Kind::Create:
	{
		if (content())
		{
			saveContent();
			if (!content()->check())
			{
				machine->failed(view(), failure("a check on the page failed"));
				return;
			}
		}
		if (m_allSourceGroupSettings.empty())
		{
			machine->failed(view(), "a project needs a source group: add_source_group");
			return;
		}
		if (!createProject())
		{
			machine->failed(view(), failure("the project could not be saved"));
		}
		return;	// created
	}
	case Kind::Cancel:
	{
		if (m_choosingType)
		{
			cancelSourceGroup();
			machine->applied(view());
			return;
		}
		cancelWizard();	// canceled
		return;
	}
	case Kind::New:
	case Kind::Edit:
		break;
	}
	machine->failed(view(), "the main window opens the wizard");
}
