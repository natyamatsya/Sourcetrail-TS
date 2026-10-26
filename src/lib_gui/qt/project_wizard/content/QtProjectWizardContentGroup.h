#ifndef QT_PROJECT_WIZARD_CONTENT_GROUP_H
#define QT_PROJECT_WIZARD_CONTENT_GROUP_H

#include "QtProjectWizardContent.h"

class QCheckBox;

class QtProjectWizardContentGroup: public QtProjectWizardContent
{
	Q_OBJECT

public:
	QtProjectWizardContentGroup(QtProjectWizardWindow* window);

	void addContent(QtProjectWizardContent* content);
	void addSpace();

	bool hasContents() const;

protected:
	// QtProjectWizardContent implementation
	void populate(QGridLayout* layout, int& row) override;

	void load() override;
	void save() override;
	void refresh() override;
	bool check() override;
	void describe(ProjectWizardView& view) const override;

private:
	std::vector<QtProjectWizardContent*> m_contents;
	bool m_isForm;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_GROUP_H
