#ifndef QT_PROJECT_WIZARD_CONTENT_PATH_MAKEFILE_H
#define QT_PROJECT_WIZARD_CONTENT_PATH_MAKEFILE_H

#include <expected>
#include <optional>

#include "MakeCompileCommands.h"
#include "QtProjectWizardContentPath.h"
#include "SingleValueCache.h"

class QComboBox;
class QLabel;
class SourceGroupSettingsCxxMake;

// The Makefile of a 'C/C++ from Makefile' group: its directory, and the goal and variables make is
// given. Picking the directory lists its goals and, when none is set yet, takes the first whose dry
// run compiles anything; the page shows what the dry run compiles, or why it compiles nothing.
class QtProjectWizardContentPathMakefile: public QtProjectWizardContentPath
{
	Q_OBJECT

public:
	QtProjectWizardContentPathMakefile(
		std::shared_ptr<SourceGroupSettingsCxxMake> settings, QtProjectWizardWindow* window);

	// QtProjectWizardContent implementation
	void populate(QGridLayout* layout, int& row) override;

	void load() override;
	void save() override;
	void refresh() override;
	bool check() override;
	// The makefile's goals (choices for make_arguments) and what the dry run compiles, or why
	// nothing (its note).
	void describe(ProjectWizardView& view) const override;

	std::vector<FilePath> getFilePaths() const override;
	QString getFileNamesTitle() const override;
	QString getFileNamesDescription(std::size_t count) const override;

private slots:
	void onDirectoryChanged(const QString& text);
	void onArgumentsEdited();

private:
	std::shared_ptr<SourceGroupSettings> getSourceGroupSettings() override;

	// The goals of the makefile in the directory, the likeliest first; and the first that compiles,
	// set when no goal is set yet.
	void listTargets();
	// Runs the dry run on the settings as they are, writing the group's database.
	void runDryRun();
	// The indexed header paths the compile commands suggest, as for a compilation database.
	void deriveIndexedHeaderPaths();

	std::shared_ptr<SourceGroupSettingsCxxMake> m_settings;
	QComboBox* m_argumentsCombo = nullptr;
	QLabel* m_fileCountLabel = nullptr;
	std::optional<std::expected<make::DryRun, make::Error>> m_dryRun;
	std::string m_dryRunOf;	   // the settings it ran on, so it runs again only when they change
	QString m_listedDirectory;	  // whose goals the combo lists
	std::vector<std::string> m_goals;	 // its goals, the likeliest first
	mutable SingleValueCache<std::vector<FilePath>> m_filePaths;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PATH_MAKEFILE_H
