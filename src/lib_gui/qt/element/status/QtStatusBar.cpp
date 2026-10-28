#include "QtStatusBar.h"

#ifndef SRCTRL_MODULE_BUILD
#include "ColorScheme.h"
#include "MessageErrorsAll.h"
#include "MessageIndexingShowDialog.h"
#include "MessageRefresh.h"
#include "MessageShowStatus.h"
#endif
#include "QtActions.h"
#include "QtResources.h"
#include "utilityQt.h"

#include <QHBoxLayout>
#include <QMovie>
#include <QStyle>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.settings;
#endif

using namespace utility;

namespace
{
// An error shows in the status bar's error colour (main.css: QStatusBar QPushButton[error=true]).
void setShowsError(QWidget& widget, bool error)
{
	if (widget.property("error").toBool() == error)
	{
		return;
	}
	widget.setProperty("error", error);
	widget.style()->unpolish(&widget);
	widget.style()->polish(&widget);
}

QIcon outdatedDot()
{
	const QColor color{QString::fromStdString(ColorScheme::getInstance()->getColor("window/status/outdated"))};
	return QIcon{colorizePixmap(QPixmap{QString::fromUtf8(QtResources::STATUSBAR_VIEW_DOT)}, color).scaledToHeight(12)};
}

QIcon errorDot()
{
	const QColor color{QString::fromStdString(ColorScheme::getInstance()->getColor("window/status/error"))};
	return QIcon{colorizePixmap(QPixmap{QString::fromUtf8(QtResources::STATUSBAR_VIEW_DOT)}, color).scaledToHeight(12)};
}
}	 // namespace

QtStatusBar::QtStatusBar()
{
	addWidget(new QWidget()); // add some space

	// loader animation
	{
		m_movie = new QMovie(this);
		m_movie->setFileName(QString::fromUtf8(QtResources::STATUSBAR_VIEW_LOADER));
		
		// Set the movie to loop forever:
		if (m_movie->loopCount() != -1)
			connect(m_movie, &QMovie::finished, m_movie, &QMovie::start);
		m_movie->start();

		m_loader = new QLabel(this);	
		m_loader->setMovie(m_movie);
		m_loader->hide();
		addWidget(m_loader);
	}
	
	// status text
	{
		m_text = new QtFlatButton(this);
		m_text->setStyleSheet(QStringLiteral("QPushButton { margin-right: 0; spacing: none; text-align: left; }"));
		m_text->setSizePolicy(QSizePolicy::Ignored, m_text->sizePolicy().verticalPolicy());
		addWidget(m_text, 1);
		setText("", false, false);
	
		connect(m_text, &QPushButton::clicked, this, &QtStatusBar::showStatus);
	}
		
	// The permanent widgets have no lines between them: main.css spaces them out.

	// indexing status
	{
		m_indexingStatus = new QtFlatButton(this);
		m_indexingStatus->setMinimumWidth(150);
		m_indexingStatus->setStyleSheet(QStringLiteral("QPushButton { margin-right: 0; spacing: none; }"));
		
		connect(m_indexingStatus, &QPushButton::clicked, this, &QtStatusBar::clickedIndexingProgress);
		
		QHBoxLayout *layout = new QHBoxLayout();
		layout->setContentsMargins(0, 0, 0, 0);
		
		layout->addWidget(new QLabel(tr("Indexing:")));
		
		m_indexingProgress = new QProgressBar(this);
		m_indexingProgress->setMinimum(0);
		m_indexingProgress->setMaximum(100);
		m_indexingProgress->setValue(100);
		layout->addWidget(m_indexingProgress);
		
		m_indexingStatus->setLayout(layout);
		m_indexingStatus->hide();
		
		addPermanentWidget(m_indexingStatus);
	}
	
	// indexed files changed on disk: one click updates the index
	{
		m_changedFiles = new QtFlatButton(this);
		m_changedFiles->setObjectName(QStringLiteral("changed_files"));
		m_changedFiles->setAccessibleName(tr("Update changed files"));
		m_changedFiles->setStyleSheet(QStringLiteral("QPushButton { margin-right: 0; spacing: none; }"));
		m_changedFiles->setIcon(outdatedDot());
		m_changedFiles->hide();
		addPermanentWidget(m_changedFiles);

		connect(m_changedFiles, &QPushButton::clicked, this, &QtStatusBar::clickedChangedFiles);
	}

	// errors
	{
		m_errorButton = new QtFlatButton(this);
		m_errorButton->hide();
		m_errorButton->setStyleSheet(QStringLiteral("QPushButton { margin-right: 0; spacing: none; }"));
		setShowsError(*m_errorButton, true);
		m_errorButton->setIcon(errorDot());
		addPermanentWidget(m_errorButton);
		
		connect(m_errorButton, &QPushButton::clicked, this, &QtStatusBar::showErrors);
	}

	// ide status
	{
		m_ideStatusText = new QLabel(this);
		m_ideStatusText->setToolTip(tr("IDE Status"));
		addPermanentWidget(m_ideStatusText);
	}

	// text encoding
	{
		m_textEncoding = new QLabel(this);
		m_textEncoding->setToolTip(QtActions::textEncoding().tooltip());
		addPermanentWidget(m_textEncoding);
	}
}

