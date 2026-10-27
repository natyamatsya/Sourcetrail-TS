#ifndef QT_CODE_FILE_TITLE_BAR_H
#define QT_CODE_FILE_TITLE_BAR_H

#include "QtCodeFileTitleButton.h"
#include "QtHoverButton.h"

class QLabel;
class QtCodeFileTitleButton;
class QtCodeGlyphButton;

// A file's header line in the code view: expand or collapse, the file's name, its reference
// count as a pill, and the buttons for its snippets or the whole file, faint until hovered.
class QtCodeFileTitleBar: public QtHoverButton
{
	Q_OBJECT

signals:
	void minimize();
	void snippet();
	void maximize();

public:
	QtCodeFileTitleBar(QWidget* parent = nullptr, bool isHovering = false, bool isSingle = false);

	QtCodeFileTitleButton* getTitleButton() const;

	void setIsComplete(bool isComplete);
	void setIsIndexed(bool isIndexed);
	void updateRefCount(int refCount, bool hasErrors, size_t fatalErrorCount);

	bool isCollapsed() const;

	void setIsFocused(bool focused);

	void setMinimized();
	void setSnippets();
	void setMaximized();

	void updateFromOther(const QtCodeFileTitleBar* other);

protected:
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private slots:
	void clickedTitleBar();
	void clickedExpandButton();
	void clickedCollapseButton();
	void clickedSnippetButton();
	void clickedMaximizeButton();

private:
	QtCodeFileTitleButton* m_titleButton;
	QPushButton* m_showErrorsButton;
	QLabel* m_referenceCount;

	QtCodeGlyphButton* m_expandButton;
	QtCodeGlyphButton* m_collapseButton;

	QtCodeGlyphButton* m_snippetButton;
	QtCodeGlyphButton* m_maximizeButton;
};

#endif	  // QT_CODE_FILE_TITLE_BAR_H
