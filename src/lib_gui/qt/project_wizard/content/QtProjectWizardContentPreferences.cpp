#include "QtProjectWizardContentPreferences.h"

#include "ApplicationSettings.h"
#include "FileLogger.h"
#include "FileSystem.h"
#include "MessageSwitchColorScheme.h"
#include "MessageTextEncodingChanged.h"
#include "QtActions.h"
#include "QtColorSchemeWatcher.h"
#include "ResourcePaths.h"
#include "TextCodec.h"
#include "logging.h"
#include "utility.h"
#include "utilityApp.h"
#include "utilityQt.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>

using namespace std;
using namespace utility;

QtProjectWizardContentPreferences::QtProjectWizardContentPreferences(QtProjectWizardWindow* window)
	: QtProjectWizardContent(window)
{
	m_colorSchemePaths = FileSystem::getFilePathsFromDirectory(
		ResourcePaths::getColorSchemesDirectoryPath(), {".json"});
}

QtProjectWizardContentPreferences::~QtProjectWizardContentPreferences()
{
	// Revert the live preview if the user dismissed the dialog without saving.
	if (m_colorSchemeModified && !m_colorSchemeSaved)
	{
		MessageSwitchColorScheme(m_initialColorSchemePath).dispatch();
	}
}

void QtProjectWizardContentPreferences::populate(QGridLayout* layout, int& row)
{
	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();

	// ui
	addTitle(tr("USER INTERFACE"), layout, row);

	// font face
	m_fontFacePlaceHolder = new QtComboBoxPlaceHolder();
	m_fontFace = new QFontComboBox();
	m_fontFace->setEditable(false);
	addLabelAndWidget(tr("Font Face"), m_fontFacePlaceHolder, layout, row);

	int rowNum = row;
	connect(m_fontFacePlaceHolder, &QtComboBoxPlaceHolder::opened, [this, rowNum, layout]()
	{
		m_fontFacePlaceHolder->hide();

		QString name = m_fontFace->currentText();
		m_fontFace->setFontFilters(QFontComboBox::MonospacedFonts);
		m_fontFace->setWritingSystem(QFontDatabase::Latin);
		m_fontFace->setCurrentText(name);

		addWidget(m_fontFace, layout, rowNum);

		QTimer::singleShot(10, [this]()
		{
			m_fontFace->showPopup();
		});
	});
	row++;

	// font size
	m_fontSize = addComboBox(tr("Font Size"),
		appSettings->getFontSizeMin(), appSettings->getFontSizeMax(),
		QLatin1String(""), layout, row);

	// tab width
	m_tabWidth = addComboBox(tr("Tab Width"), 1, 16, QLatin1String(""), layout, row);

	// text encoding
	m_textEncoding = addComboBox(tr("Text Encoding"), QLatin1String(""), layout, row);
	m_textEncoding->addItems(TextCodec::availableCodecs());

	// color scheme
	m_colorSchemes = addComboBox(tr("Color Scheme"), QLatin1String(""), layout, row);
	for (size_t i = 0; i < m_colorSchemePaths.size(); i++)
	{
		m_colorSchemes->insertItem(
			static_cast<int>(i),
			QString::fromStdString(m_colorSchemePaths[i].withoutExtension().fileName()));
	}
	connect(m_colorSchemes, qOverload<int>(&QComboBox::activated), this, &QtProjectWizardContentPreferences::colorSchemeChanged);

	// follow system Day/Night appearance (Qt QStyleHints::colorScheme)
	m_followSystemColorScheme = addCheckBox(tr("Day/Night Mode"),
		tr("Follow system appearance"),
		tr("<p>Automatically switch between the color scheme selected above (used while "
		   "the system is in light mode) and the dark color scheme below (used while the "
		   "system is in dark mode), following the operating system's Day/Night "
		   "appearance.</p>"),
		layout,
		row);
	connect(m_followSystemColorScheme, &QCheckBox::toggled, this, &QtProjectWizardContentPreferences::colorSchemeChanged);

	// dark color scheme (used when following the system appearance and it is dark)
	m_colorSchemesDark = addComboBox(tr("Dark Color Scheme"), QLatin1String(""), layout, row);
	for (size_t i = 0; i < m_colorSchemePaths.size(); i++)
	{
		m_colorSchemesDark->insertItem(
			static_cast<int>(i),
			QString::fromStdString(m_colorSchemePaths[i].withoutExtension().fileName()));
	}
	connect(m_colorSchemesDark, qOverload<int>(&QComboBox::activated), this, &QtProjectWizardContentPreferences::colorSchemeChanged);

	// animations
	m_useAnimations = addCheckBox(tr("Animations"),
		tr("Enable animations"),
		tr("<p>Enable animations throughout the user interface.</p>"),
		layout,
		row);

	// built-in types
	m_showBuiltinTypes = addCheckBox(tr("Built-in Types"),
		tr("Show built-in types in graph when referenced"),
		tr("<p>Enable display of referenced built-in types in the graph view.</p>"),
		layout,
		row);

	// hide deprecated
	m_hideDeprecated = addCheckBox(tr("Deprecated Symbols"),
		tr("Hide deprecated symbols in graph"),
		tr("<p>Hide declarations marked deprecated (Swift <code>@available(*, deprecated)</code>, "
		   "C++ <code>[[deprecated]]</code>, Rust <code>#[deprecated]</code>) from the graph view. "
		   "The active symbol is still shown.</p>"),
		layout,
		row);

	// directory in code
	m_showDirectoryInCode = addCheckBox(tr("Directory in File Title"),
		tr("Show directory of file in code title"),
		tr("<p>Enable display of the parent directory of a code file relative to the project file.</p>"),
		layout,
		row);
	layout->setRowMinimumHeight(row - 1, 30);

	addGap(layout, row);

	// UI scale

	// screen
	addTitle(tr("SCREEN"), layout, row);

	QLabel* hint = new QLabel(tr("<changes need restart>"));
	hint->setStyleSheet(QStringLiteral("color: grey"));
	layout->addWidget(hint, row - 1, QtProjectWizardWindow::BACK_COL, Qt::AlignRight);

	// auto scaling
	m_screenAutoScalingInfoLabel = new QLabel(QLatin1String(""));
	m_screenAutoScaling = addComboBoxWithWidgets(tr("Auto Scaling to DPI"),
		tr("<p>Define if automatic scaling to screen DPI resolution is active. "
			"This setting manipulates the environment flag %1 of the Qt framework "
			"(<a href=\"http://doc.qt.io/qt-5/highdpi.html\">http://doc.qt.io/qt-5/highdpi.html</a>). "
			"Choose <b>system</b> to stick to the setting of your current environment.</p>"
			"<p>Changes to this setting require a <b>restart</b> of the application to take effect.</p>"
		).arg(QT_AUTO_SCREEN_SCALE_FACTOR), {m_screenAutoScalingInfoLabel}, layout, row);
	m_screenAutoScaling->addItem(tr("system"), -1);
	m_screenAutoScaling->addItem(tr("off"), 0);
	m_screenAutoScaling->addItem(tr("on"), 1);
	connect(m_screenAutoScaling, qOverload<int>(&QComboBox::activated), this, &QtProjectWizardContentPreferences::uiAutoScalingChanges);

	// scale factor
	m_screenScaleFactorInfoLabel = new QLabel;
	m_screenScaleFactor = addComboBoxWithWidgets(tr("Scale Factor"),
		tr("<p>Define a screen scale factor for the user interface of the application. "
			"This setting manipulates the environment flag %1 of the Qt framework "
			"(<a href=\"http://doc.qt.io/qt-5/highdpi.html\">http://doc.qt.io/qt-5/highdpi.html</a>). "
			"Choose <b>system</b> to stick to the setting of your current environment.</p>"
			"<p>Changes to this setting require a <b>restart</b> of the application to take effect.</p>"
		).arg(QT_SCALE_FACTOR), {m_screenScaleFactorInfoLabel}, layout, row);
	m_screenScaleFactor->addItem(tr("system"), -1.0);
	m_screenScaleFactor->addItem(tr("25%"), 0.25);
	m_screenScaleFactor->addItem(tr("50%"), 0.5);
	m_screenScaleFactor->addItem(tr("75%"), 0.75);
	m_screenScaleFactor->addItem(tr("100%"), 1.0);
	m_screenScaleFactor->addItem(tr("125%"), 1.25);
	m_screenScaleFactor->addItem(tr("150%"), 1.5);
	m_screenScaleFactor->addItem(tr("175%"), 1.75);
	m_screenScaleFactor->addItem(tr("200%"), 2.0);
	m_screenScaleFactor->addItem(tr("250%"), 2.5);
	m_screenScaleFactor->addItem(tr("300%"), 3.0);
	m_screenScaleFactor->addItem(tr("400%"), 4.0);
	connect(m_screenScaleFactor, qOverload<int>(&QComboBox::activated), this, &QtProjectWizardContentPreferences::uiScaleFactorChanges);

	addGap(layout, row);

	// Controls
	addTitle(tr("CONTROLS"), layout, row);

	// scroll speed
	m_scrollSpeed = addLineEdit(tr("Scroll Speed"),
		tr(
			"<p>Set a multiplier for the in app scroll speed.</p>"
			"<p>A value between 0 and 1 results in slower scrolling while a value higher than 1 "
			"increases scroll speed.</p>"
		), layout, row);

	// graph zooming
	m_graphZooming = addCheckBox(tr("Graph Zoom"),
		tr("Zoom graph on mouse wheel"),
		tr("<p>Enable graph zoom using mouse wheel only, instead of using %1/%2.</p>")
			.arg(QtActions::zoomInWithMouse().shortcut()).arg(QtActions::zoomOutWithMouse().shortcut()),
		layout,
		row);

	addGap(layout, row);

	// output
	addTitle(tr("OUTPUT"), layout, row);

	// logging
	m_loggingEnabled = addCheckBox(tr("Logging"),
		tr("Enable console and file logging"),
		tr("<p>Show logs in the console and save this information in files.</p>"),
		layout,
		row);
	connect(m_loggingEnabled, &QCheckBox::clicked, this, &QtProjectWizardContentPreferences::loggingEnabledChanged);

	m_verboseIndexerLoggingEnabled = addCheckBox(tr("Indexer Logging"),
		tr("Enable verbose indexer logging"),
		tr("<p>Enable additional logs of abstract syntax tree traversal during indexing. This "
			"information can help "
			"tracking down crashes that occur during indexing.</p>"
			"<p><b>Warning</b>: This slows down indexing performance a lot.</p>"),
		layout,
		row);

	m_logPath = new QtLocationPicker(this);
	m_logPath->setPickDirectory(true);
	addLabelAndWidget(tr("Log Directory Path"), m_logPath, layout, row);
	addHelpButton(tr("Log Directory Path"), tr("<p>Log file will be saved to this path.</p>"),
		layout,
		row);
	row++;

	addGap(layout, row);

	// Plugins
	addTitle(tr("PLUGIN"), layout, row);

	// Sourcetrail port
	m_sourcetrailPort = addLineEdit(tr("Sourcetrail Port"),
		tr("<p>Port number that Sourcetrail uses to listen for incoming messages from plugins.</p>"),
		layout,
		row);

	// Sourcetrail port
	m_pluginPort = addLineEdit(tr("Plugin Port"),
		tr("<p>Port number that Sourcetrail uses to sends outgoing messages to plugins.</p>"),
		layout,
		row);

	addGap(layout, row);

	// indexing
	addTitle(tr("INDEXING"), layout, row);

	// indexer threads
	m_threadsInfoLabel = new QLabel;
	utility::setWidgetRetainsSpaceWhenHidden(m_threadsInfoLabel);
	m_threads = addComboBoxWithWidgets(tr("Indexer Threads"),
		0, utility::getIdealThreadCount(), tr("<p>Set the number of threads used to work on indexing your project in parallel.</p>"),
		{m_threadsInfoLabel}, layout, row);
	m_threads->setItemText(0, tr("default"));
	connect(m_threads, qOverload<int>(&QComboBox::activated), this, &QtProjectWizardContentPreferences::indexerThreadsChanges);

	addGap(layout, row);

	addTitle(tr("C/C++"), layout, row);
}

