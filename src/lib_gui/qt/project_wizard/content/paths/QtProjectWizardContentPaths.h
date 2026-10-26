#ifndef QT_PROJECT_WIZARD_CONTENT_PATHS_H
#define QT_PROJECT_WIZARD_CONTENT_PATHS_H

#include <set>

#include "CombinedPathDetector.h"
#include "QtPathListBox.h"
#include "QtProjectWizardContent.h"

class QComboBox;
class SourceGroupSettings;

class QtProjectWizardContentPaths: public QtProjectWizardContent
{
	Q_OBJECT

signals:
	void showSourceFiles();

public:
	QtProjectWizardContentPaths(
		std::shared_ptr<SourceGroupSettings> settings,
		QtProjectWizardWindow* window,
		QtPathListBox::SelectionPolicyType selectionPolicy,
		bool checkMissingPaths);

	// QtSettingsWindow implementation
	void populate(QGridLayout* layout, int& row) override;

	bool check() override;

	// The list's paths as a person edits them (a global list's own read-only entries
	// left out), and replacing them, for the Preferences machine.
	virtual std::vector<FilePath> listPaths() const;
	virtual void setListPaths(const std::vector<FilePath>& paths);

protected:
	void setTitleString(const QString& title);
	void setHelpString(const QString& help);

	void addDetection(QGridLayout* layout, int row);

	virtual void detectedPaths(const std::vector<FilePath>& paths);

	std::shared_ptr<SourceGroupSettings> m_settings;

	QtPathListBox* m_list;

	QString m_showFilesString;
	std::shared_ptr<CombinedPathDetector> m_pathDetector;

	bool m_makePathsRelativeToProjectFileLocation = true;

private slots:
	void detectionClicked();

private:
	const QtPathListBox::SelectionPolicyType m_selectionPolicy;
	const bool m_checkMissingPaths;
	QString m_titleString;
	QString m_helpString;

	QComboBox* m_detectorBox;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PATHS_H
