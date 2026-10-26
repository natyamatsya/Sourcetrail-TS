#ifndef QT_PROJECT_WIZARD_CONTENT_PROJECT_DATA_H
#define QT_PROJECT_WIZARD_CONTENT_PROJECT_DATA_H

#include "QtLocationPicker.h"
#include "QtProjectWizardContent.h"

class ProjectSettings;

class QtProjectWizardContentProjectData: public QtProjectWizardContent
{
	Q_OBJECT

public:
	QtProjectWizardContentProjectData(
		std::shared_ptr<ProjectSettings> projectSettings,
		QtProjectWizardWindow* window,
		bool disableNameEditing = false);

	// QtProjectWizardContent implementation
	void populate(QGridLayout* layout, int& row) override;

	void load() override;
	void save() override;
	bool check() override;

	// A name the project file can carry on every platform: no path separators or reserved characters.
	static bool isValidProjectName(const QString& name);

public slots:
	void onProjectNameEdited(QString text);

private:
	std::shared_ptr<ProjectSettings> m_projectSettings;

	bool m_disableNameEditing;
	QLineEdit* m_projectName = nullptr;
	QtLocationPicker* m_projectFileLocation = nullptr;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PROJECT_DATA_H
