#ifndef QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H
#define QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H

#include <expected>
#include <string>
#include <vector>

#include "CMakeFileAPIReader.h"
#include "FilePath.h"
#include "QtProjectWizardContent.h"

class QCheckBox;
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
	void onTargetListFilterEdited();
	void onRunCMake();
	void onShowCMakeLog();

private:
	// A target of the reply, with the source files it has in the configuration shown.
	struct Target
	{
		std::string name;
		std::string type;
		std::size_t sourceFileCount{0};
		std::vector<FilePath> sources;	// the translation units it compiles (not generated ones)
		// What its compile groups say, for its tooltip.
		std::vector<std::string> includes;
		std::vector<std::string> defines;
		std::string standard;	// "c++17", from its -std flag; empty without one
		std::size_t generatedCount{0};
	};

	// The row's tooltip: what the target compiles, and how.
	static QString targetDetails(const Target& target);

	// What a target is, for its group in the list: library, executable, test or utility.
	static std::string targetKind(const Target& target);
	// The target's row text, with what it adds to the checked targets' files when checked.
	QString targetText(const Target& target, bool checked) const;
	// The files the indexed targets have, each once.
	std::size_t checkedFileCount() const;
	// The filter indexing uses: the typed one, with the targets they link to when asked.
	std::string effectiveTargetFilter() const;
	// Updates the checked rows' texts (what each adds) after a check.
	void updateTargetTexts();

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
	// The last configure's error under the build directory, with its log; hidden when it
	// did not fail.
	void showConfigureError();
	// The CMake Arguments field's words; the reason when they cannot be split.
	std::expected<std::vector<std::string>, std::string> cmakeArguments() const;
	// Shows why the check failed; always false.
	bool fail(const QString& message);

	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> m_settings;

	QtLocationPicker* m_sourceDirPicker = nullptr;
	QComboBox* m_presetCombo = nullptr;
	QLabel* m_presetStatusLabel = nullptr;
	QtLocationPicker* m_buildDirPicker = nullptr;	// without a preset
	QLabel* m_buildDirLabel = nullptr;
	QPushButton* m_runCMakeButton = nullptr;
	QPushButton* m_refreshPresetsButton = nullptr;
	QLabel* m_cmakeArgumentsLabel = nullptr;
	QWidget* m_cmakeArgumentsRow = nullptr;
	QLineEdit* m_cmakeArgumentsEdit = nullptr;
	QWidget* m_cmakeArgumentsHelp = nullptr;
	QWidget* m_configureErrorRow = nullptr;
	QLabel* m_configureErrorLabel = nullptr;
	QComboBox* m_configurationCombo = nullptr;
	QLineEdit* m_targetFilterEdit = nullptr;
	QListWidget* m_targetList = nullptr;
	QLineEdit* m_targetListFilterEdit = nullptr;	// narrows the list; not the indexing filter
	QCheckBox* m_showEmptyTargetsCheck = nullptr;
	QCheckBox* m_indexLinkedTargetsCheck = nullptr;
	QLabel* m_targetSummaryLabel = nullptr;

	FilePath m_sourceDir;	// expanded and absolute
	std::vector<CMakeFileAPIReader::PresetInfo> m_presets;
	FilePath m_buildDir;	// of the selected preset; empty if it has none
	bool m_hasReply = false;
	bool m_configured = false;	// CMake configured and generated the build directory, reply or not
	bool m_configureFailed = false;	// CMake's last configure of the build directory failed
	bool m_runningCMake = false;
	FilePath m_failedBuildDir;	// where Run CMake failed last (older CMake versions write no error reply)
	std::string m_configureError;	// the first error of the last configure that failed
	bool m_missesSubmodules = false;	// ... and it looks like a checkout without its submodules
	std::vector<std::string> m_configurations;
	std::vector<Target> m_targets;
	std::vector<CMakeFileAPIReader::TargetEntry> m_targetEntries;	// with their dependencies
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PATH_CMAKE_FILE_API_H
