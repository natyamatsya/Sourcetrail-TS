#include "QtBookmarkCategory.h"

#include "Application.h"
#include "DialogView.h"
#include "utilityQt.h"
#include "QtResources.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>


QtBookmarkCategory::QtBookmarkCategory(ControllerProxy<BookmarkController>* controllerProxy)
	: m_controllerProxy(controllerProxy), m_id(0)
{
	setObjectName(QStringLiteral("bookmark_category"));

	QHBoxLayout* layout = new QHBoxLayout();
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setAlignment(Qt::AlignTop);
	setLayout(layout);

	m_expandButton = new QPushButton();
	m_expandButton->setObjectName(QStringLiteral("category_expand_button"));
	m_expandButton->setToolTip(tr("Show/Hide bookmarks in this category"));
	m_expandButton->setAccessibleName(tr("Show/Hide Bookmarks"));
	m_expandButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	m_expandButton->setIcon(QPixmap(QString::fromUtf8(QtResources::BOOKMARK_VIEW_ARROW_DOWN)));
	m_expandButton->setIconSize(QSize(8, 8));
	layout->addWidget(m_expandButton);

	connect(m_expandButton, &QPushButton::clicked, this, &QtBookmarkCategory::expandClicked);

	m_name = new QLabel();
	m_name->setObjectName(QStringLiteral("category_name"));
	layout->addWidget(m_name);

	layout->addStretch();

	m_deleteButton = new QPushButton();
	m_deleteButton->setObjectName(QStringLiteral("category_delete_button"));
	m_deleteButton->setToolTip(tr("Delete this Bookmark Category and the containing Bookmarks"));
	m_deleteButton->setAccessibleName(tr("Delete Category"));
	m_deleteButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	m_deleteButton->setIconSize(QSize(20, 20));
	m_deleteButton->setIcon(QPixmap(QString::fromUtf8(QtResources::BOOKMARK_VIEW_BOOKMARK_DELETE_ICON)));
	utility::setWidgetRetainsSpaceWhenHidden(m_deleteButton);
	m_deleteButton->hide();
	layout->addWidget(m_deleteButton);

	connect(m_deleteButton, &QPushButton::clicked, this, &QtBookmarkCategory::deleteClicked);
}

QtBookmarkCategory::~QtBookmarkCategory() = default;

void QtBookmarkCategory::setName(const std::string& name)
{
	m_name->setText(QString::fromStdString(name));
}

std::string QtBookmarkCategory::getName() const
{
	return m_name->text().toStdString();
}

void QtBookmarkCategory::setId(const Id id)
{
	m_id = id;
}

Id QtBookmarkCategory::getId() const
{
	return m_id;
}

void QtBookmarkCategory::setTreeWidgetItem(QTreeWidgetItem* treeItem)
{
	m_treeItem = treeItem;
}

void QtBookmarkCategory::updateArrow()
{
	if (m_treeItem != nullptr)
	{
		if (m_treeItem->isExpanded())
		{
			QPixmap pixmap(QString::fromUtf8(QtResources::BOOKMARK_VIEW_ARROW_DOWN));
			m_expandButton->setIcon(QIcon(utility::colorizePixmap(pixmap, "black")));
		}
		else
		{
			QPixmap pixmap(QString::fromUtf8(QtResources::BOOKMARK_VIEW_ARROW_RIGHT));
			m_expandButton->setIcon(QIcon(utility::colorizePixmap(pixmap, "black")));
		}
	}
}

void QtBookmarkCategory::expandClicked()
{
	if (m_treeItem != nullptr)
	{
		m_treeItem->setExpanded(!m_treeItem->isExpanded());
		updateArrow();
	}
}

void QtBookmarkCategory::enterEvent(QEnterEvent * /*event*/)
{
	m_deleteButton->show();
}

void QtBookmarkCategory::leaveEvent(QEvent*  /*event*/)
{
	m_deleteButton->hide();
}

void QtBookmarkCategory::deleteClicked()
{
	// A question (QuestionStateMachine): an agent answers it as the box's buttons do.
	whenAnswered(
		Application::getInstance()->getDialogView(DialogView::UseCase::GENERAL)->asked(
			"Do you really want to delete the category '" + m_name->text().toStdString() +
				"' AND all the bookmarks in it?",
			{"Delete", "Keep"}),
		[proxy = m_controllerProxy, id = m_id](int option) {
			if (option == 0)
			{
				proxy->executeAsTaskWithArgs(&BookmarkController::deleteBookmarkCategory, id);
			}
		});
}
