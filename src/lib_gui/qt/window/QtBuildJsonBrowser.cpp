#include "QtBuildJsonBrowser.h"

#include <algorithm>
#include <unordered_map>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "QtJsonTreeModel.h"
#include "SourceGroup.h"

namespace
{

constexpr std::size_t MAX_AUTO_EXPANDED_MATCHES{2000};
constexpr int TARGET_DETAILS_ROLE{Qt::UserRole + 100};
constexpr int TARGET_PATH_ROLE{Qt::UserRole + 101};
constexpr int TARGET_LINK_TARGET_NAME_ROLE{Qt::UserRole + 102};
constexpr int TARGET_NODE_TYPE_ROLE{Qt::UserRole + 103};
constexpr int TARGET_FILE_SUMMARY_ROLE{Qt::UserRole + 104};
constexpr int TARGET_FILE_INCLUDES_ROLE{Qt::UserRole + 105};
constexpr int TARGET_FILE_SYSTEM_INCLUDES_ROLE{Qt::UserRole + 106};
constexpr int TARGET_FILE_FRAMEWORK_PATHS_ROLE{Qt::UserRole + 107};
constexpr int TARGET_FILE_DEFINES_ROLE{Qt::UserRole + 108};
constexpr int TARGET_FILE_FLAGS_ROLE{Qt::UserRole + 109};
constexpr std::size_t DETAIL_LIST_PREVIEW_LIMIT{12};

QStringList toQStringList(const std::vector<std::string>& values)
{
	QStringList result{};
	result.reserve(static_cast<qsizetype>(values.size()));
	for (const std::string& value : values)
		result.push_back(QString::fromStdString(value));
	return result;
}

QStringList toQStringList(const std::vector<FilePath>& values)
{
	QStringList result{};
	result.reserve(static_cast<qsizetype>(values.size()));
	for (const FilePath& value : values)
		result.push_back(QString::fromStdString(value.str()));
	return result;
}

void appendStringListPreview(
	QStringList& lines,
	const QString& title,
	const std::vector<std::string>& values,
	const std::size_t maxEntries = DETAIL_LIST_PREVIEW_LIMIT)
{
	if (values.empty())
		return;

	lines.push_back(title + QStringLiteral(":"));
	const std::size_t shownCount{std::min(values.size(), maxEntries)};
	for (std::size_t i{0}; i < shownCount; ++i)
		lines.push_back(QStringLiteral("  - ") + QString::fromStdString(values[i]));

	if (values.size() <= shownCount)
		return;
	lines.push_back(
		QStringLiteral("  ... (%1 more)")
			.arg(static_cast<qlonglong>(values.size() - shownCount)));
}

void appendPathListPreview(
	QStringList& lines,
	const QString& title,
	const std::vector<FilePath>& values,
	const std::size_t maxEntries = DETAIL_LIST_PREVIEW_LIMIT)
{
	if (values.empty())
		return;

	lines.push_back(title + QStringLiteral(":"));
	const std::size_t shownCount{std::min(values.size(), maxEntries)};
	for (std::size_t i{0}; i < shownCount; ++i)
		lines.push_back(QStringLiteral("  - ") + QString::fromStdString(values[i].str()));

	if (values.size() <= shownCount)
		return;
	lines.push_back(
		QStringLiteral("  ... (%1 more)")
			.arg(static_cast<qlonglong>(values.size() - shownCount)));
}

QString targetKindToString(const BuildTargetKind kind)
{
	switch (kind)
	{
	case BuildTargetKind::EXECUTABLE:
		return QStringLiteral("executable");
	case BuildTargetKind::STATIC_LIBRARY:
		return QStringLiteral("static_library");
	case BuildTargetKind::SHARED_LIBRARY:
		return QStringLiteral("shared_library");
	case BuildTargetKind::MODULE_LIBRARY:
		return QStringLiteral("module_library");
	case BuildTargetKind::OBJECT_LIBRARY:
		return QStringLiteral("object_library");
	case BuildTargetKind::INTERFACE_LIBRARY:
		return QStringLiteral("interface_library");
	case BuildTargetKind::UTILITY:
		return QStringLiteral("utility");
	case BuildTargetKind::CUSTOM:
		return QStringLiteral("custom");
	case BuildTargetKind::UNKNOWN:
		return QStringLiteral("unknown");
	}
	return QStringLiteral("unknown");
}

QString buildLanguageToString(const BuildLanguage language)
{
	switch (language)
	{
	case BuildLanguage::C:
		return QStringLiteral("C");
	case BuildLanguage::CXX:
		return QStringLiteral("C++");
	case BuildLanguage::RUST:
		return QStringLiteral("Rust");
	case BuildLanguage::UNKNOWN:
		return QStringLiteral("Unknown");
	}
	return QStringLiteral("Unknown");
}

}