void QtProjectWizardContentPreferences::load()
{
	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();

	QString fontName = QString::fromStdString(appSettings->getFontName());
	m_fontFace->setCurrentText(fontName);
	m_fontFacePlaceHolder->addItem(fontName);
	m_fontFacePlaceHolder->setCurrentText(fontName);

	m_fontSize->setCurrentIndex(appSettings->getFontSize() - appSettings->getFontSizeMin());
	m_tabWidth->setCurrentIndex(appSettings->getCodeTabWidth() - 1);

	m_textEncoding->setCurrentText(QString::fromStdString(appSettings->getTextEncoding()));

	const auto selectColorScheme = [this](QComboBox* comboBox, const FilePath& colorSchemePath)
	{
		for (int i = 0; i < static_cast<int>(m_colorSchemePaths.size()); i++)
		{
			if (colorSchemePath == m_colorSchemePaths[i])
			{
				comboBox->setCurrentIndex(i);
				break;
			}
		}
	};
	selectColorScheme(m_colorSchemes, appSettings->getColorSchemePath(appSettings->getColorSchemeName()));
	selectColorScheme(m_colorSchemesDark, appSettings->getColorSchemePath(appSettings->getColorSchemeNameDark()));
	m_followSystemColorScheme->setChecked(appSettings->getColorSchemeFollowsSystem());

	// Remember the effective scheme so an unsaved live preview can be reverted.
	m_initialColorSchemePath = QtColorSchemeWatcher::resolveColorSchemePath();
	m_colorSchemeModified = false;
	m_colorSchemeSaved = false;

	m_useAnimations->setChecked(appSettings->getUseAnimations());
	m_showBuiltinTypes->setChecked(appSettings->getShowBuiltinTypesInGraph());
	m_hideDeprecated->setChecked(appSettings->getHideDeprecatedInGraph());
	m_showDirectoryInCode->setChecked(appSettings->getShowDirectoryInCodeFileTitle());

	if (m_screenAutoScaling)
	{
		m_screenAutoScaling->setCurrentIndex(
			m_screenAutoScaling->findData(appSettings->getScreenAutoScaling()));
		uiAutoScalingChanges(m_screenAutoScaling->currentIndex());
	}

	if (m_screenScaleFactor)
	{
		m_screenScaleFactor->setCurrentIndex(
			m_screenScaleFactor->findData(appSettings->getScreenScaleFactor()));
		uiScaleFactorChanges(m_screenScaleFactor->currentIndex());
	}

	m_scrollSpeed->setText(QString::number(appSettings->getScrollSpeed(), 'f', 1));
	m_graphZooming->setChecked(appSettings->getControlsGraphZoomOnMouseWheel());

	m_loggingEnabled->setChecked(appSettings->getLoggingEnabled());
	m_verboseIndexerLoggingEnabled->setChecked(appSettings->getVerboseIndexerLoggingEnabled());
	m_verboseIndexerLoggingEnabled->setEnabled(m_loggingEnabled->isChecked());
	if (m_logPath)
	{
		m_logPath->setText(QString::fromStdString(appSettings->getLogDirectoryPath().str()));
	}

	m_sourcetrailPort->setText(QString::number(appSettings->getSourcetrailPort()));
	m_pluginPort->setText(QString::number(appSettings->getPluginPort()));

	m_threads->setCurrentIndex(
		appSettings->getIndexerThreadCount());	  // index and value are the same
	indexerThreadsChanges(m_threads->currentIndex());
}

