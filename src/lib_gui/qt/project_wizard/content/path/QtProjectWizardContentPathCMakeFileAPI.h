#ifndef QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H
#define QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H

#include "QtProjectWizardContent.h"

class QComboBox;
class QLabel;
class QtLocationPicker;
class SourceGroupSettingsCxxCMakeFileAPI;

class QtProjectWizardContentPathCMakeFileAPI: public QtProjectWizardContent
{
	Q_OBJECT

public:
	QtProjectWizardContentPathCMakeFileAPI(
		std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings,
		QtProjectWizardWindow* window);

	void populate(QGridLayout* layout, int& row) override;

	void load() override;
	void save() override;
	bool check() override;

private slots:
	void onSourceDirChanged(const QString& text);
	void onRefreshPresets();

private:
	void repopulatePresets(const QString& sourceDir);

	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> m_settings;

	QtLocationPicker* m_sourceDirPicker = nullptr;
	QComboBox* m_presetCombo = nullptr;
	QLabel* m_presetStatusLabel = nullptr;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H
