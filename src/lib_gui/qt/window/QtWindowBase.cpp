#include "QtWindowBase.h"

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#endif
#include "compatibilityQt.h"
#include "QtResources.h"

#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QVBoxLayout>

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

using namespace utility::compatibility;

QtWindowBase::QtWindowBase(bool isSubWindow, QWidget* parent)
	: QtWindowStackElement(parent)
	, m_isSubWindow(isSubWindow)
{
	if (isSubWindow)
	{
		setWindowFlags(Qt::SubWindow | Qt::FramelessWindowHint);
		setAttribute(Qt::WA_TranslucentBackground, true);
		setFocusPolicy(Qt::StrongFocus);
	}
	else
	{
		setWindowFlags(Qt::Window);
	}

	m_window = new QWidget(this);
	m_window->setObjectName(QStringLiteral("window"));

	QVBoxLayout* layout = new QVBoxLayout(m_window);
	layout->setSpacing(0);

	m_content = new QWidget();
	layout->addWidget(m_content);

	if (isSubWindow)
	{
		std::string frameStyle =
			"#window {"
			"	border: 1px solid lightgray;"
			"	border-radius: 15px;"
			"	background: white;"
			"}";
		m_window->setStyleSheet(frameStyle.c_str());

		QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect;
		effect->setBlurRadius(15);
		effect->setXOffset(0);
		effect->setYOffset(5);
		effect->setColor(Qt::darkGray);
		m_window->setGraphicsEffect(effect);

		QHBoxLayout* gripLayout = new QHBoxLayout();
		m_sizeGrip = new QSizeGrip(m_window);
		setSizeGripStyle(true);
		gripLayout->addWidget(new QWidget());
		gripLayout->addWidget(m_sizeGrip);
		layout->addLayout(gripLayout);
	}

	resize(sizeHint());

	moveToCenter();

	this->raise();
}

QSize QtWindowBase::sizeHint() const
{
	return QSize(
		ApplicationSettings::getInstance()->getWindowBaseWidth(),
		ApplicationSettings::getInstance()->getWindowBaseHeight());
}

void QtWindowBase::setSizeGripStyle(bool isBlack)
{
	if (!m_sizeGrip)
	{
		return;
	}

	const std::string path = isBlack ? QtResources::WINDOW_SIZE_GRIP_BLACK : QtResources::WINDOW_SIZE_GRIP_WHITE;

	m_sizeGrip->setStyleSheet(QString::fromStdString(
		"QSizeGrip {"
		"	max-height: 16px;"
		"	max-width: 16px;"
		"	border-image: url(" + path + ");"
		"}"));
}

bool QtWindowBase::isSubWindow() const
{
	return m_isSubWindow;
}

void QtWindowBase::moveToCenter()
{
	if (parentWidget())
	{
		if (m_isSubWindow)
		{
			move(
				parentWidget()->width() / 2 - sizeHint().width() / 2,
				parentWidget()->height() / 2 - sizeHint().height() / 2);
		}
		else
		{
			move(
				parentWidget()->pos().x() + parentWidget()->width() / 2 - sizeHint().width() / 2,
				parentWidget()->pos().y() + parentWidget()->height() / 2 - sizeHint().height() / 2);
		}
	}
}

void QtWindowBase::setVisible(bool visible)
{
	QWidget::setVisible(visible);

	if (visible)
		setFocus();
}

void QtWindowBase::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton && m_mousePressedInWindow)
	{
		if (m_isSubWindow)
		{
			QPoint pos = QMouseEvent_globalPos(event) - m_dragPosition;
			QRect parentRect = parentWidget()->rect();

			if (pos.x() < parentRect.left())
			{
				pos.setX(parentRect.left());
			}
			pos.setX(qBound(parentRect.left(), parentRect.right() - width(), pos.x()));
			pos.setY(qBound(parentRect.top(), parentRect.bottom() - height(), pos.y()));

			move(pos);
			event->accept();
		}
		else
		{
			move(QMouseEvent_globalPos(event) - m_dragPosition);
			event->accept();
		}
	}
}

void QtWindowBase::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		m_dragPosition = QMouseEvent_globalPos(event) - frameGeometry().topLeft();
		event->accept();
		m_mousePressedInWindow = true;
	}
}

void QtWindowBase::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		m_dragPosition = QMouseEvent_globalPos(event) - frameGeometry().topLeft();
		event->accept();
		m_mousePressedInWindow = false;
	}
}