void QtProjectWizardContentPreferences::save()
{
	std::shared_ptr<ApplicationSettings> appSettings = ApplicationSettings::getInstance();

	appSettings->setFontName(m_fontFace->currentText().toStdString());

	appSettings->setFontSize(m_fontSize->currentIndex() + appSettings->getFontSizeMin());
	appSettings->setCodeTabWidth(m_tabWidth->currentIndex() + 1);

	appSettings->setTextEncoding(m_textEncoding->currentText().toStdString());
	MessageTextEncodingChanged(appSettings->getTextEncoding()).dispatch();
	
	appSettings->setColorSchemeName(
		m_colorSchemePaths[m_colorSchemes->currentIndex()].withoutExtension().fileName());
	appSettings->setColorSchemeNameDark(
		m_colorSchemePaths[m_colorSchemesDark->currentIndex()].withoutExtension().fileName());
	appSettings->setColorSchemeFollowsSystem(m_followSystemColorScheme->isChecked());
	m_colorSchemeSaved = true;

	// Apply the effective scheme (honoring the follow-system setting just saved).
	MessageSwitchColorScheme(QtColorSchemeWatcher::resolveColorSchemePath()).dispatch();

	appSettings->setUseAnimations(m_useAnimations->isChecked());
	appSettings->setShowBuiltinTypesInGraph(m_showBuiltinTypes->isChecked());
	appSettings->setHideDeprecatedInGraph(m_hideDeprecated->isChecked());
	appSettings->setShowDirectoryInCodeFileTitle(m_showDirectoryInCode->isChecked());

	if (m_screenAutoScaling)
	{
		appSettings->setScreenAutoScaling(m_screenAutoScaling->currentData().toInt());
	}

	if (m_screenScaleFactor)
	{
		appSettings->setScreenScaleFactor(m_screenScaleFactor->currentData().toFloat());
	}

	float scrollSpeed = m_scrollSpeed->text().toFloat();
	if (scrollSpeed)
		appSettings->setScrollSpeed(scrollSpeed);

	appSettings->setControlsGraphZoomOnMouseWheel(m_graphZooming->isChecked());

	appSettings->setLoggingEnabled(m_loggingEnabled->isChecked());
	appSettings->setVerboseIndexerLoggingEnabled(m_verboseIndexerLoggingEnabled->isChecked());
	if (m_logPath && m_logPath->getText().toStdString() != appSettings->getLogDirectoryPath().str())
	{
		appSettings->setLogDirectoryPath(FilePath((m_logPath->getText() + '/').toStdString()));
		Logger* logger = LogManager::getInstance()->getLoggerByType("FileLogger");
		if (logger)
		{
			auto *fileLogger = dynamic_cast<FileLogger*>(logger);
			fileLogger->setLogDirectory(appSettings->getLogDirectoryPath());
			fileLogger->setFileName(FileLogger::generateDatedFileName("log"));
		}
	}

	int sourcetrailPort = m_sourcetrailPort->text().toInt();
	if (sourcetrailPort)
		appSettings->setSourcetrailPort(sourcetrailPort);

	int pluginPort = m_pluginPort->text().toInt();
	if (pluginPort)
		appSettings->setPluginPort(pluginPort);

	appSettings->setIndexerThreadCount(m_threads->currentIndex());	  // index and value are the same

	appSettings->save();
}