QtBuildJsonBrowser::QtBuildJsonBrowser(QWidget* parent)
	: QWidget(parent)
	, m_tabWidget{new QTabWidget(this)}
	, m_model{new QtJsonTreeModel(this)}
	, m_treeView{new QTreeView(this)}
	, m_searchEdit{new QLineEdit(this)}
	, m_searchStatusLabel{new QLabel(this)}
	, m_searchProgressBar{new QProgressBar(this)}
	, m_detailsView{new QPlainTextEdit(this)}
	, m_targetTreeView{new QTreeView(this)}
	, m_targetSearchEdit{new QLineEdit(this)}
	, m_targetSearchStatusLabel{new QLabel(this)}
	, m_targetModel{new QStandardItemModel(this)}
	, m_targetDetailsView{new QPlainTextEdit(this)}
	, m_targetDetailsStack{new QStackedWidget(this)}
	, m_fileDetailsSummaryLabel{new QLabel(this)}
	, m_fileDetailsFilterEdit{new QLineEdit(this)}
	, m_fileDetailsTabWidget{new QTabWidget(this)}
	, m_fileIncludesList{new QListWidget(this)}
	, m_fileSystemIncludesList{new QListWidget(this)}
	, m_fileFrameworkPathsList{new QListWidget(this)}
	, m_fileDefinesList{new QListWidget(this)}
	, m_fileFlagsList{new QListWidget(this)}
	, m_targetCopyDetailsButton{new QPushButton(this)}
	, m_targetCopyPathButton{new QPushButton(this)}
	, m_targetOpenPathButton{new QPushButton(this)}
{
	setWindowFlags(
		Qt::Window |
		Qt::WindowCloseButtonHint |
		Qt::WindowMinimizeButtonHint |
		Qt::WindowMaximizeButtonHint);
	setWindowTitle(QStringLiteral("Build Browser"));
	setMinimumSize(700, 450);
	setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
	resize(1100, 700);

	m_treeView->setModel(m_model);
	m_treeView->setAlternatingRowColors(true);
	m_treeView->setUniformRowHeights(true);
	m_treeView->setExpandsOnDoubleClick(true);
	m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_treeView->header()->setStretchLastSection(true);
	m_treeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_treeView->header()->setSectionResizeMode(2, QHeaderView::Stretch);

	m_searchEdit->setClearButtonEnabled(true);
	m_searchEdit->setPlaceholderText(QStringLiteral("Search key/type/value/pointer and expand matches..."));
	m_searchStatusLabel->setText(QStringLiteral("Type to search."));
	m_searchStatusLabel->setMinimumWidth(140);
	m_searchProgressBar->setVisible(false);
	m_searchProgressBar->setMinimumWidth(180);
	m_searchProgressBar->setRange(0, 100);
	m_searchProgressBar->setValue(0);

	m_detailsView->setReadOnly(true);
	m_detailsView->setPlaceholderText(QStringLiteral("Select a JSON node to inspect details."));

	m_targetModel->setHorizontalHeaderLabels(
		{QStringLiteral("Target / File"), QStringLiteral("Kind"), QStringLiteral("Info")});
	m_targetTreeView->setModel(m_targetModel);
	m_targetTreeView->setAlternatingRowColors(true);
	m_targetTreeView->setUniformRowHeights(true);
	m_targetTreeView->setExpandsOnDoubleClick(true);
	m_targetTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_targetTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_targetTreeView->header()->setStretchLastSection(true);
	m_targetTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_targetTreeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_targetTreeView->header()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_targetSearchEdit->setClearButtonEnabled(true);
	m_targetSearchEdit->setPlaceholderText(
		QStringLiteral("Filter targets/files/dependencies..."));
	m_targetSearchStatusLabel->setText(QStringLiteral("Showing all targets."));
	m_targetSearchStatusLabel->setMinimumWidth(180);

	m_targetDetailsView->setReadOnly(true);
	m_targetDetailsView->setPlaceholderText(QStringLiteral("Select a target or file to inspect details."));
	m_fileDetailsSummaryLabel->setWordWrap(true);
	m_fileDetailsSummaryLabel->setText(QStringLiteral("Select a file to browse compile details."));
	m_fileDetailsFilterEdit->setClearButtonEnabled(true);
	m_fileDetailsFilterEdit->setPlaceholderText(
		QStringLiteral("Filter includes/defines/flags in this file..."));
	m_fileIncludesList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_fileSystemIncludesList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_fileFrameworkPathsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_fileDefinesList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_fileFlagsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_fileDetailsTabWidget->addTab(m_fileIncludesList, QStringLiteral("Includes"));
	m_fileDetailsTabWidget->addTab(m_fileSystemIncludesList, QStringLiteral("System Includes"));
	m_fileDetailsTabWidget->addTab(m_fileFrameworkPathsList, QStringLiteral("Framework Paths"));
	m_fileDetailsTabWidget->addTab(m_fileDefinesList, QStringLiteral("Defines"));
	m_fileDetailsTabWidget->addTab(m_fileFlagsList, QStringLiteral("Flags"));

	auto* fileDetailsWidget{new QWidget(this)};
	auto* fileDetailsLayout{new QVBoxLayout()};
	fileDetailsLayout->setContentsMargins(0, 0, 0, 0);
	fileDetailsLayout->addWidget(m_fileDetailsSummaryLabel);
	fileDetailsLayout->addWidget(m_fileDetailsFilterEdit);
	fileDetailsLayout->addWidget(m_fileDetailsTabWidget);
	fileDetailsWidget->setLayout(fileDetailsLayout);

	m_targetDetailsStack->addWidget(m_targetDetailsView);
	m_targetDetailsStack->addWidget(fileDetailsWidget);
	m_targetDetailsStack->setCurrentWidget(m_targetDetailsView);
	m_targetCopyDetailsButton->setText(QStringLiteral("Copy Details"));
	m_targetCopyPathButton->setText(QStringLiteral("Copy Path"));
	m_targetOpenPathButton->setText(QStringLiteral("Open Path"));
	m_targetCopyDetailsButton->setEnabled(false);
	m_targetCopyPathButton->setEnabled(false);
	m_targetOpenPathButton->setEnabled(false);

	auto* splitter{new QSplitter(Qt::Vertical, this)};
	splitter->addWidget(m_treeView);
	splitter->addWidget(m_detailsView);
	splitter->setStretchFactor(0, 4);
	splitter->setStretchFactor(1, 1);

	auto* searchLayout{new QHBoxLayout()};
	searchLayout->setContentsMargins(0, 0, 0, 0);
	searchLayout->addWidget(m_searchEdit, 1);
	searchLayout->addWidget(m_searchStatusLabel);
	searchLayout->addWidget(m_searchProgressBar);

	auto* jsonTab{new QWidget(this)};
	auto* jsonTabLayout{new QVBoxLayout()};
	jsonTabLayout->setContentsMargins(0, 0, 0, 0);
	jsonTabLayout->addLayout(searchLayout);
	jsonTabLayout->addWidget(splitter);
	jsonTab->setLayout(jsonTabLayout);

	auto* targetSearchLayout{new QHBoxLayout()};
	targetSearchLayout->setContentsMargins(0, 0, 0, 0);
	targetSearchLayout->addWidget(m_targetSearchEdit, 1);
	targetSearchLayout->addWidget(m_targetSearchStatusLabel);

	auto* targetActionsLayout{new QHBoxLayout()};
	targetActionsLayout->setContentsMargins(0, 0, 0, 0);
	targetActionsLayout->addWidget(m_targetCopyDetailsButton);
	targetActionsLayout->addWidget(m_targetCopyPathButton);
	targetActionsLayout->addWidget(m_targetOpenPathButton);
	targetActionsLayout->addStretch(1);

	auto* targetDetailsPanel{new QWidget(this)};
	auto* targetDetailsPanelLayout{new QVBoxLayout()};
	targetDetailsPanelLayout->setContentsMargins(0, 0, 0, 0);
	targetDetailsPanelLayout->addLayout(targetActionsLayout);
	targetDetailsPanelLayout->addWidget(m_targetDetailsStack);
	targetDetailsPanel->setLayout(targetDetailsPanelLayout);

	auto* targetSplitter{new QSplitter(Qt::Vertical, this)};
	targetSplitter->addWidget(m_targetTreeView);
	targetSplitter->addWidget(targetDetailsPanel);
	targetSplitter->setStretchFactor(0, 4);
	targetSplitter->setStretchFactor(1, 1);

	auto* targetTab{new QWidget(this)};
	auto* targetTabLayout{new QVBoxLayout()};
	targetTabLayout->setContentsMargins(0, 0, 0, 0);
	targetTabLayout->addLayout(targetSearchLayout);
	targetTabLayout->addWidget(targetSplitter);
	targetTab->setLayout(targetTabLayout);

	m_tabWidget->addTab(targetTab, QStringLiteral("Targets"));
	m_tabWidget->addTab(jsonTab, QStringLiteral("Raw JSON"));
	m_tabWidget->setCurrentWidget(targetTab);

	auto* layout{new QVBoxLayout()};
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_tabWidget);
	setLayout(layout);

	connect(
		m_treeView,
		&QTreeView::expanded,
		this,
		[this](const QModelIndex& index)
		{
			if (!m_model->canFetchMore(index))
				return;
			m_model->fetchMore(index);
		});

	connect(
		m_treeView->selectionModel(),
		&QItemSelectionModel::currentChanged,
		this,
		[this](const QModelIndex& current, const QModelIndex&  /*previous*/)
		{
			m_detailsView->setPlainText(formatSelectionDetails(current));
		});

	connect(
		m_targetTreeView->selectionModel(),
		&QItemSelectionModel::currentChanged,
		this,
		[this](const QModelIndex& current, const QModelIndex&  /*previous*/)
		{
			if (selectTopLevelTargetForDependency(current))
				return;
			updateTargetDetailsView(current);
			updateTargetActionButtons(current);
		});

	connect(
		m_targetSearchEdit,
		&QLineEdit::textChanged,
		this,
		[this](const QString& text)
		{
			applyTargetFilter(text);
		});

	connect(
		m_fileDetailsFilterEdit,
		&QLineEdit::textChanged,
		this,
		[this](const QString&  /*text*/)
		{
			updateFileDetailsView(m_targetTreeView->currentIndex());
		});

	connect(
		m_targetCopyDetailsButton,
		&QPushButton::clicked,
		this,
		[this]()
		{
			const QString details{m_targetDetailsView->toPlainText()};
			if (details.isEmpty())
				return;
			QApplication::clipboard()->setText(details);
		});

	connect(
		m_targetCopyPathButton,
		&QPushButton::clicked,
		this,
		[this]()
		{
			const QString path{currentTargetPath()};
			if (path.isEmpty())
				return;
			QApplication::clipboard()->setText(path);
		});

	connect(
		m_targetOpenPathButton,
		&QPushButton::clicked,
		this,
		[this]()
		{
			const QString path{currentTargetPath()};
			if (path.isEmpty())
				return;

			const QFileInfo fileInfo{path};
			if (!fileInfo.exists())
				return;
			QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
		});

	connect(
		m_searchEdit,
		&QLineEdit::returnPressed,
		this,
		[this]()
		{
			applySearch(m_searchEdit->text());
		});

	connect(
		m_searchEdit,
		&QLineEdit::textChanged,
		this,
		[this](const QString& text)
		{
			m_searchProgressBar->setVisible(false);
			const QString query{text.trimmed()};
			if (query.isEmpty())
			{
				m_searchStatusLabel->setText(QStringLiteral("Type to search."));
				return;
			}
			if (query.size() < 2)
			{
				m_searchStatusLabel->setText(QStringLiteral("Type at least 2 characters, then press Enter."));
				return;
			}
			m_searchStatusLabel->setText(QStringLiteral("Press Enter to search."));
		});
}

