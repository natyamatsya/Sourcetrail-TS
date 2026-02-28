#include "QtBuildJsonBrowser.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

#include "QtJsonTreeModel.h"
#include "SourceGroup.h"

QtBuildJsonBrowser::QtBuildJsonBrowser(QWidget* parent)
	: QWidget(parent)
	, m_model{new QtJsonTreeModel(this)}
	, m_treeView{new QTreeView(this)}
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

	m_detailsView->setReadOnly(true);
	m_detailsView->setPlaceholderText(QStringLiteral("Select a JSON node to inspect details."));

	auto* splitter{new QSplitter(Qt::Vertical, this)};
	splitter->addWidget(m_treeView);
	splitter->addWidget(m_detailsView);
	splitter->setStretchFactor(0, 4);
	splitter->setStretchFactor(1, 1);

	auto* layout{new QVBoxLayout()};
	layout->setContentsMargins(0, 0, 0, 0);
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
}

void QtBuildJsonBrowser::setEntryPoints(const std::vector<BuildJsonEntryPoint>& entryPoints)
{
	m_model->setEntryPoints(entryPoints);
	m_detailsView->clear();

	for (int row{0}; row < m_model->rowCount(); ++row)
		m_treeView->setExpanded(m_model->index(row, 0), true);
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
