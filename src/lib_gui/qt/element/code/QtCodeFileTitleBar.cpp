#include "QtCodeFileTitleBar.h"

#include "Application.h"
#ifndef SRCTRL_MODULE_BUILD
#include "MessageErrorsForFile.h"
#endif
#include "Project.h"
#include "QtCodeGlyphButton.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QVariant>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

QtCodeFileTitleBar::QtCodeFileTitleBar(QWidget* parent, bool isHovering, bool isSingle)
	: QtHoverButton(parent)
{
	setObjectName(QStringLiteral("title_bar"));
	setProperty("hovering", isHovering);
	setAttribute(Qt::WA_LayoutUsesWidgetRect);	  // fixes layouting on Mac

	if (!isSingle)
	{
		connect(this, &QPushButton::clicked, this, &QtCodeFileTitleBar::clickedTitleBar);
	}

	QHBoxLayout* titleLayout = new QHBoxLayout();
	titleLayout->setContentsMargins(0, 0, 0, 0);
	titleLayout->setSpacing(0);
	titleLayout->setAlignment(Qt::AlignLeft);
	setLayout(titleLayout);

	m_expandButton = new QtCodeGlyphButton(QtCodeGlyph::ChevronRight, this);
	m_collapseButton = new QtCodeGlyphButton(QtCodeGlyph::ChevronDown, this);

	m_expandButton->setToolTip(QStringLiteral("expand"));
	m_collapseButton->setToolTip(QStringLiteral("collapse"));
	m_expandButton->setAccessibleName(tr("Expand"));
	m_collapseButton->setAccessibleName(tr("Collapse"));

	for (QtCodeGlyphButton* button: {m_expandButton, m_collapseButton})
	{
		button->setObjectName(QStringLiteral("expand_button"));
		titleLayout->addWidget(button);
	}

	connect(
		m_expandButton,
		&QtCodeGlyphButton::clicked,
		this,
		&QtCodeFileTitleBar::clickedExpandButton);
	connect(
		m_collapseButton,
		&QtCodeGlyphButton::clicked,
		this,
		&QtCodeFileTitleBar::clickedCollapseButton);

	m_titleButton = new QtCodeFileTitleButton(this);
	QSizePolicy policy = m_titleButton->sizePolicy();
	policy.setRetainSizeWhenHidden(true);
	m_titleButton->setSizePolicy(policy);
	titleLayout->addWidget(m_titleButton);

	setMinimumHeight(m_titleButton->height() + 4);

	m_referenceCount = new QLabel(this);
	m_referenceCount->setObjectName(QStringLiteral("references_label"));
	m_referenceCount->hide();
	titleLayout->addWidget(m_referenceCount);

	titleLayout->addStretch(3);

	m_showErrorsButton = new QPushButton(QStringLiteral("show errors"));
	m_showErrorsButton->setObjectName(QStringLiteral("screen_button"));
	m_showErrorsButton->setToolTip(
		QStringLiteral("Show all errors causing this file to be incomplete"));
	m_showErrorsButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);	  // fixes layouting on Mac
	m_showErrorsButton->hide();
	titleLayout->addWidget(m_showErrorsButton);

	connect(m_showErrorsButton, &QPushButton::clicked, [this]() {
		MessageErrorsForFile(m_titleButton->getFilePath()).dispatch();
	});

	m_snippetButton = new QtCodeGlyphButton(QtCodeGlyph::Snippets, this);
	m_snippetButton->setToolTip(QStringLiteral("show snippets"));
	m_snippetButton->setAccessibleName(tr("Show Snippets"));

	m_maximizeButton = new QtCodeGlyphButton(QtCodeGlyph::Maximize, this);
	m_maximizeButton->setToolTip(QStringLiteral("maximize"));
	m_maximizeButton->setAccessibleName(tr("Maximize"));

	for (QtCodeGlyphButton* button: {m_snippetButton, m_maximizeButton})
	{
		button->setObjectName(QStringLiteral("file_button"));
		button->setEnabled(false);
		button->setFaintUntilHovered(this);
		titleLayout->addWidget(button);
	}

	connect(m_snippetButton, &QtCodeGlyphButton::clicked, this, &QtCodeFileTitleBar::clickedSnippetButton);
	connect(m_maximizeButton, &QtCodeGlyphButton::clicked, this, &QtCodeFileTitleBar::clickedMaximizeButton);

	if (isSingle)
	{
		m_expandButton->hide();
		m_collapseButton->hide();
		m_maximizeButton->hide();
	}
	else
	{
		m_snippetButton->hide();
	}
}