void QtBuildJsonBrowser::setEntryPoints(const std::vector<BuildJsonEntryPoint>& entryPoints)
{
	m_model->setEntryPoints(entryPoints);
	m_detailsView->clear();
	m_treeView->collapseAll();
	m_searchProgressBar->setVisible(false);
	m_searchProgressBar->setRange(0, 100);
	m_searchProgressBar->setValue(0);

	for (int row{0}; row < m_model->rowCount(); ++row)
		m_treeView->setExpanded(m_model->index(row, 0), true);

	if (m_searchEdit->text().trimmed().isEmpty())
	{
		m_searchStatusLabel->setText(QStringLiteral("Type to search."));
		return;
	}

	if (m_searchEdit->text().trimmed().size() < 2)
	{
		m_searchStatusLabel->setText(QStringLiteral("Type at least 2 characters, then press Enter."));
		return;
	}

	m_searchStatusLabel->setText(QStringLiteral("Press Enter to search."));
}

void QtBuildJsonBrowser::setSourceGroup(const std::shared_ptr<const SourceGroup>& sourceGroup)
{
	if (!sourceGroup)
	{
		m_projectRootPath.clear();
		setEntryPoints({});
		populateTargetTree(std::nullopt);
		return;
	}

	const auto snapshot{sourceGroup->getBuildModelSnapshot()};
	if (!snapshot.has_value())
	{
		m_projectRootPath.clear();
		setEntryPoints({});
		populateTargetTree(std::nullopt);
		return;
	}

	m_projectRootPath.clear();
	const auto setProjectRootFromPath = [this](const FilePath& path)
	{
		if (path.empty())
			return false;

		const QString normalizedPath{QDir::cleanPath(QString::fromStdString(path.str()))};
		if (normalizedPath.isEmpty())
			return false;
		if (!normalizedPath.startsWith('/'))
			return false;

		m_projectRootPath = normalizedPath;
		return true;
	};

	for (const BuildTargetSnapshot& target : snapshot->targets)
		if (setProjectRootFromPath(target.sourceDir))
			break;
	if (m_projectRootPath.isEmpty())
		for (const BuildFileSnapshot& file : snapshot->files)
			if (setProjectRootFromPath(file.sourceDir))
				break;

	setEntryPoints(snapshot->jsonEntryPoints);
	populateTargetTree(snapshot);
	if (!snapshot->targets.empty())
		m_tabWidget->setCurrentIndex(0);
	else
		m_tabWidget->setCurrentIndex(1);
	if (!snapshot->buildDir.empty())
		setWindowTitle(
			QStringLiteral("Build Browser — ") +
			QString::fromStdString(snapshot->buildDir.str()));
}

QString QtBuildJsonBrowser::formatSelectionDetails(const QModelIndex& index) const
{
	if (!index.isValid())
		return {};

	const QModelIndex keyIndex{index.siblingAtColumn(0)};
	const QModelIndex typeIndex{index.siblingAtColumn(1)};
	const QModelIndex valueIndex{index.siblingAtColumn(2)};

	const QString key{m_model->data(keyIndex, Qt::DisplayRole).toString()};
	const QString type{m_model->data(typeIndex, Qt::DisplayRole).toString()};
	const QString value{m_model->data(valueIndex, Qt::DisplayRole).toString()};
	const QString pointer{m_model->data(keyIndex, QtJsonTreeModel::JSON_POINTER_ROLE).toString()};
	const QString sourceFile{m_model->data(keyIndex, QtJsonTreeModel::SOURCE_FILE_ROLE).toString()};
	const QString referenceFile{m_model->data(keyIndex, QtJsonTreeModel::REFERENCE_FILE_ROLE).toString()};

	QStringList lines{};
	lines.push_back(QStringLiteral("Key: ") + key);
	lines.push_back(QStringLiteral("Type: ") + type);
	lines.push_back(QStringLiteral("Value: ") + value);
	lines.push_back(QStringLiteral("JSON Pointer: ") + pointer);
	if (!sourceFile.isEmpty())
		lines.push_back(QStringLiteral("Source File: ") + sourceFile);
	if (!referenceFile.isEmpty())
		lines.push_back(QStringLiteral("Reference File: ") + referenceFile);
	return lines.join(QStringLiteral("\n"));
}

QString QtBuildJsonBrowser::formatTargetSelectionDetails(const QModelIndex& index) const
{
	if (!index.isValid())
		return {};

	const QModelIndex keyIndex{index.siblingAtColumn(0)};
	const QString details{m_targetModel->data(keyIndex, TARGET_DETAILS_ROLE).toString()};
	if (!details.isEmpty())
		return details;

	const QString key{m_targetModel->data(keyIndex, Qt::DisplayRole).toString()};
	const QString type{m_targetModel->data(index.siblingAtColumn(1), Qt::DisplayRole).toString()};
	const QString value{m_targetModel->data(index.siblingAtColumn(2), Qt::DisplayRole).toString()};

	QStringList lines{};
	lines.push_back(QStringLiteral("Name: ") + key);
	lines.push_back(QStringLiteral("Kind: ") + type);
	lines.push_back(QStringLiteral("Info: ") + value);
	return lines.join(QStringLiteral("\n"));
}

