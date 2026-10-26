#ifndef QT_PROJECT_WIZARD_CONTENT_PATHS_SOURCE_H
#define QT_PROJECT_WIZARD_CONTENT_PATHS_SOURCE_H

#include "QtProjectWizardContentPaths.h"

class QtProjectWizardContentPathsSource: public QtProjectWizardContentPaths
{
	Q_OBJECT
public:
	QtProjectWizardContentPathsSource(
		std::shared_ptr<SourceGroupSettings> settings, QtProjectWizardWindow* window);

	// QtProjectWizardContent implementation
	void load() override;
	void save() override;

	bool check() override;

	std::vector<FilePath> getFilePaths() const override;
	QString getFileNamesTitle() const override;
	QString getFileNamesDescription(std::size_t count) const override;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PATHS_SOURCE_H