bool QtProjectWizardContentPreferences::check()
{
	return true;
}

void QtProjectWizardContentPreferences::colorSchemeChanged()
{
	// Preview the scheme that would be active given the current dialog state: the
	// dark scheme when following the system appearance and it is currently dark,
	// otherwise the (day) scheme.
	const bool dark = m_followSystemColorScheme->isChecked() &&
		QtColorSchemeWatcher::systemColorScheme() == Qt::ColorScheme::Dark;
	const int index = (dark ? m_colorSchemesDark : m_colorSchemes)->currentIndex();

	m_colorSchemeModified = true;
	MessageSwitchColorScheme(m_colorSchemePaths[index]).dispatch();
}

void QtProjectWizardContentPreferences::loggingEnabledChanged()
{
	m_verboseIndexerLoggingEnabled->setEnabled(m_loggingEnabled->isChecked());
}

void QtProjectWizardContentPreferences::indexerThreadsChanges(int index)
{
	if (index == 0)
	{
		m_threadsInfoLabel->setText(tr("detected <b>%1</b> threads to be ideal.").arg(utility::getIdealThreadCount()));
		m_threadsInfoLabel->show();
	}
	else
	{
		m_threadsInfoLabel->hide();
	}
}

void QtProjectWizardContentPreferences::uiAutoScalingChanges(int index)
{
	if (index == 0)
	{
		QString autoScale = isQtAutoScreenScaleFactorEnabled() ? tr("on") : tr("off");
		m_screenAutoScalingInfoLabel->setText(tr("detected: <b>%1</b>").arg(autoScale));
		m_screenAutoScalingInfoLabel->show();
	}
	else
	{
		m_screenAutoScalingInfoLabel->hide();
	}
}

