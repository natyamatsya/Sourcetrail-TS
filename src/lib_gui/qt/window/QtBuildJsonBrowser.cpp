#include "QtBuildJsonBrowser.h"

#include <algorithm>

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

#include "QtJsonTreeModel.h"
#include "SourceGroup.h"

namespace
{

constexpr std::size_t MAX_AUTO_EXPANDED_MATCHES{2000};

}

QtBuildJsonBrowser::QtBuildJsonBrowser(QWidget* parent)
	: QWidget(parent)
	, m_model{new QtJsonTreeModel(this)}
	, m_treeView{new QTreeView(this)}
	, m_searchEdit{new QLineEdit(this)}
	, m_searchStatusLabel{new QLabel(this)}
	, m_searchProgressBar{new QProgressBar(this)}
	, m_detailsView{new QPlainTextEdit(this)}
{
	setWindowFlags(
		Qt::Window |
		Qt::WindowCloseButtonHint |
		Qt::WindowMinimizeButtonHint |
		Qt::WindowMaximizeButtonHint);
	setWindowTitle(QStringLiteral("Build JSON Browser"));
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

	auto* layout{new QVBoxLayout()};
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(searchLayout);
	layout->addWidget(splitter);
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
		return;
	}

	const auto snapshot{sourceGroup->getBuildModelSnapshot()};
	if (!snapshot.has_value())
	{
		setEntryPoints({});
		return;
	}

	setEntryPoints(snapshot->jsonEntryPoints);
	if (!snapshot->buildDir.empty())
		setWindowTitle(
			QStringLiteral("Build JSON Browser — ") +
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
