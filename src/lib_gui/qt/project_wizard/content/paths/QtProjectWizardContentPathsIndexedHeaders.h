#ifndef QT_PROJECT_WIZARD_CONTENT_PATHS_INDEXED_HEADER_PATHS_H
#define QT_PROJECT_WIZARD_CONTENT_PATHS_INDEXED_HEADER_PATHS_H

#include "QtProjectWizardContentPaths.h"

class SourceGroupSettingsCxxCdb;
class SourceGroupSettingsWithCxxCMakeBuildDirectory;

class QtProjectWizardContentPathsIndexedHeaders: public QtProjectWizardContentPaths
{
	Q_OBJECT

public:
	static std::vector<FilePath> getIndexedPathsDerivedFromCDB(
		std::shared_ptr<const SourceGroupSettingsCxxCdb> settings);

	// The directories of the source files of the targets the settings select, and the
	// (non-system) include paths of their compile groups, as the build directory's
	// CMake File API reply has them; empty without a reply.
	static std::vector<FilePath> getIndexedPathsDerivedFromCMake(
		const SourceGroupSettingsWithCxxCMakeBuildDirectory& settings);

	QtProjectWizardContentPathsIndexedHeaders(
		std::shared_ptr<SourceGroupSettings> settings,
		QtProjectWizardWindow* window,
		const std::string& projectKindName);

	void populate(QGridLayout* layout, int& row) override;

	// QtProjectWizardContent implementation
	void load() override;
	void save() override;

	bool check() override;

private slots:
	void buttonClicked();
	void savedFilesDialog();

private:
	// Shows the dialog to select from `paths`, which the source group's settings say.
	void showSelectPathsDialog(
		const QString& title, const QString& description, const std::vector<FilePath>& paths);

	const std::string m_projectKindName;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PATHS_INDEXED_HEADER_PATHS_H