QString QtBuildJsonBrowser::currentTargetPath() const
{
	const QModelIndex currentIndex{m_targetTreeView->currentIndex()};
	if (!currentIndex.isValid())
		return {};

	const QModelIndex keyIndex{currentIndex.siblingAtColumn(0)};
	return m_targetModel->data(keyIndex, TARGET_PATH_ROLE).toString();
}

QString QtBuildJsonBrowser::shortenProjectPath(const QString& path) const
{
	if (path.isEmpty())
		return path;
	if (m_projectRootPath.isEmpty())
		return path;

	const QString normalizedPath{QDir::cleanPath(path)};
	if (normalizedPath == m_projectRootPath)
		return QStringLiteral("<Project>");

	const QString projectPathPrefix{m_projectRootPath + QStringLiteral("/")};
	if (!normalizedPath.startsWith(projectPathPrefix))
		return path;

	return QStringLiteral("<Project>/") + normalizedPath.mid(projectPathPrefix.size());
}

QString QtBuildJsonBrowser::shortenProjectPathInText(const QString& text) const
{
	if (text.isEmpty())
		return text;
	if (m_projectRootPath.isEmpty())
		return text;

	QString shortened{text};
	shortened.replace(
		m_projectRootPath + QStringLiteral("/"), QStringLiteral("<Project>/"));
	shortened.replace(m_projectRootPath, QStringLiteral("<Project>"));
	return shortened;
}

void QtBuildJsonBrowser::updateTargetDetailsView(const QModelIndex& index)
{
	if (!index.isValid())
	{
		m_targetDetailsView->clear();
		m_targetDetailsStack->setCurrentWidget(m_targetDetailsView);
		updateFileDetailsView({});
		return;
	}

	const QModelIndex keyIndex{index.siblingAtColumn(0)};
	const QString nodeType{m_targetModel->data(keyIndex, TARGET_NODE_TYPE_ROLE).toString()};

	m_targetDetailsView->setPlainText(
		shortenProjectPathInText(formatTargetSelectionDetails(index)));
	if (nodeType != QStringLiteral("file"))
	{
		m_targetDetailsStack->setCurrentWidget(m_targetDetailsView);
		updateFileDetailsView({});
		return;
	}

	m_targetDetailsStack->setCurrentIndex(1);
	updateFileDetailsView(index);
}

int QtBuildJsonBrowser::populateFileDetailsList(
	QListWidget* const listWidget,
	const QStringList& values,
	const QString& filterText,
	const QString& noEntriesText,
	const bool shortenPathEntries) const
{
	listWidget->clear();

	int visibleCount{0};
	for (const QString& value : values)
	{
		const QString fullValue{shortenPathEntries ? QDir::cleanPath(value) : value};
		const QString displayValue{shortenPathEntries ? shortenProjectPath(fullValue) : fullValue};
		if (!filterText.isEmpty() &&
			!fullValue.contains(filterText, Qt::CaseInsensitive) &&
			!displayValue.contains(filterText, Qt::CaseInsensitive))
			continue;

		auto* item{new QListWidgetItem(displayValue)};
		if (shortenPathEntries && displayValue != fullValue)
			item->setToolTip(fullValue);
		listWidget->addItem(item);
		++visibleCount;
	}

	if (visibleCount > 0)
		return visibleCount;

	auto* placeholderItem{new QListWidgetItem(filterText.isEmpty()
		? noEntriesText
		: QStringLiteral("No matches for current filter."))};
	placeholderItem->setFlags(Qt::NoItemFlags);
	listWidget->addItem(placeholderItem);
	return 0;
}

void QtBuildJsonBrowser::updateFileDetailsView(const QModelIndex& index)
{
	const QModelIndex keyIndex{index.isValid() ? index.siblingAtColumn(0) : QModelIndex{}};
	const QString nodeType{index.isValid()
		? m_targetModel->data(keyIndex, TARGET_NODE_TYPE_ROLE).toString()
		: QString{}};

	if (nodeType != QStringLiteral("file"))
	{
		m_fileDetailsSummaryLabel->setText(QStringLiteral("Select a file to browse compile details."));
		m_fileDetailsSummaryLabel->setToolTip({});
		m_fileDetailsFilterEdit->setEnabled(false);
		m_fileIncludesList->clear();
		m_fileSystemIncludesList->clear();
		m_fileFrameworkPathsList->clear();
		m_fileDefinesList->clear();
		m_fileFlagsList->clear();
		m_fileDetailsTabWidget->setTabText(0, QStringLiteral("Includes"));
		m_fileDetailsTabWidget->setTabText(1, QStringLiteral("System Includes"));
		m_fileDetailsTabWidget->setTabText(2, QStringLiteral("Framework Paths"));
		m_fileDetailsTabWidget->setTabText(3, QStringLiteral("Defines"));
		m_fileDetailsTabWidget->setTabText(4, QStringLiteral("Flags"));
		return;
	}

	m_fileDetailsFilterEdit->setEnabled(true);
	const QString filterText{m_fileDetailsFilterEdit->text().trimmed()};
	const QString summary{m_targetModel->data(keyIndex, TARGET_FILE_SUMMARY_ROLE).toString()};
	m_fileDetailsSummaryLabel->setText(shortenProjectPathInText(summary));
	m_fileDetailsSummaryLabel->setToolTip(summary);

	const QStringList includes{
		m_targetModel->data(keyIndex, TARGET_FILE_INCLUDES_ROLE).toStringList()};
	const QStringList systemIncludes{
		m_targetModel->data(keyIndex, TARGET_FILE_SYSTEM_INCLUDES_ROLE).toStringList()};
	const QStringList frameworkPaths{
		m_targetModel->data(keyIndex, TARGET_FILE_FRAMEWORK_PATHS_ROLE).toStringList()};
	const QStringList defines{
		m_targetModel->data(keyIndex, TARGET_FILE_DEFINES_ROLE).toStringList()};
	const QStringList flags{
		m_targetModel->data(keyIndex, TARGET_FILE_FLAGS_ROLE).toStringList()};

	const int includeCount{populateFileDetailsList(
		m_fileIncludesList,
		includes,
		filterText,
		QStringLiteral("No include paths."),
		true)};
	const int systemIncludeCount{populateFileDetailsList(
		m_fileSystemIncludesList,
		systemIncludes,
		filterText,
		QStringLiteral("No system include paths."),
		true)};
	const int frameworkCount{populateFileDetailsList(
		m_fileFrameworkPathsList,
		frameworkPaths,
		filterText,
		QStringLiteral("No framework search paths."),
		true)};
	const int defineCount{populateFileDetailsList(
		m_fileDefinesList,
		defines,
		filterText,
		QStringLiteral("No defines."),
		false)};
	const int flagCount{populateFileDetailsList(
		m_fileFlagsList,
		flags,
		filterText,
		QStringLiteral("No flags."),
		false)};

	m_fileDetailsTabWidget->setTabText(
		0, QStringLiteral("Includes (%1)").arg(includeCount));
	m_fileDetailsTabWidget->setTabText(
		1, QStringLiteral("System Includes (%1)").arg(systemIncludeCount));
	m_fileDetailsTabWidget->setTabText(
		2, QStringLiteral("Framework Paths (%1)").arg(frameworkCount));
	m_fileDetailsTabWidget->setTabText(
		3, QStringLiteral("Defines (%1)").arg(defineCount));
	m_fileDetailsTabWidget->setTabText(
		4, QStringLiteral("Flags (%1)").arg(flagCount));
}

