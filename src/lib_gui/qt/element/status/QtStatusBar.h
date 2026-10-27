#ifndef QT_STATUS_BAR_H
#define QT_STATUS_BAR_H

#include "ErrorCountInfo.h"
#include "QtFlatButton.h"

#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>

#include <string>

class QtStatusBar: public QStatusBar
{
	Q_OBJECT

public:
	QtStatusBar();

	void setText(const std::string& text, bool isError, bool showLoader);
	void setErrorCount(ErrorCountInfo errorCount);

	void setIdeStatus(const std::string& text);

	void showIndexingProgress(size_t progressPercent);
	void hideIndexingProgress();
	
	void setTextEncoding(const std::string &encoding);
	
protected:
	void resizeEvent(QResizeEvent* event) override;

private slots:
	static void showStatus();
	static void showErrors();
	static void clickedIndexingProgress();

private:
	// loader widgets:
	QMovie *m_movie;
	QLabel *m_loader;
	
	// status text widgets:
	QtFlatButton *m_text;

	// indexing widgets:	
	QtFlatButton *m_indexingStatus;
	QProgressBar *m_indexingProgress;
	
	// error(s) widgets:
	QtFlatButton *m_errorButton;
	
	// ide status widgets:
	QLabel *m_ideStatusText;
	
	// encoding widgets:
	QLabel *m_textEncoding;
};

#endif	  // QT_STATUS_BAR_H