void QtProjectWizardContentPreferences::uiScaleFactorChanges(int index)
{
	if (index == 0)
	{
		QString scale = tr("100");
		optional<double> scaleFactor = getQtScaleFactor();
		if (scaleFactor)
		{
			scale = QString::number(int(*scaleFactor * 100));
		}

		m_screenScaleFactorInfoLabel->setText(tr("detected: <b>%1%</b>").arg(scale));
		m_screenScaleFactorInfoLabel->show();
	}
	else
	{
		m_screenScaleFactorInfoLabel->hide();
	}
}

void QtProjectWizardContentPreferences::addTitle(const QString& title, QGridLayout* layout, int& row)
{
	layout->addWidget(createFormTitle(title), row++, QtProjectWizardWindow::FRONT_COL, Qt::AlignLeft);
}

void QtProjectWizardContentPreferences::addLabel(const QString& label, QGridLayout* layout, int row)
{
	layout->addWidget(createFormLabel(label), row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);
}

void QtProjectWizardContentPreferences::addWidget(
	QWidget* widget, QGridLayout* layout, int row, Qt::Alignment widgetAlignment)
{
	layout->addWidget(widget, row, QtProjectWizardWindow::BACK_COL, widgetAlignment);
}

void QtProjectWizardContentPreferences::addLabelAndWidget(
	const QString& label, QWidget* widget, QGridLayout* layout, int row, Qt::Alignment widgetAlignment)
{
	addLabel(label, layout, row);
	addWidget(widget, layout, row, widgetAlignment);
}

