#ifndef QT_PROJECT_WIZARD_CONTENT_CARGO_OPTIONS_H
#define QT_PROJECT_WIZARD_CONTENT_CARGO_OPTIONS_H

#include <memory>

#include "QtProjectWizardContent.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QtLocationPicker;
class SourceGroupSettingsWithCargoOptions;

// Cargo project-model options for Rust source groups (project model v1):
// feature selection and target triple.
class QtProjectWizardContentCargoOptions: public QtProjectWizardContent
{
	Q_OBJECT

public:
	QtProjectWizardContentCargoOptions(
		std::shared_ptr<SourceGroupSettingsWithCargoOptions> settings,
		QtProjectWizardWindow* window);

	// QtProjectWizardContent implementation
	void populate(QGridLayout* layout, int& row) override;

	void load() override;
	void save() override;

private:
	std::shared_ptr<SourceGroupSettingsWithCargoOptions> m_settings;

	QtLocationPicker* m_workspaceDir = nullptr;
	QLineEdit* m_features = nullptr;
	QCheckBox* m_allFeatures = nullptr;
	QCheckBox* m_noDefaultFeatures = nullptr;
	QLineEdit* m_targetTriple = nullptr;
	QComboBox* m_specializationScope = nullptr;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_CARGO_OPTIONS_H
