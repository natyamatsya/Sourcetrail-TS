#include "QtStatusView.h"
#include "UiPost.h"

#include <utility>

#include <QBoxLayout>
#include <QCheckBox>
#include <QFrame>
#include <QPushButton>
#include <QStandardItemModel>

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "ColorScheme.h"
#include "MessageClearStatusView.h"
#include "MessageStatusFilterChanged.h"
#endif
#include "QtDockChrome.h"
#include "QtTable.h"
#include "QtViewWidgetWrapper.h"
#include "utilityQt.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.settings;
#endif

QtStatusView::QtStatusView(ViewLayout* viewLayout): StatusView(viewLayout)
{
	setWidgetWrapper(std::make_shared<QtViewWidgetWrapper>(new QFrame()));

	QWidget* widget = QtViewWidgetWrapper::getWidgetOfView(this);

	QBoxLayout* layout = new QVBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	widget->setLayout(layout);

	m_table = new QtTable(this);
	m_model = new QStandardItemModel(this);
	m_table->setModel(m_model);

	m_model->setColumnCount(2);
	m_table->setColumnWidth(static_cast<int>(STATUSVIEW_COLUMN::TYPE), 100);
	// m_table->setColumnWidth(STATUSVIEW_COLUMN::STATUS, 150);

	QStringList headers;
	headers << QStringLiteral("Type") << QStringLiteral("Message");
	m_model->setHorizontalHeaderLabels(headers);

	layout->addWidget(m_table);

	// The filters and Clear Table: a bar the dock's header shows right of the tabs while it has
	// room for it, else a row under the table.
	QWidget* filterRow{new QWidget()};
	QHBoxLayout* filterRowLayout{new QHBoxLayout(filterRow)};
	filterRowLayout->setContentsMargins(8, 4, 8, 4);
	layout->addWidget(filterRow);

	QWidget* filterBar{new QWidget(filterRow)};
	filterBar->setObjectName(QStringLiteral("filter_bar"));
	QHBoxLayout* filters{new QHBoxLayout(filterBar)};
	filters->setContentsMargins(0, 0, 0, 0);
	filters->setSpacing(4);
	filterRowLayout->addWidget(filterBar);

	const StatusFilter filter = ApplicationSettings::getInstance()->getStatusFilter();
	m_showInfo = createFilterCheckbox(
		QStringLiteral("Info"), filters, filter & std::to_underlying(StatusType::STATUS_INFO));
	m_showErrors = createFilterCheckbox(
		QStringLiteral("Error"), filters, filter & std::to_underlying(StatusType::STATUS_ERROR));

	filters->addStretch();

	QPushButton* clearButton = new QPushButton(QStringLiteral("Clear Table"));
	clearButton->setObjectName(QStringLiteral("screen_button"));
	connect(clearButton, &QPushButton::clicked, [=]() { MessageClearStatusView().dispatch(); });

	filters->addWidget(clearButton);

	QtDockChrome::offerBarToDockHeader(*widget, *filterBar);
}

void QtStatusView::createWidgetWrapper() {}

void QtStatusView::refreshView()
{
	execution::qt::onUi(this, [this]() {
		QWidget* widget = QtViewWidgetWrapper::getWidgetOfView(this);
		utility::setWidgetBackgroundColor(
			widget, ColorScheme::getInstance()->getColor("window/background"));

		QPalette palette(m_showErrors->palette());
		palette.setColor(
			QPalette::WindowText,
			QColor(ColorScheme::getInstance()->getColor("table/text/normal").c_str()));

		m_showErrors->setPalette(palette);
		m_showInfo->setPalette(palette);

		m_table->updateRows();
	});
}

void QtStatusView::clear()
{
	execution::qt::onUi(this, [this]()
	{
		m_model->removeRows(0, m_model->rowCount());
		m_table->showFirstRow();

		m_status.clear();
	});
}

void QtStatusView::addStatus(const std::vector<Status>& status)
{
	execution::qt::onUi(this, [=, this]() {
		for (const Status& s: status)
		{
			const int rowNumber = m_table->getFilledRowCount();
			if (rowNumber < m_model->rowCount())
			{
				m_model->insertRow(rowNumber);
			}

			QString statusType =
				(s.type == StatusType::STATUS_ERROR ? QStringLiteral("ERROR")
													: QStringLiteral("INFO"));
			m_model->setItem(rowNumber, static_cast<int>(STATUSVIEW_COLUMN::TYPE), new QStandardItem(statusType));
			m_model->setItem(
				rowNumber,
				static_cast<int>(STATUSVIEW_COLUMN::STATUS),
				new QStandardItem(QString::fromStdString(s.message)));
			m_model->item(rowNumber, static_cast<int>(STATUSVIEW_COLUMN::STATUS))->setToolTip(QString::fromStdString(s.message));

			if (s.type == StatusType::STATUS_ERROR)
			{
				m_model->item(rowNumber, static_cast<int>(STATUSVIEW_COLUMN::TYPE))
					->setForeground(QBrush(QColor(ColorScheme::getInstance()->getColor("table/text/error").c_str())));
			}
		}

		m_table->updateRows();

		if (!m_table->hasSelection())
		{
			m_table->showLastRow();
		}
	});
}

QCheckBox* QtStatusView::createFilterCheckbox(const QString& name, QBoxLayout* layout, bool checked)
{
	QCheckBox* checkbox = new QCheckBox(name);
	checkbox->setChecked(checked);

	connect(checkbox, &QCheckBox::checkStateChanged, [=, this](int) {
		m_table->selectionModel()->clearSelection();

		const StatusFilter statusMask =
			(m_showInfo->isChecked() ? std::to_underlying(StatusType::STATUS_INFO) : 0) +
			(m_showErrors->isChecked() ? std::to_underlying(StatusType::STATUS_ERROR) : 0);

		MessageStatusFilterChanged(statusMask).dispatch();
	});

	layout->addWidget(checkbox);

	return checkbox;
}