QModelIndex QtBuildJsonBrowser::findTopLevelTargetIndexByName(const QString& targetName) const
{
	if (targetName.isEmpty())
		return {};

	for (int row{0}; row < m_targetModel->rowCount(); ++row)
	{
		const QModelIndex candidate{m_targetModel->index(row, 0)};
		if (m_targetModel->data(candidate, Qt::DisplayRole).toString() == targetName)
			return candidate;
	}

	return {};
}

bool QtBuildJsonBrowser::selectTopLevelTargetForDependency(const QModelIndex& index)
{
	if (!index.isValid())
		return false;
	if (m_syncingTargetSelection)
		return false;

	const QModelIndex keyIndex{index.siblingAtColumn(0)};
	const QString linkedTargetName{
		m_targetModel->data(keyIndex, TARGET_LINK_TARGET_NAME_ROLE).toString()};
	if (linkedTargetName.isEmpty())
		return false;

	const QModelIndex linkedTargetIndex{findTopLevelTargetIndexByName(linkedTargetName)};
	if (!linkedTargetIndex.isValid())
		return false;
	if (linkedTargetIndex == keyIndex)
		return false;

	m_syncingTargetSelection = true;
	m_targetTreeView->expand(linkedTargetIndex);
	m_targetTreeView->setCurrentIndex(linkedTargetIndex);
	m_targetTreeView->scrollTo(linkedTargetIndex, QAbstractItemView::PositionAtCenter);
	m_syncingTargetSelection = false;
	return true;
}

void QtBuildJsonBrowser::updateTargetActionButtons(const QModelIndex& index)
{
	const bool hasDetails{index.isValid() && !formatTargetSelectionDetails(index).isEmpty()};
	m_targetCopyDetailsButton->setEnabled(hasDetails);

	const QString path{index.isValid()
		? m_targetModel->data(index.siblingAtColumn(0), TARGET_PATH_ROLE).toString()
		: QString{}};
	const bool hasPath{!path.isEmpty()};
	m_targetCopyPathButton->setEnabled(hasPath);
	m_targetOpenPathButton->setEnabled(hasPath && QFileInfo{path}.exists());
}

bool QtBuildJsonBrowser::targetIndexMatchesQuery(const QModelIndex& index, const QString& query) const
{
	if (!index.isValid())
		return false;
	if (query.isEmpty())
		return true;

	for (int column{0}; column < m_targetModel->columnCount(); ++column)
	{
		const QModelIndex columnIndex{index.siblingAtColumn(column)};
		const QString text{m_targetModel->data(columnIndex, Qt::DisplayRole).toString()};
		if (text.contains(query, Qt::CaseInsensitive))
			return true;
	}

	const QString details{m_targetModel->data(index, TARGET_DETAILS_ROLE).toString()};
	if (details.contains(query, Qt::CaseInsensitive))
		return true;

	const QString path{m_targetModel->data(index, TARGET_PATH_ROLE).toString()};
	if (path.contains(query, Qt::CaseInsensitive))
		return true;
	if (shortenProjectPath(path).contains(query, Qt::CaseInsensitive))
		return true;

	const QString linkedTargetName{
		m_targetModel->data(index, TARGET_LINK_TARGET_NAME_ROLE).toString()};
	if (linkedTargetName.contains(query, Qt::CaseInsensitive))
		return true;

	for (const int role : {TARGET_FILE_INCLUDES_ROLE,
			 TARGET_FILE_SYSTEM_INCLUDES_ROLE,
			 TARGET_FILE_FRAMEWORK_PATHS_ROLE,
			 TARGET_FILE_DEFINES_ROLE,
			 TARGET_FILE_FLAGS_ROLE})
		for (const QString& value : m_targetModel->data(index, role).toStringList())
			if (value.contains(query, Qt::CaseInsensitive) ||
				shortenProjectPath(value).contains(query, Qt::CaseInsensitive))
				return true;

	return false;
}

bool QtBuildJsonBrowser::filterTargetRow(
	const QModelIndex& parent, const int row, const QString& query)
{
	const QModelIndex index{m_targetModel->index(row, 0, parent)};

	bool childMatches{false};
	for (int childRow{0}; childRow < m_targetModel->rowCount(index); ++childRow)
		if (filterTargetRow(index, childRow, query))
			childMatches = true;

	const bool selfMatches{targetIndexMatchesQuery(index, query)};
	const bool isVisible{query.isEmpty() || selfMatches || childMatches};
	m_targetTreeView->setRowHidden(row, parent, !isVisible);
	if (!query.isEmpty() && childMatches)
		m_targetTreeView->setExpanded(index, true);
	return isVisible;
}

void QtBuildJsonBrowser::applyTargetFilter(const QString& text)
{
	const QString query{text.trimmed()};
	const int targetCount{m_targetModel->rowCount()};

	int visibleTargetCount{0};
	for (int row{0}; row < targetCount; ++row)
		if (filterTargetRow({}, row, query))
			++visibleTargetCount;

	if (targetCount == 0)
	{
		m_targetSearchStatusLabel->setText(QStringLiteral("No targets."));
		return;
	}
	if (query.isEmpty())
	{
		m_targetSearchStatusLabel->setText(
			QStringLiteral("Showing %1 targets.").arg(targetCount));
		return;
	}
	if (visibleTargetCount == 0)
	{
		m_targetSearchStatusLabel->setText(
			QStringLiteral("No matches for \"%1\".").arg(query));
		return;
	}

	m_targetSearchStatusLabel->setText(
		QStringLiteral("Filter \"%1\": %2 / %3 targets")
			.arg(query)
			.arg(visibleTargetCount)
			.arg(targetCount));
}

