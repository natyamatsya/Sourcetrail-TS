#ifndef QT_PROJECT_WIZARD_CONTENT_SWIFT_OPTIONS_H
#define QT_PROJECT_WIZARD_CONTENT_SWIFT_OPTIONS_H

#include <memory>

#include "QtProjectWizardContent.h"

class QComboBox;
class QLineEdit;
class QtLocationPicker;
class SourceGroupSettingsWithSwiftOptions;

// Swift project-model options for Swift source groups (SW5): extra `swift build`
// args, a toolchain override, and an index-store override. The Swift analog of
// QtProjectWizardContentCargoOptions.
class QtProjectWizardContentSwiftOptions: public QtProjectWizardContent
{
	Q_OBJECT

public:
	QtProjectWizardContentSwiftOptions(
		std::shared_ptr<SourceGroupSettingsWithSwiftOptions> settings,
		QtProjectWizardWindow* window);

	// QtProjectWizardContent implementation
	void populate(QGridLayout* layout, int& row) override;

	void load() override;
	void save() override;

private:
	std::shared_ptr<SourceGroupSettingsWithSwiftOptions> m_settings;

	QLineEdit* m_buildArgs = nullptr;
	QtLocationPicker* m_toolchainPath = nullptr;
	QtLocationPicker* m_indexStorePath = nullptr;
	QComboBox* m_specializationScope = nullptr;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_SWIFT_OPTIONS_H
