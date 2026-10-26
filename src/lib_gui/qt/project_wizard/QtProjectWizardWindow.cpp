#include "QtProjectWizardWindow.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>

#include "Application.h"
#include "DialogView.h"
#include "QtProjectWizardContent.h"
#include "utilityQt.h"

namespace
{
// A field without a name of its own is called what its row's label says; a button without
// text, "<label>: <tooltip>" (help, pick file, add line, ...).
void nameWidget(QWidget& widget, const QString& name)
{
	if (QAbstractButton* button = qobject_cast<QAbstractButton*>(&widget))
	{
		if (button->text().isEmpty() &&
			(button->accessibleName().isEmpty() || button->accessibleName() == button->toolTip()))
		{
			button->setAccessibleName(button->toolTip().isEmpty() ? name : name + QStringLiteral(": ") + button->toolTip());
		}
		return;
	}
	if (widget.accessibleName().isEmpty() &&
		(qobject_cast<QLineEdit*>(&widget) || qobject_cast<QComboBox*>(&widget) ||
		 qobject_cast<QAbstractSpinBox*>(&widget) || qobject_cast<QAbstractScrollArea*>(&widget)))
	{
		widget.setAccessibleName(name);
	}
}

void nameLayoutItem(QLayoutItem& item, const QString& name);

// Rows of a form: the label in `labelColumn` names the widgets in the columns after it. A row
// without one can hold a form of its own (cross-compilation's architecture, vendor, ...).
void nameRows(QGridLayout& layout, int labelColumn)
{
	for (int row = 0; row < layout.rowCount(); row++)
	{
		const QLayoutItem* labelItem = layout.itemAtPosition(row, labelColumn);
		const QLabel* label = labelItem ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
		const QString name = label
			? QString(label->text()).remove(QLatin1Char('*')).remove(QLatin1Char(':')).simplified()
			: QString();
		for (int column = labelColumn + 1; column < layout.columnCount(); column++)
		{
			if (QLayoutItem* item = layout.itemAtPosition(row, column); item && item != labelItem)
			{
				nameLayoutItem(*item, name);
			}
		}
	}
}

void nameLayoutItem(QLayoutItem& item, const QString& name)
{
	if (QWidget* widget = item.widget(); widget && !name.isEmpty())
	{
		nameWidget(*widget, name);
		for (QWidget* child: widget->findChildren<QWidget*>())
		{
			nameWidget(*child, name);
		}
	}
	else if (QLayout* layout = item.layout())
	{
		if (QGridLayout* grid = qobject_cast<QGridLayout*>(layout))
		{
			nameRows(*grid, 0);	// a form of its own: its labels come first
		}
		for (int i = 0; i < layout->count(); i++)
		{
			nameLayoutItem(*layout->itemAt(i), name);
		}
	}
}
}	 // namespace

QtProjectWizardWindow::QtProjectWizardWindow(QWidget* parent, bool showSeparator)
	: QtWindow(false, parent),  m_showSeparator(showSeparator)
{
}

QSize QtProjectWizardWindow::sizeHint() const
{
	if (m_preferredSize.width())
	{
		return m_preferredSize;
	}
	return QSize(750, 620);
}

QtProjectWizardContent* QtProjectWizardWindow::content() const
{
	return m_content;
}

void QtProjectWizardWindow::setContent(QtProjectWizardContent* content)
{
	m_content = content;
}

void QtProjectWizardWindow::setPreferredSize(QSize size)
{
	m_preferredSize = size;
}

void QtProjectWizardWindow::saveContent()
{
	m_content->save();
}

void QtProjectWizardWindow::loadContent()
{
	m_content->load();
}

void QtProjectWizardWindow::refreshContent()
{
	m_content->refresh();
}

void QtProjectWizardWindow::tell(const QString& text)
{
	ask(text, {"OK"});
}

int QtProjectWizardWindow::ask(const QString& text, const std::vector<std::string>& options)
{
	m_lastMessage = text;
	return Application::getInstance()->getDialogView(DialogView::UseCase::PROJECT_SETUP)->confirm(text.toStdString(), options);
}

void QtProjectWizardWindow::contentChanged() {}

QString QtProjectWizardWindow::lastMessage() const
{
	return m_lastMessage;
}

void QtProjectWizardWindow::clearLastMessage()
{
	m_lastMessage.clear();
}

void QtProjectWizardWindow::populateWindow(QWidget* widget)
{
	QGridLayout* layout = new QGridLayout();
	layout->setContentsMargins(0, 0, 0, 0);

	layout->setColumnStretch(QtProjectWizardWindow::FRONT_COL, 1);
	layout->setColumnStretch(QtProjectWizardWindow::BACK_COL, 3);

	layout->setColumnStretch(HELP_COL, 0);
	layout->setColumnStretch(LINE_COL, 0);

	int row = 0;
	m_content->populate(layout, row);
	nameRows(*layout, FRONT_COL);

	QFrame* separator = new QFrame();
	separator->setFrameShape(QFrame::VLine);

	QPalette palette = separator->palette();
	palette.setColor(QPalette::WindowText, m_showSeparator ? Qt::lightGray : Qt::transparent);
	separator->setPalette(palette);

	layout->addWidget(separator, 0, LINE_COL, -1, 1);

	if (isScrollAble())
	{
		layout->setColumnMinimumWidth(BACK_COL + 1, 10);
	}

	widget->setLayout(layout);
}

void QtProjectWizardWindow::windowReady()
{
	updateTitle(QStringLiteral("NEW SOURCE GROUP"));

	m_content->windowReady();
}

void QtProjectWizardWindow::handleNext()
{
	if (m_content)
	{
		saveContent();

		if (!m_content->check())
		{
			return;
		}
	}

	emit next();
}

void QtProjectWizardWindow::handlePrevious()
{
	if (m_content)
	{
		m_content->save();
	}

	emit previous();
}