void QtBuildJsonBrowser::populateTargetTree(const std::optional<BuildModelSnapshot>& snapshot)
{
	m_targetModel->clear();
	m_targetModel->setHorizontalHeaderLabels(
		{QStringLiteral("Target / File"), QStringLiteral("Kind"), QStringLiteral("Info")});
	m_targetDetailsView->clear();
	m_targetDetailsStack->setCurrentWidget(m_targetDetailsView);
	m_fileDetailsFilterEdit->clear();
	updateFileDetailsView({});
	updateTargetActionButtons({});

	if (!snapshot.has_value())
	{
		m_targetSearchStatusLabel->setText(QStringLiteral("No targets."));
		return;
	}

	std::unordered_map<std::string, std::vector<const BuildFileSnapshot*>> filesByTarget{};
	for (const auto& file : snapshot->files)
		filesByTarget[file.targetName].push_back(&file);

	std::unordered_map<std::string, const BuildTargetSnapshot*> targetsByName{};
	for (const auto& target : snapshot->targets)
		targetsByName[target.name] = &target;

	std::vector<const BuildTargetSnapshot*> targets{};
	targets.reserve(snapshot->targets.size());
	for (const auto& target : snapshot->targets)
		targets.push_back(&target);
	std::sort(
		targets.begin(),
		targets.end(),
		[](const BuildTargetSnapshot* lhs, const BuildTargetSnapshot* rhs)
		{
			return lhs->name < rhs->name;
		});

	for (const BuildTargetSnapshot* const target : targets)
	{
		auto* targetNameItem{new QStandardItem(QString::fromStdString(target->name))};
		auto* targetKindItem{new QStandardItem(targetKindToString(target->kind))};
		auto* targetInfoItem{
			new QStandardItem(QStringLiteral("%1 files").arg(static_cast<qlonglong>(target->fileCount)))};

		QStringList targetDetails{};
		targetDetails.push_back(QStringLiteral("Target: ") + QString::fromStdString(target->name));
		targetDetails.push_back(QStringLiteral("Kind: ") + targetKindToString(target->kind));
		targetDetails.push_back(
			QStringLiteral("File Count: ") + QString::number(static_cast<qlonglong>(target->fileCount)));
		targetDetails.push_back(
			QStringLiteral("Dependency Count: ") +
			QString::number(static_cast<qlonglong>(target->dependencies.size())));
		appendStringListPreview(targetDetails, QStringLiteral("Dependencies"), target->dependencies);
		if (!target->sourceDir.empty())
			targetDetails.push_back(
				QStringLiteral("Source Dir: ") + QString::fromStdString(target->sourceDir.str()));
		targetNameItem->setData(
			shortenProjectPathInText(targetDetails.join(QStringLiteral("\n"))),
			TARGET_DETAILS_ROLE);
		targetNameItem->setData(QStringLiteral("target"), TARGET_NODE_TYPE_ROLE);
		if (!target->sourceDir.empty())
			targetNameItem->setData(
				QString::fromStdString(target->sourceDir.str()), TARGET_PATH_ROLE);

		QList<QStandardItem*> targetRow{};
		targetRow << targetNameItem << targetKindItem << targetInfoItem;
		m_targetModel->invisibleRootItem()->appendRow(targetRow);

		if (!target->dependencies.empty())
		{
			auto* dependencyGroupNameItem{new QStandardItem(QStringLiteral("Dependencies"))};
			auto* dependencyGroupKindItem{new QStandardItem(QStringLiteral("group"))};
			auto* dependencyGroupInfoItem{new QStandardItem(
				QStringLiteral("%1 targets")
					.arg(static_cast<qlonglong>(target->dependencies.size())))};

			QStringList dependencyGroupDetails{};
			dependencyGroupDetails.push_back(
				QStringLiteral("Target: ") + QString::fromStdString(target->name));
			dependencyGroupDetails.push_back(
				QStringLiteral("Dependency Count: ") +
				QString::number(static_cast<qlonglong>(target->dependencies.size())));
			dependencyGroupNameItem->setData(
				shortenProjectPathInText(dependencyGroupDetails.join(QStringLiteral("\n"))),
				TARGET_DETAILS_ROLE);
			dependencyGroupNameItem->setData(QStringLiteral("group"), TARGET_NODE_TYPE_ROLE);

			QList<QStandardItem*> dependencyGroupRow{};
			dependencyGroupRow << dependencyGroupNameItem << dependencyGroupKindItem
							   << dependencyGroupInfoItem;
			targetNameItem->appendRow(dependencyGroupRow);

			auto dependencyNames{target->dependencies};
			std::sort(dependencyNames.begin(), dependencyNames.end());
			for (const std::string& dependencyName : dependencyNames)
			{
				const auto dependencyTargetIt{targetsByName.find(dependencyName)};
				const BuildTargetSnapshot* dependencyTarget{dependencyTargetIt != targetsByName.end()
					? dependencyTargetIt->second
					: nullptr};

				auto* dependencyNameItem{new QStandardItem(QString::fromStdString(dependencyName))};
				auto* dependencyKindItem{new QStandardItem(
					dependencyTarget ? targetKindToString(dependencyTarget->kind) : QStringLiteral("external"))};
				auto* dependencyInfoItem{new QStandardItem(
					dependencyTarget
						? QStringLiteral("%1 files")
							  .arg(static_cast<qlonglong>(dependencyTarget->fileCount))
						: QStringLiteral("not in snapshot"))};

				QStringList dependencyDetails{};
				dependencyDetails.push_back(
					QStringLiteral("Dependency: ") + QString::fromStdString(dependencyName));
				dependencyDetails.push_back(
					QStringLiteral("Required By: ") + QString::fromStdString(target->name));
				if (dependencyTarget)
				{
					dependencyDetails.push_back(
						QStringLiteral("Kind: ") + targetKindToString(dependencyTarget->kind));
					dependencyDetails.push_back(
						QStringLiteral("File Count: ") +
						QString::number(static_cast<qlonglong>(dependencyTarget->fileCount)));
					dependencyDetails.push_back(QStringLiteral("Action: select row to jump to target"));
					if (!dependencyTarget->sourceDir.empty())
						dependencyDetails.push_back(
							QStringLiteral("Source Dir: ") +
							QString::fromStdString(dependencyTarget->sourceDir.str()));
				}
				else
					dependencyDetails.push_back(QStringLiteral("Kind: external or filtered target"));
				dependencyNameItem->setData(
					shortenProjectPathInText(dependencyDetails.join(QStringLiteral("\n"))),
					TARGET_DETAILS_ROLE);
				dependencyNameItem->setData(QStringLiteral("dependency"), TARGET_NODE_TYPE_ROLE);
				if (dependencyTarget)
					dependencyNameItem->setData(
						QString::fromStdString(dependencyTarget->name),
						TARGET_LINK_TARGET_NAME_ROLE);
				if (dependencyTarget && !dependencyTarget->sourceDir.empty())
					dependencyNameItem->setData(
						QString::fromStdString(dependencyTarget->sourceDir.str()), TARGET_PATH_ROLE);

				QList<QStandardItem*> dependencyRow{};
				dependencyRow << dependencyNameItem << dependencyKindItem << dependencyInfoItem;
				dependencyGroupNameItem->appendRow(dependencyRow);
			}
		}

		const auto filesIt{filesByTarget.find(target->name)};
		if (filesIt == filesByTarget.end())
			continue;

		auto files{filesIt->second};
		std::sort(
			files.begin(),
			files.end(),
			[](const BuildFileSnapshot* lhs, const BuildFileSnapshot* rhs)
			{
				return lhs->path.str() < rhs->path.str();
			});

		auto* filesGroupNameItem{new QStandardItem(QStringLiteral("Files"))};
		auto* filesGroupKindItem{new QStandardItem(QStringLiteral("group"))};
		auto* filesGroupInfoItem{
			new QStandardItem(QStringLiteral("%1 files").arg(static_cast<qlonglong>(files.size())))};

		QStringList filesGroupDetails{};
		filesGroupDetails.push_back(QStringLiteral("Target: ") + QString::fromStdString(target->name));
		filesGroupDetails.push_back(
			QStringLiteral("File Count: ") + QString::number(static_cast<qlonglong>(files.size())));
		filesGroupNameItem->setData(
			shortenProjectPathInText(filesGroupDetails.join(QStringLiteral("\n"))),
			TARGET_DETAILS_ROLE);
		filesGroupNameItem->setData(QStringLiteral("group"), TARGET_NODE_TYPE_ROLE);

		QList<QStandardItem*> filesGroupRow{};
		filesGroupRow << filesGroupNameItem << filesGroupKindItem << filesGroupInfoItem;
		targetNameItem->appendRow(filesGroupRow);

		for (const BuildFileSnapshot* const file : files)
		{
			const QString fullFilePath{QDir::cleanPath(QString::fromStdString(file->path.str()))};
			const QString shortFilePath{shortenProjectPath(fullFilePath)};
			auto* fileNameItem{new QStandardItem(shortFilePath)};
			if (shortFilePath != fullFilePath)
				fileNameItem->setToolTip(fullFilePath);

			QString fileKind{file->isGenerated ? QStringLiteral("generated") : QStringLiteral("source")};
			if (file->compileGroup.has_value())
				fileKind += QStringLiteral(" / ") + buildLanguageToString(file->compileGroup->language);
			auto* fileKindItem{new QStandardItem(fileKind)};
			auto* fileInfoItem{new QStandardItem(QString::fromStdString(file->targetType))};

			QStringList fileDetails{};
			fileDetails.push_back(QStringLiteral("File: ") + QString::fromStdString(file->path.str()));
			fileDetails.push_back(QStringLiteral("Target: ") + QString::fromStdString(file->targetName));
			fileDetails.push_back(
				QStringLiteral("Target Type: ") + QString::fromStdString(file->targetType));
			fileDetails.push_back(
				QStringLiteral("Generated: ") +
				(file->isGenerated ? QStringLiteral("yes") : QStringLiteral("no")));
			if (!file->sourceDir.empty())
				fileDetails.push_back(
					QStringLiteral("Source Dir: ") + QString::fromStdString(file->sourceDir.str()));
			if (file->compileGroup.has_value())
			{
				const BuildCompileGroupSnapshot& compileGroup{*file->compileGroup};
				fileDetails.push_back(
					QStringLiteral("Language: ") + buildLanguageToString(compileGroup.language));
				if (!compileGroup.compilerPath.empty())
					fileDetails.push_back(
						QStringLiteral("Compiler: ") + QString::fromStdString(compileGroup.compilerPath));
				if (!compileGroup.sysroot.empty())
					fileDetails.push_back(
						QStringLiteral("Sysroot: ") + QString::fromStdString(compileGroup.sysroot.str()));
				fileDetails.push_back(
					QStringLiteral("Includes: ") +
					QString::number(static_cast<qlonglong>(compileGroup.includes.size())));
				appendPathListPreview(
					fileDetails, QStringLiteral("Include Paths"), compileGroup.includes);
				fileDetails.push_back(
					QStringLiteral("System Includes: ") +
					QString::number(static_cast<qlonglong>(compileGroup.systemIncludes.size())));
				appendPathListPreview(
					fileDetails,
					QStringLiteral("System Include Paths"),
					compileGroup.systemIncludes);
				fileDetails.push_back(
					QStringLiteral("Framework Search Paths: ") +
					QString::number(static_cast<qlonglong>(compileGroup.frameworkSearchPaths.size())));
				appendPathListPreview(
					fileDetails,
					QStringLiteral("Framework Search Paths"),
					compileGroup.frameworkSearchPaths);
				fileDetails.push_back(
					QStringLiteral("Defines: ") +
					QString::number(static_cast<qlonglong>(compileGroup.defines.size())));
				appendStringListPreview(
					fileDetails, QStringLiteral("Definitions"), compileGroup.defines);
				fileDetails.push_back(
					QStringLiteral("Flags: ") +
					QString::number(static_cast<qlonglong>(compileGroup.flags.size())));
				appendStringListPreview(
					fileDetails, QStringLiteral("Flags Preview"), compileGroup.flags);
			}

			QStringList fileSummary{};
			fileSummary.push_back(QStringLiteral("File: ") + QString::fromStdString(file->path.str()));
			fileSummary.push_back(QStringLiteral("Target: ") + QString::fromStdString(file->targetName));
			fileSummary.push_back(
				QStringLiteral("Target Type: ") + QString::fromStdString(file->targetType));
			if (!file->sourceDir.empty())
				fileSummary.push_back(
					QStringLiteral("Source Dir: ") + QString::fromStdString(file->sourceDir.str()));
			if (file->compileGroup.has_value())
			{
				const BuildCompileGroupSnapshot& compileGroup{*file->compileGroup};
				fileSummary.push_back(
					QStringLiteral("Language: ") + buildLanguageToString(compileGroup.language));
				if (!compileGroup.compilerPath.empty())
					fileSummary.push_back(
						QStringLiteral("Compiler: ") + QString::fromStdString(compileGroup.compilerPath));
				if (!compileGroup.sysroot.empty())
					fileSummary.push_back(
						QStringLiteral("Sysroot: ") + QString::fromStdString(compileGroup.sysroot.str()));
				fileSummary.push_back(
					QStringLiteral("Includes: %1")
						.arg(static_cast<qlonglong>(compileGroup.includes.size())));
				fileSummary.push_back(
					QStringLiteral("System Includes: %1")
						.arg(static_cast<qlonglong>(compileGroup.systemIncludes.size())));
				fileSummary.push_back(
					QStringLiteral("Framework Paths: %1")
						.arg(static_cast<qlonglong>(compileGroup.frameworkSearchPaths.size())));
				fileSummary.push_back(
					QStringLiteral("Defines: %1")
						.arg(static_cast<qlonglong>(compileGroup.defines.size())));
				fileSummary.push_back(
					QStringLiteral("Flags: %1")
						.arg(static_cast<qlonglong>(compileGroup.flags.size())));
			}
			else
				fileSummary.push_back(QStringLiteral("No compile group metadata available."));

			fileNameItem->setData(
				shortenProjectPathInText(fileDetails.join(QStringLiteral("\n"))),
				TARGET_DETAILS_ROLE);
			fileNameItem->setData(QStringLiteral("file"), TARGET_NODE_TYPE_ROLE);
			fileNameItem->setData(fileSummary.join(QStringLiteral("\n")), TARGET_FILE_SUMMARY_ROLE);
			if (file->compileGroup.has_value())
			{
				const BuildCompileGroupSnapshot& compileGroup{*file->compileGroup};
				fileNameItem->setData(
					toQStringList(compileGroup.includes), TARGET_FILE_INCLUDES_ROLE);
				fileNameItem->setData(
					toQStringList(compileGroup.systemIncludes), TARGET_FILE_SYSTEM_INCLUDES_ROLE);
				fileNameItem->setData(
					toQStringList(compileGroup.frameworkSearchPaths), TARGET_FILE_FRAMEWORK_PATHS_ROLE);
				fileNameItem->setData(
					toQStringList(compileGroup.defines), TARGET_FILE_DEFINES_ROLE);
				fileNameItem->setData(
					toQStringList(compileGroup.flags), TARGET_FILE_FLAGS_ROLE);
			}
			else
			{
				fileNameItem->setData(QStringList{}, TARGET_FILE_INCLUDES_ROLE);
				fileNameItem->setData(QStringList{}, TARGET_FILE_SYSTEM_INCLUDES_ROLE);
				fileNameItem->setData(QStringList{}, TARGET_FILE_FRAMEWORK_PATHS_ROLE);
				fileNameItem->setData(QStringList{}, TARGET_FILE_DEFINES_ROLE);
				fileNameItem->setData(QStringList{}, TARGET_FILE_FLAGS_ROLE);
			}
			fileNameItem->setData(fullFilePath, TARGET_PATH_ROLE);

			QList<QStandardItem*> fileRow{};
			fileRow << fileNameItem << fileKindItem << fileInfoItem;
			filesGroupNameItem->appendRow(fileRow);
		}
	}

	m_targetTreeView->collapseAll();
	applyTargetFilter(m_targetSearchEdit->text());
	updateTargetDetailsView(m_targetTreeView->currentIndex());
}