void QtProjectWizardContentPreferences::addGap(QGridLayout* layout, int& row)
{
	layout->setRowMinimumHeight(row++, 20);
}

QCheckBox* QtProjectWizardContentPreferences::addCheckBox(
	const QString& label, const QString& text, const QString& helpText, QGridLayout* layout, int& row)
{
	QCheckBox* checkBox = new QCheckBox(text, this);
	addLabelAndWidget(label, checkBox, layout, row, Qt::AlignLeft);

	if (helpText.size())
	{
		addHelpButton(label, helpText, layout, row);
	}

	row++;

	return checkBox;
}

QComboBox* QtProjectWizardContentPreferences::addComboBox(
	const QString& label, const QString& helpText, QGridLayout* layout, int& row)
{
	QComboBox* comboBox = new QComboBox(this);
	addLabelAndWidget(label, comboBox, layout, row, Qt::AlignLeft);

	if (helpText.size())
	{
		addHelpButton(label, helpText, layout, row);
	}

	row++;

	return comboBox;
}

QComboBox* QtProjectWizardContentPreferences::addComboBoxWithWidgets(
	const QString& label,
	const QString& helpText,
	std::vector<QWidget*> widgets,
	QGridLayout* layout,
	int& row)
{
	QComboBox* comboBox = new QComboBox(this);

	QHBoxLayout* hlayout = new QHBoxLayout();
	hlayout->setContentsMargins(0, 0, 0, 0);
	hlayout->addWidget(comboBox);

	for (QWidget* widget: widgets)
	{
		hlayout->addWidget(widget);
	}

	QWidget* container = new QWidget();
	container->setLayout(hlayout);

	addLabelAndWidget(label, container, layout, row, Qt::AlignLeft);

	if (helpText.size())
	{
		addHelpButton(label, helpText, layout, row);
	}

	row++;

	return comboBox;
}

QComboBox* QtProjectWizardContentPreferences::addComboBox(
	const QString& label, int min, int max, const QString& helpText, QGridLayout* layout, int& row)
{
	QComboBox* comboBox = addComboBox(label, helpText, layout, row);

	if (min != max)
	{
		for (int i = min; i <= max; i++)
		{
			comboBox->insertItem(i, QString::number(i));
		}
	}

	return comboBox;
}

QComboBox* QtProjectWizardContentPreferences::addComboBoxWithWidgets(
	const QString& label,
	int min,
	int max,
	const QString& helpText,
	std::vector<QWidget*> widgets,
	QGridLayout* layout,
	int& row)
{
	QComboBox* comboBox = addComboBoxWithWidgets(label, helpText, widgets, layout, row);

	if (min != max)
	{
		for (int i = min; i <= max; i++)
		{
			comboBox->insertItem(i, QString::number(i));
		}
	}

	return comboBox;
}

QLineEdit* QtProjectWizardContentPreferences::addLineEdit(
	const QString& label, const QString& helpText, QGridLayout* layout, int& row)
{
	QLineEdit* lineEdit = new QLineEdit(this);
	lineEdit->setObjectName(QStringLiteral("name"));
	lineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

	addLabelAndWidget(label, lineEdit, layout, row);

	if (helpText.size())
	{
		addHelpButton(label, helpText, layout, row);
	}

	row++;

	return lineEdit;
}
