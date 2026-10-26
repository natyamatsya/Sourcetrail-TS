#ifndef QT_PREFERENCES_WINDOW_H
#define QT_PREFERENCES_WINDOW_H

#include "QtProjectWizardWindow.h"

#include "ApplicationSettings.h"
#include "PreferencesStateMachine.h"

class QtProjectWizardContentPaths;
class QtProjectWizardContentPreferences;

class QtPreferencesWindow: public QtProjectWizardWindow
{
	Q_OBJECT

public:
	QtPreferencesWindow(QWidget* parent = nullptr);
	~QtPreferencesWindow() override;

	// Carries out what an agent asked (PreferencesStateMachine) and reports how it went.
	void apply(const PreferencesRequest& request);

protected:
	void windowReady() override;
	void handleNext() override;

private:
	// The fields as shown, by name (PreferencesStateMachine).
	PreferenceFields fields() const;

	ApplicationSettings m_appSettings;

	QtProjectWizardContentPreferences* m_preferences = nullptr;
	QtProjectWizardContentPaths* m_headerSearchPaths = nullptr;
	QtProjectWizardContentPaths* m_frameworkSearchPaths = nullptr;
};

#endif	  // QT_PREFERENCES_WINDOW_H