void QtBuildJsonBrowser::applySearch(const QString& text)
{
	const QString query{text.trimmed()};
	if (query.isEmpty())
	{
		m_searchStatusLabel->setText(QStringLiteral("Type to search."));
		m_searchProgressBar->setVisible(false);
		return;
	}
	if (query.size() < 2)
	{
		m_searchStatusLabel->setText(QStringLiteral("Type at least 2 characters."));
		m_searchProgressBar->setVisible(false);
		return;
	}

	m_searchEdit->setEnabled(false);
	m_searchProgressBar->setVisible(true);
	m_searchProgressBar->setRange(0, 0);
	m_searchProgressBar->setFormat(QStringLiteral("Loading..."));
	m_searchStatusLabel->setText(QStringLiteral("Loading nodes..."));
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	std::size_t loadedNodes{0};
	fetchAllChildren({}, loadedNodes);

	const std::size_t totalNodes{countLoadedNodes({})};
	const int progressMax{totalNodes > 0 ? static_cast<int>(totalNodes) : 1};
	m_searchProgressBar->setRange(0, progressMax);
	m_searchProgressBar->setValue(0);
	m_searchProgressBar->setFormat(QStringLiteral("%p%"));
	m_searchStatusLabel->setText(QStringLiteral("Searching..."));
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	std::vector<QModelIndex> matches{};
	std::size_t visitedNodes{0};
	collectMatches({}, query, matches, visitedNodes, totalNodes);
	m_searchEdit->setEnabled(true);
	if (matches.empty())
	{
		m_searchProgressBar->setValue(progressMax);
		m_searchStatusLabel->setText(QStringLiteral("No matches."));
		return;
	}

	const std::size_t expandedMatchCount{std::min(matches.size(), MAX_AUTO_EXPANDED_MATCHES)};
	const int expansionMax{expandedMatchCount > 0 ? static_cast<int>(expandedMatchCount) : 1};
	m_searchProgressBar->setRange(0, expansionMax);
	m_searchProgressBar->setValue(0);
	m_searchProgressBar->setFormat(QStringLiteral("Expanding %v/%m"));
	m_searchStatusLabel->setText(QStringLiteral("Expanding matches..."));
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	m_treeView->setUpdatesEnabled(false);
	m_treeView->collapseAll();
	for (std::size_t i{0}; i < expandedMatchCount; ++i)
	{
		expandAncestors(matches[i]);
		m_searchProgressBar->setValue(static_cast<int>(i + 1));
		if ((i + 1) % 250 == 0)
			QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	}
	m_treeView->setUpdatesEnabled(true);

	const QModelIndex firstMatch{matches.front()};
	m_treeView->setCurrentIndex(firstMatch);
	m_treeView->scrollTo(firstMatch, QAbstractItemView::PositionAtCenter);
	m_searchProgressBar->setRange(0, 100);
	m_searchProgressBar->setValue(100);
	m_searchProgressBar->setFormat(QStringLiteral("%p%"));
	if (matches.size() > expandedMatchCount)
	{
		m_searchStatusLabel->setText(
			QStringLiteral("%1 matches (expanded first %2, refine query)")
				.arg(matches.size())
				.arg(static_cast<qlonglong>(expandedMatchCount)));
		return;
	}
	m_searchStatusLabel->setText(QStringLiteral("%1 matches").arg(matches.size()));
}

