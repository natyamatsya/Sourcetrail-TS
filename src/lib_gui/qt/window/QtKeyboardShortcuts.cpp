#include "QtKeyboardShortcuts.h"

#include "QtActions.h"
#include "QtResources.h"
#include "utilityQt.h"

#include <QHeaderView>
#include <QLabel>

using namespace utility;

static void addShortcuts(QtShortcutTable* table, const std::vector<QtActions::Info> &shortcuts)
{
	table->setRowCount(static_cast<int>(shortcuts.size()));

	for (size_t row = 0; row < shortcuts.size(); ++row)
	{
		table->setItem(static_cast<int>(row), 0, new QTableWidgetItem(shortcuts[row].name()));
		table->setItem(static_cast<int>(row), 1, new QTableWidgetItem(shortcuts[row].shortcuts()));
	}
	table->updateSize();
}

QtShortcutTable::QtShortcutTable(QWidget* parent)
	: QTableWidget(parent)
{
}

void QtShortcutTable::updateSize()
{
	int height = rowCount() * rowHeight(0) + horizontalHeader()->height() + 2 * frameWidth() + 8;
	setMinimumHeight(height);
	setMaximumHeight(height);
}

void QtShortcutTable::wheelEvent(QWheelEvent* event)
{
	event->ignore();
}


QtKeyboardShortcuts::QtKeyboardShortcuts(QWidget* parent)
	: QtWindow(false, parent)
{
	setWindowTitle(tr("Keyboard Shortcuts"));
	setScrollAble(true);
}

QtKeyboardShortcuts::~QtKeyboardShortcuts() = default;

QSize QtKeyboardShortcuts::sizeHint() const
{
	return QSize(666, 666);
}

void QtKeyboardShortcuts::populateWindow(QWidget* widget)
{
	QVBoxLayout* layout = new QVBoxLayout(widget);

	QLabel* generalLabel = new QLabel(this);
	generalLabel->setObjectName(QStringLiteral("general_label"));
	generalLabel->setText(tr("General Shortcuts"));
	layout->addWidget(generalLabel);

	layout->addWidget(createGeneralShortcutsTable());

	layout->addSpacing(20);

	QLabel* codeLabel = new QLabel(this);
	codeLabel->setObjectName(QStringLiteral("code_label"));
	codeLabel->setText(tr("Code View Shortcuts"));
	layout->addWidget(codeLabel);

	layout->addWidget(createCodeViewShortcutsTable());

	layout->addSpacing(20);

	QLabel* graphLabel = new QLabel(this);
	graphLabel->setObjectName(QStringLiteral("graph_label"));
	graphLabel->setText(tr("Graph View Shortcuts"));
	layout->addWidget(graphLabel);

	layout->addWidget(createGraphViewShortcutsTable());

	widget->setLayout(layout);

	widget->setStyleSheet(QtResources::loadStyleSheet(QtResources::KEYBOARD_SHORTCUTS_CSS));
}

void QtKeyboardShortcuts::windowReady()
{
	updateTitle(tr("Keyboard Shortcuts"));
	updateCloseButton(tr("Close"));

	setNextVisible(false);
	setPreviousVisible(false);
}


QtShortcutTable* QtKeyboardShortcuts::createTableWidget(const std::string& objectName)
{
	QtShortcutTable* table = new QtShortcutTable(this);
	table->setObjectName(objectName.c_str());

	table->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
	table->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
	table->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

	table->setShowGrid(true);
	table->setAlternatingRowColors(true);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeMode::ResizeToContents);
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->hide();

	table->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

	table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	table->setColumnCount(2);
	table->setHorizontalHeaderItem(0, new QTableWidgetItem(tr("Command")));
	table->setHorizontalHeaderItem(1, new QTableWidgetItem(tr("Shortcut")));

	return table;
}


QTableWidget* QtKeyboardShortcuts::createGeneralShortcutsTable()
{
	QtShortcutTable* table = createTableWidget("table_general");

	addShortcuts(table, {
		QtActions::switchGraphCodeFocus(),
		QtActions::largerFont(),
		QtActions::smallerFont(),
		QtActions::resetFontSize(),
		QtActions::back(),
		QtActions::forward(),
		QtActions::refresh(),
		QtActions::fullRefresh(),
		QtActions::findSymbol(),
		QtActions::findText(),
		QtActions::findOnScreen(),
		QtActions::newProject(),
		QtActions::newEmptyProject(),
		QtActions::openProject(),
		QtActions::closeTab(),
		QtActions::refreshUI(),
		QtActions::toOverview(),
		QtActions::preferences(),
		QtActions::bookmarkActiveSymbol(),
		QtActions::bookmarkManager()
	});
	return table;
}

QTableWidget* QtKeyboardShortcuts::createCodeViewShortcutsTable()
{
	QtShortcutTable* table = createTableWidget("table_code");

	addShortcuts(table, {
		QtActions::moveCodeFocus(),
		QtActions::moveReferenceFocus(),
		QtActions::activateLocation(),
		QtActions::activateLocationNewTab(),
		QtActions::nextReference(),
		QtActions::previousReference(),
		QtActions::nextLocalReference(),
		QtActions::previousLocalReference(),
		QtActions::scrollCodeArea()
	});
	return table;
}

QTableWidget* QtKeyboardShortcuts::createGraphViewShortcutsTable()
{
	QtShortcutTable* table = createTableWidget("table_graph");

	addShortcuts(table, {
		QtActions::moveNodeFocus(),
		QtActions::moveEdgeFocus(),
		QtActions::activateNodeEdge(),
		QtActions::expandOrCollapseNode(),
		QtActions::activateNodeNewTab(),
		QtActions::scrollGraphArea(),
		QtActions::zoomIn(),
		QtActions::zoomOut(),
		QtActions::resetZoom(),
		QtActions::customTrailDialog()
	});
	return table;
}
