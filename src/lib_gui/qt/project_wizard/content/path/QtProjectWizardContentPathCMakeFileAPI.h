#ifndef QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H
#define QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H

#include <string>
#include <vector>

#include "CMakeFileAPIReader.h"
#include "FilePath.h"
#include "QtProjectWizardContent.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QtLocationPicker;
class SourceGroupSettingsCxxCMakeFileAPI;

// The CMake File API source group's own rows: the source directory, the configure
// preset (and the build directory it resolves to), and -- once the build directory
// has a File API reply -- the configuration and the targets to index, with what
// each target contributes.
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
	void describe(ProjectWizardView& view) const override;

private slots:
	void onSourceDirChanged(const QString& text);
	void onRefreshPresets();
	void onPresetChanged();
	void onConfigurationChanged();
	void onTargetFilterEdited();
	void onTargetChecked();
	void onRunCMake();

private:
	// A target of the reply, with the source files it has in the configuration shown.
	struct Target
	{
		std::string name;
		std::string type;
		std::size_t sourceFileCount{0};
	};

	// Lists the source directory's presets, keeping `presetToKeep` (or the "no preset"
	// choice) selected.
	void repopulatePresets(
		const QString& sourceDir, const std::string& presetToKeep, bool withoutPreset = false);
	std::string selectedPreset() const;	// empty for none, and for "No preset"
	bool noPresetSelected() const;
	// The build directory of the selected preset, and what its reply has.
	void readBuildDirectory();
	void readTargets();
	void showTargets();
	void updateTargetSummary();
	QString targetSummary() const;
	QString buildDirectoryNote() const;
	// Shows why the check failed; always false.
	bool fail(const QString& message);

	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> m_settings;

	QtLocationPicker* m_sourceDirPicker = nullptr;
	QComboBox* m_presetCombo = nullptr;
	QLabel* m_presetStatusLabel = nullptr;
	QtLocationPicker* m_buildDirPicker = nullptr;	// without a preset
	QLabel* m_buildDirLabel = nullptr;
	QPushButton* m_runCMakeButton = nullptr;
	QComboBox* m_configurationCombo = nullptr;
	QLineEdit* m_targetFilterEdit = nullptr;
	QListWidget* m_targetList = nullptr;
	QLabel* m_targetSummaryLabel = nullptr;

	FilePath m_sourceDir;	// expanded and absolute
	std::vector<CMakeFileAPIReader::PresetInfo> m_presets;
	FilePath m_buildDir;	// of the selected preset; empty if it has none
	bool m_hasReply = false;
	bool m_configured = false;	// CMake configured and generated the build directory, reply or not
	bool m_configureFailed = false;	// CMake's last configure of the build directory failed
	bool m_runningCMake = false;
	FilePath m_failedBuildDir;	// where Run CMake failed last (older CMake versions write no error reply)
	std::vector<std::string> m_configurations;
	std::vector<Target> m_targets;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H