void QtBuildJsonBrowser::fetchAllChildren(const QModelIndex& parent, std::size_t& visitedNodes)
{
	if (m_model->canFetchMore(parent))
		m_model->fetchMore(parent);

	for (int row{0}; row < m_model->rowCount(parent); ++row)
	{
		++visitedNodes;
		if (visitedNodes % 250 == 0)
			QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		fetchAllChildren(m_model->index(row, 0, parent), visitedNodes);
	}
}

std::size_t QtBuildJsonBrowser::countLoadedNodes(const QModelIndex& parent) const
{
	std::size_t count{0};
	for (int row{0}; row < m_model->rowCount(parent); ++row)
	{
		const QModelIndex index{m_model->index(row, 0, parent)};
		++count;
		count += countLoadedNodes(index);
	}
	return count;
}

void QtBuildJsonBrowser::collectMatches(
	const QModelIndex& parent,
	const QString& query,
	std::vector<QModelIndex>& matches,
	std::size_t& visitedNodes,
	const std::size_t totalNodes)
{
	for (int row{0}; row < m_model->rowCount(parent); ++row)
	{
		const QModelIndex index{m_model->index(row, 0, parent)};
		++visitedNodes;
		if (totalNodes > 0)
			m_searchProgressBar->setValue(static_cast<int>(visitedNodes > totalNodes ? totalNodes : visitedNodes));
		if (visitedNodes % 250 == 0)
		{
			m_searchStatusLabel->setText(
				QStringLiteral("Searching... %1 / %2")
					.arg(static_cast<qlonglong>(visitedNodes))
					.arg(static_cast<qlonglong>(totalNodes)));
			QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		}
		if (indexMatchesQuery(index, query))
			matches.push_back(index);
		collectMatches(index, query, matches, visitedNodes, totalNodes);
	}
}

bool QtBuildJsonBrowser::indexMatchesQuery(const QModelIndex& index, const QString& query) const
{
	if (!index.isValid())
		return false;

	const QModelIndex keyIndex{index.siblingAtColumn(0)};
	const QModelIndex typeIndex{index.siblingAtColumn(1)};
	const QModelIndex valueIndex{index.siblingAtColumn(2)};

	const QString key{m_model->data(keyIndex, Qt::DisplayRole).toString()};
	if (key.contains(query, Qt::CaseInsensitive))
		return true;

	const QString type{m_model->data(typeIndex, Qt::DisplayRole).toString()};
	if (type.contains(query, Qt::CaseInsensitive))
		return true;

	const QString value{m_model->data(valueIndex, Qt::DisplayRole).toString()};
	if (value.contains(query, Qt::CaseInsensitive))
		return true;

	const QString pointer{m_model->data(keyIndex, QtJsonTreeModel::JSON_POINTER_ROLE).toString()};
	if (pointer.contains(query, Qt::CaseInsensitive))
		return true;

	const QString sourceFile{m_model->data(keyIndex, QtJsonTreeModel::SOURCE_FILE_ROLE).toString()};
	if (sourceFile.contains(query, Qt::CaseInsensitive))
		return true;

	const QString referenceFile{m_model->data(keyIndex, QtJsonTreeModel::REFERENCE_FILE_ROLE).toString()};
	if (referenceFile.contains(query, Qt::CaseInsensitive))
		return true;

	return false;
}

void QtBuildJsonBrowser::expandAncestors(const QModelIndex& index)
{
	QModelIndex current{index};
	while (current.isValid())
	{
		if (!m_treeView->isExpanded(current))
			m_treeView->expand(current);
		current = current.parent();
	}
}