QtCodeFileTitleButton* QtCodeFileTitleBar::getTitleButton() const
{
	return m_titleButton;
}

void QtCodeFileTitleBar::setIsComplete(bool isComplete)
{
	m_titleButton->setIsComplete(isComplete);
	m_showErrorsButton->setVisible(!isComplete);

	std::shared_ptr<const Project> project = Application::getInstance()->getCurrentProject();
	if (project && project->isIndexing())
	{
		m_showErrorsButton->setVisible(false);
	}
}

void QtCodeFileTitleBar::setIsIndexed(bool isIndexed)
{
	m_titleButton->setIsIndexed(isIndexed);
}

void QtCodeFileTitleBar::updateRefCount(int refCount, bool hasErrors, size_t fatalErrorCount)
{
	if (refCount > 0)
	{
		QString label = hasErrors ? QStringLiteral("error") : QStringLiteral("reference");
		if (refCount > 1)
		{
			label += QChar('s');
		}

		if (fatalErrorCount > 0)
		{
			label += QStringLiteral(" (") + QString::number(fatalErrorCount) +
				QStringLiteral(" fatal)");
		}

		QString text = QString::number(refCount) + QChar(' ') + label;
		if (text != m_referenceCount->text())
		{
			m_referenceCount->setText(text);
		}
		m_referenceCount->show();
	}
	else
	{
		m_referenceCount->hide();
	}
}

bool QtCodeFileTitleBar::isCollapsed() const
{
	return m_expandButton->isVisible();
}

void QtCodeFileTitleBar::setIsFocused(bool focused)
{
	setProperty("focused", focused);
	style()->polish(this);
}

void QtCodeFileTitleBar::setMinimized()
{
	m_expandButton->setVisible(true);
	m_collapseButton->setVisible(false);
	m_snippetButton->setEnabled(true);
	m_maximizeButton->setEnabled(true);
}

void QtCodeFileTitleBar::setSnippets()
{
	m_expandButton->setVisible(false);
	m_collapseButton->setVisible(true);
	m_snippetButton->setEnabled(false);
	m_maximizeButton->setEnabled(true);
}

void QtCodeFileTitleBar::setMaximized()
{
	m_expandButton->setVisible(false);
	m_collapseButton->setVisible(false);
	m_snippetButton->setEnabled(true);
	m_maximizeButton->setEnabled(false);
}

void QtCodeFileTitleBar::updateFromOther(const QtCodeFileTitleBar* other)
{
	m_titleButton->updateFromOther(other->getTitleButton());
	setIsComplete(m_titleButton->isComplete());

	QString refString = other->m_referenceCount->text();
	if (refString.size())
	{
		m_referenceCount->setText(refString);
		m_referenceCount->show();
	}
	else
	{
		m_referenceCount->hide();
	}

	m_expandButton->setVisible(other->m_expandButton->isVisible());
	m_collapseButton->setVisible(other->m_collapseButton->isVisible());
	m_snippetButton->setEnabled(other->m_snippetButton->isEnabled());
	m_maximizeButton->setEnabled(other->m_maximizeButton->isEnabled());

	disconnect();

	connect(this, &QPushButton::clicked, this, &QtCodeFileTitleBar::clickedTitleBar);
}

void QtCodeFileTitleBar::enterEvent(QEnterEvent* event)
{
	QtHoverButton::enterEvent(event);
	QtCodeGlyphButton::updateFaintButtons(*this);
}

void QtCodeFileTitleBar::leaveEvent(QEvent* event)
{
	QtHoverButton::leaveEvent(event);
	QtCodeGlyphButton::updateFaintButtons(*this);
}

void QtCodeFileTitleBar::clickedTitleBar()
{
	if (m_collapseButton->isVisible())
	{
		emit minimize();
	}
	else if (m_expandButton->isVisible())
	{
		emit snippet();
	}
	else
	{
		emit maximize();
	}
}

void QtCodeFileTitleBar::clickedExpandButton()
{
	emit snippet();
}

void QtCodeFileTitleBar::clickedCollapseButton()
{
	emit minimize();
}

void QtCodeFileTitleBar::clickedSnippetButton()
{
	emit snippet();
}

void QtCodeFileTitleBar::clickedMaximizeButton()
{
	emit maximize();
}
