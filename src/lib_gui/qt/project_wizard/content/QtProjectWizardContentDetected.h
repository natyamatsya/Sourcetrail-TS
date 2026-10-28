#ifndef QT_PROJECT_WIZARD_CONTENT_DETECTED_H
#define QT_PROJECT_WIZARD_CONTENT_DETECTED_H

#include "ProjectWizardStateMachine.h"
#include "QtProjectWizardContent.h"

// What New Project found in the folder, above the fields it filled in: why the project is as
// proposed (the first reason sums it up), and, quieter, what it noticed besides (the git
// repository, its submodules, build systems it does not read yet). Nothing to confirm: the
// fields below are the person's to change.
class QtProjectWizardContentDetected: public QtProjectWizardContent
{
	Q_OBJECT

public:
	QtProjectWizardContentDetected(ProjectWizardView::Detection detection, QtProjectWizardWindow* window);

	void populate(QGridLayout* layout, int& row) override;

private:
	const ProjectWizardView::Detection m_detection;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_DETECTED_H
