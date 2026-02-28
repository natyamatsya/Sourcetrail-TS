#include "QtBuildJsonBrowser.h"

#include <algorithm>
#include <unordered_map>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
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
constexpr std::size_t DETAIL_LIST_PREVIEW_LIMIT{12};

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
	targetDetailsPanelLayout->addWidget(m_targetDetailsView);
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
			m_targetDetailsView->setPlainText(formatTargetSelectionDetails(current));
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
		setEntryPoints({});
		populateTargetTree(std::nullopt);
		return;
	}

	const auto snapshot{sourceGroup->getBuildModelSnapshot()};
	if (!snapshot.has_value())
	{
		setEntryPoints({});
		populateTargetTree(std::nullopt);
		return;
	}

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
	const bool hasDetails{index.isValid() && !m_targetDetailsView->toPlainText().isEmpty()};
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

	const QString linkedTargetName{
		m_targetModel->data(index, TARGET_LINK_TARGET_NAME_ROLE).toString()};
	if (linkedTargetName.contains(query, Qt::CaseInsensitive))
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
		targetNameItem->setData(targetDetails.join(QStringLiteral("\n")), TARGET_DETAILS_ROLE);
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
				dependencyGroupDetails.join(QStringLiteral("\n")), TARGET_DETAILS_ROLE);

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
					dependencyDetails.join(QStringLiteral("\n")), TARGET_DETAILS_ROLE);
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
		filesGroupNameItem->setData(filesGroupDetails.join(QStringLiteral("\n")), TARGET_DETAILS_ROLE);

		QList<QStandardItem*> filesGroupRow{};
		filesGroupRow << filesGroupNameItem << filesGroupKindItem << filesGroupInfoItem;
		targetNameItem->appendRow(filesGroupRow);

		for (const BuildFileSnapshot* const file : files)
		{
			auto* fileNameItem{new QStandardItem(QString::fromStdString(file->path.str()))};

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
			fileNameItem->setData(fileDetails.join(QStringLiteral("\n")), TARGET_DETAILS_ROLE);
			fileNameItem->setData(
				QString::fromStdString(file->path.str()), TARGET_PATH_ROLE);

			QList<QStandardItem*> fileRow{};
			fileRow << fileNameItem << fileKindItem << fileInfoItem;
			filesGroupNameItem->appendRow(fileRow);
		}
	}

	m_targetTreeView->collapseAll();
	applyTargetFilter(m_targetSearchEdit->text());
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
