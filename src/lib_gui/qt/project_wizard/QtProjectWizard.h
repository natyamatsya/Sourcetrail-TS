#ifndef QT_PROJECT_WIZARD_H
#define QT_PROJECT_WIZARD_H

#include <functional>

#include <QWidget>

#include "ApplicationSettings.h"
#include "ProjectSettings.h"
#include "ProjectWizardStateMachine.h"
#include "QtProjectWizardWindow.h"
#include "QtWindow.h"

class QListWidget;
class QPushButton;
class QtProjectWizardContent;
class QtProjectWizardContentGroup;
class QtProjectWizardWindow;

class QtProjectWizard: public QtProjectWizardWindow
{
	Q_OBJECT

public:
	QtProjectWizard(QWidget* parent = nullptr);
	~QtProjectWizard() override;
	QSize sizeHint() const override;

	// Carries out what an agent asked (ProjectWizardStateMachine), and reports how it
	// went; New and Edit are the main window's.
	void apply(const ProjectWizardRequest& request);

public slots:
	void newProject();

	void newProjectFromCDB(const FilePath& filePath);

	void editProject(const FilePath& settingsPath);
	void editProject(std::shared_ptr<ProjectSettings> settings);

protected:
	void populateWindow(QWidget* widget) override;
	void windowReady() override;

	void handlePrevious() override;

private:
	QtProjectWizardWindow* createWindowWithContent(
		std::function<QtProjectWizardContent*(QtProjectWizardWindow*)> func);

	void updateSourceGroupList();
	bool canExitContent();

	// The wizard as the project wizard machine shows it.
	ProjectWizardView view() const;
	// A person changed the page: the machine's context follows.
	void reportChanged() const;
	void removeSourceGroup(int row);
	// What keeps an agent's request from going through: what was told last, or `otherwise`.
	std::string failure(const std::string& otherwise) const;

	QtWindowStack m_windowStack;

	std::shared_ptr<ProjectSettings> m_projectSettings;
	std::vector<std::shared_ptr<SourceGroupSettings>> m_allSourceGroupSettings;
	ApplicationSettings m_appSettings;

	bool m_editing = false;
	int m_previouslySelectedIndex = -1;

	bool m_applying = false;	// an agent's request is carried out: it reports itself
	bool m_choosingType = false;	// the type selection is open
	bool m_typeSelected = false;	// a type was chosen there

	QPushButton* m_generalButton;
	QPushButton* m_removeButton;
	QPushButton* m_duplicateButton;
	QListWidget* m_sourceGroupList;
	QWidget* m_contentWidget = nullptr;

private slots:
	void generalButtonClicked();
	void selectedSourceGroupChanged(int index);
	void selectedSourceGroupNameChanged(const QString& name);
	void removeSelectedSourceGroup();
	void duplicateSelectedSourceGroup();

	void cancelSourceGroup();

	void cancelWizard();
	void finishWizard();

	void windowStackChanged();

	void newSourceGroup();
	void selectedProjectType(SourceGroupType sourceGroupType);

	void createSourceGroup(std::shared_ptr<SourceGroupSettings> settings);
	// Saves the project and loads it; false (told why) when it could not be saved.
	bool createProject();
};

#endif	  // QT_PROJECT_WIZARD_H