void QtStatusBar::setText(const std::string &text, bool isError, bool showLoader)
{
	setShowsError(*m_text, isError);

	if (showLoader)
		m_loader->show();
	else
		m_loader->hide();

	m_text->setText(m_text->fontMetrics().elidedText(QString::fromStdString(text), Qt::ElideRight, m_text->width()));
}

void QtStatusBar::setErrorCount(ErrorCountInfo errorCount)
{
	if (errorCount.total > 0)
	{
		QString errorText = tr("%1 error%2").arg(errorCount.total).arg(errorCount.total > 1 ? "s" : "");
		QString fatalText = tr("(%1 fatal)").arg(errorCount.fatal);
		m_errorButton->setText(errorText + (errorCount.fatal > 0 ? " " + fatalText : ""));
		m_errorButton->setMinimumWidth(m_errorButton->fontMetrics().boundingRect(QString(m_errorButton->text().size(), 'a')).width());

		setShowsError(*m_errorButton, errorCount.fatal > 0);
		m_errorButton->setIcon(errorDot());	   // in the colour scheme's colour of the moment

		m_errorButton->show();
	}
	else
	{
		m_errorButton->hide();
	}
}

void QtStatusBar::setIdeStatus(const std::string &text)
{
	m_ideStatusText->setText(QString::fromStdString(text));
}

void QtStatusBar::showIndexingProgress(size_t progressPercent)
{
	m_indexingStatus->show();

	m_indexingProgress->setValue(static_cast<int>(progressPercent));
}

void QtStatusBar::hideIndexingProgress()
{
	m_indexingStatus->hide();
}

void QtStatusBar::showChangedFiles(size_t changedFileCount)
{
	if (changedFileCount == 0)
	{
		m_changedFiles->hide();
		return;
	}

	m_changedFiles->setText(
		changedFileCount == 1 ? tr("1 file changed \u2013 Update")
							  : tr("%1 files changed \u2013 Update").arg(changedFileCount));
	m_changedFiles->setToolTip(
		tr("Indexed files changed on disk since they were indexed. Click to index them anew (%1).")
			.arg(QtActions::refresh().shortcut()));
	m_changedFiles->setIcon(outdatedDot());	   // in the colour scheme's colour of the moment
	m_changedFiles->show();
}

void QtStatusBar::setTextEncoding(const std::string &encoding)
{
	m_textEncoding->setText(QString::fromStdString(encoding));
}

void QtStatusBar::resizeEvent(QResizeEvent * /*event*/)
{
	m_text->setText(m_text->fontMetrics().elidedText(m_text->text(), Qt::ElideRight, m_text->width()));
}

void QtStatusBar::showStatus()
{
	MessageShowStatus().dispatch();
}

void QtStatusBar::showErrors()
{
	MessageErrorsAll().dispatch();
}

void QtStatusBar::clickedIndexingProgress()
{
	MessageIndexingShowDialog().dispatch();
}

void QtStatusBar::clickedChangedFiles()
{
	MessageRefresh().direct().dispatch();
}
