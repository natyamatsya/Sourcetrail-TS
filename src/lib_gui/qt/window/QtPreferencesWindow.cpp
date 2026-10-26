#include "QtPreferencesWindow.h"

#include "language_package_flags.h"

#ifndef SRCTRL_MODULE_BUILD
#include "MessageLoadProject.h"
#include "MessagePluginPortChange.h"
#include "MessageRefreshUI.h"
#include "MessageScrollSpeedChange.h"
#endif

#include "Application.h"
#include "DialogView.h"
#include "QtProjectWizardContentGroup.h"
#include "QtProjectWizardContentPathsFrameworkSearchGlobal.h"
#include "QtProjectWizardContentPathsHeaderSearchGlobal.h"
#include "QtProjectWizardContentPreferences.h"
#include "Platform.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

QtPreferencesWindow::QtPreferencesWindow(QWidget* parent): QtProjectWizardWindow(parent)
{
	setWindowTitle(tr("Preferences"));

	// save old application settings so they can be compared later
	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
	m_appSettings.setHeaderSearchPaths(appSettings->getHeaderSearchPaths());
	m_appSettings.setFrameworkSearchPaths(appSettings->getFrameworkSearchPaths());
	m_appSettings.setScrollSpeed(appSettings->getScrollSpeed());
	m_appSettings.setSourcetrailPort(appSettings->getSourcetrailPort());
	m_appSettings.setPluginPort(appSettings->getPluginPort());

	m_appSettings.setScreenAutoScaling(appSettings->getScreenAutoScaling());
	m_appSettings.setScreenScaleFactor(appSettings->getScreenScaleFactor());


	QtProjectWizardContentGroup* summary = new QtProjectWizardContentGroup(this);
	m_preferences = new QtProjectWizardContentPreferences(this);
	summary->addContent(m_preferences);

	if constexpr (language_packages::buildCxxLanguagePackage)
	{
		m_headerSearchPaths = new QtProjectWizardContentPathsHeaderSearchGlobal(this);
		summary->addContent(m_headerSearchPaths);
		if constexpr (utility::Platform::isMac())
		{
			m_frameworkSearchPaths = new QtProjectWizardContentPathsFrameworkSearchGlobal(this);
			summary->addContent(m_frameworkSearchPaths);
		}
	}

	// Closed without saving (Cancel, Escape, the window's close button); after a save the
	// machine is closed already and takes no notice.
	connect(this, &QtWindow::canceled, this, []() { PreferencesStateMachine::instance()->canceled(); });

	setPreferredSize(QSize(750, 500));
	setContent(summary);
	setScrollAble(true);
}

QtPreferencesWindow::~QtPreferencesWindow() = default;

void QtPreferencesWindow::windowReady()
{
	QtProjectWizardWindow::windowReady();

	updateTitle(tr("PREFERENCES"));
	updateNextButton(tr("Save"));
	setPreviousVisible(false);

	loadContent();
	PreferencesStateMachine::instance()->opened(fields());
}

PreferenceFields QtPreferencesWindow::fields() const
{
	PreferenceFields fields = m_preferences->fields();
	const auto addPaths = [&fields](const std::string& name, const QtProjectWizardContentPaths* content) {
		if (content == nullptr)
		{
			return;
		}
		std::vector<std::string> paths;
		for (const FilePath& path: content->listPaths())
		{
			paths.push_back(path.str());
		}
		fields.emplace(name, std::move(paths));
	};
	addPaths("header_search_paths", m_headerSearchPaths);
	addPaths("framework_search_paths", m_frameworkSearchPaths);
	return fields;
}

void QtPreferencesWindow::apply(const PreferencesRequest& request)
{
	const std::shared_ptr<PreferencesStateMachine> machine = PreferencesStateMachine::instance();
	clearLastMessage();
	switch (request.kind)
	{
		using enum PreferencesRequest::Kind;
	case Open:
		machine->opened(fields());	// open already
		break;
	case Set:
		for (const auto& [name, value]: request.fields)
		{
			QtProjectWizardContentPaths* const paths = name == "header_search_paths" ? m_headerSearchPaths
				: name == "framework_search_paths"									 ? m_frameworkSearchPaths
																					 : nullptr;
			if (paths != nullptr)
			{
				const auto* list = std::get_if<std::vector<std::string>>(&value);
				if (list == nullptr)
				{
					machine->failed(fields(), PreferenceFieldFailure {.error = PreferenceFieldError::NotAList}.message(name));
					return;
				}
				paths->setListPaths(std::vector<FilePath>(list->begin(), list->end()));
			}
			else if (const auto set = m_preferences->setField(name, value); !set)
			{
				machine->failed(fields(), set.error().message(name));
				return;
			}
		}
		machine->applied(fields());
		break;
	case Save:
		handleNext();	// saved, or failed with what it told
		break;
	case Cancel:
		handleClose();	// canceled
		break;
	}
}

void QtPreferencesWindow::handleNext()
{
	if (!content()->check())
	{
		// An agent's save: what the check told (a person's Save the machine takes no notice of).
		PreferencesStateMachine::instance()->failed(
			fields(), lastMessage().isEmpty() ? "not saved" : lastMessage().toStdString());
		return;
	}

	saveContent();

	Application* app = Application::getInstance().get();
	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();

	bool needsRestart = m_appSettings.getScreenAutoScaling() != appSettings->getScreenAutoScaling() ||
		m_appSettings.getScreenScaleFactor() != appSettings->getScreenScaleFactor();

	if (needsRestart)
	{
		app->getDialogView(DialogView::UseCase::PROJECT_SETUP)->notify(
			tr("<p>Please <b>restart</b> the application for all changes to take effect.</p>"
			"<p>Note: ""These changes may harm ""the execution of the application. "
			"In case the application is not usable ""anymore, please run the "
			"<b>resetPreferences.sh</b> script located in your install directory.</p>").toStdString());
	}


	bool appSettingsChanged = !(m_appSettings == *appSettings);

	if (m_appSettings.getScrollSpeed() != appSettings->getScrollSpeed())
	{
		MessageScrollSpeedChange(appSettings->getScrollSpeed()).dispatch();
	}

	if (m_appSettings.getSourcetrailPort() != appSettings->getSourcetrailPort() ||
		m_appSettings.getPluginPort() != appSettings->getPluginPort())
	{
		MessagePluginPortChange().dispatch();
	}

	Application::loadSettings();

	if (appSettingsChanged)
	{
		std::shared_ptr<const Project> currentProject =
			Application::getInstance()->getCurrentProject();
		if (currentProject)
		{
			MessageLoadProject(currentProject->getProjectSettingsFilePath(), true).dispatch();
		}
	}
	else
	{
		MessageRefreshUI().dispatch();
	}

	PreferencesStateMachine::instance()->saved();
	QtWindow::handleClose();
}
