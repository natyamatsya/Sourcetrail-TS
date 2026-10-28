#ifndef QT_PROJECT_WIZARD_CONTENT_PREFERENCES_H
#define QT_PROJECT_WIZARD_CONTENT_PREFERENCES_H

#include <expected>
#include <string>
#include <utility>
#include <vector>

#include <QComboBox>

#include "PreferencesStateMachine.h"
#include "QtLocationPicker.h"
#include "QtProjectWizardContent.h"

class QCheckBox;
class QFontComboBox;
class QLabel;
class QLineEdit;


class QtComboBoxPlaceHolder: public QComboBox
{
	Q_OBJECT

signals:
	void opened();

public:
	void showPopup() override
	{
		emit opened();
	}
};


class QtProjectWizardContentPreferences: public QtProjectWizardContent
{
	Q_OBJECT

public:
	QtProjectWizardContentPreferences(QtProjectWizardWindow* window);
	~QtProjectWizardContentPreferences() override;

	// QtProjectWizardContent implementation
	void populate(QGridLayout* layout, int& row) override;

	void load() override;
	void save() override;
	bool check() override;

	// The fields as the dialog shows them, by name (PreferencesStateMachine), and setting
	// one as a person does: a list's item by its text, a check box by clicking it.
	PreferenceFields fields() const;
	[[nodiscard]] std::expected<void, PreferenceFieldFailure> setField(
		const std::string& name, const PreferenceField& value);

private slots:
	void colorSchemeChanged();
	void loggingEnabledChanged();
	void indexerThreadsChanges(int index);
	void uiAutoScalingChanges(int index);
	void uiScaleFactorChanges(int index);

private:
	static void addTitle(const QString& title, QGridLayout* layout, int& row);
	void addLabel(const QString& label, QGridLayout* layout, int row);
	static void addWidget(
		QWidget* widget, QGridLayout* layout, int row, Qt::Alignment widgetAlignment = Qt::Alignment());
	void addLabelAndWidget(
		const QString& label,
		QWidget* widget,
		QGridLayout* layout,
		int row,
		Qt::Alignment widgetAlignment = Qt::Alignment());
	static void addGap(QGridLayout* layout, int& row);

	QCheckBox* addCheckBox(
		const QString& label,
		const QString& text,
		const QString& helpText,
		QGridLayout* layout,
		int& row);
	QComboBox* addComboBox(const QString& label, const QString& helpText, QGridLayout* layout, int& row);
	QComboBox* addComboBoxWithWidgets(
		const QString& label,
		const QString& helpText,
		std::vector<QWidget*> widgets,
		QGridLayout* layout,
		int& row);
	QComboBox* addComboBox(
		const QString& label, int min, int max, const QString& helpText, QGridLayout* layout, int& row);
	QComboBox* addComboBoxWithWidgets(
		const QString& label,
		int min,
		int max,
		const QString& helpText,
		std::vector<QWidget*> widgets,
		QGridLayout* layout,
		int& row);
	QLineEdit* addLineEdit(const QString& label, const QString& helpText, QGridLayout* layout, int& row);

	// The fields by name, in the dialog's order (font_face is the placeholder until opened).
	std::vector<std::pair<std::string, QWidget*>> fieldWidgets() const;
	[[nodiscard]] std::expected<void, PreferenceFieldFailure> setFontFace(const QString& name);

	QFontComboBox* m_fontFace;
	QtComboBoxPlaceHolder* m_fontFacePlaceHolder;

	QComboBox* m_fontSize;
	QComboBox* m_tabWidth;
	QComboBox* m_textEncoding;

	QComboBox* m_colorSchemes;
	QComboBox* m_colorSchemesDark;
	QCheckBox* m_followSystemColorScheme;
	std::vector<FilePath> m_colorSchemePaths;
	// The color scheme active when the dialog opened, restored on cancel.
	FilePath m_initialColorSchemePath;
	bool m_colorSchemeModified = false;
	bool m_colorSchemeSaved = false;

	QCheckBox* m_useAnimations;
	QCheckBox* m_showBuiltinTypes;
	QCheckBox* m_hideDeprecated;
	QCheckBox* m_showDirectoryInCode;

	QComboBox* m_screenAutoScaling = nullptr;
	QLabel* m_screenAutoScalingInfoLabel;

	QComboBox* m_screenScaleFactor = nullptr;
	QLabel* m_screenScaleFactorInfoLabel;

	QLineEdit* m_scrollSpeed;
	QCheckBox* m_graphZooming;

	QCheckBox* m_loggingEnabled;
	QCheckBox* m_verboseIndexerLoggingEnabled;
	QtLocationPicker* m_logPath;

	QLineEdit* m_sourcetrailPort;
	QLineEdit* m_pluginPort;
	QLineEdit* m_editorCommand;

	QComboBox* m_threads;
	QLabel* m_threadsInfoLabel;
};

#endif	  // QT_PROJECT_WIZARD_CONTENT_PREFERENCES_H
