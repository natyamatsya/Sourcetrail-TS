#ifndef QT_TABLE_H
#define QT_TABLE_H

#include <QTableView>

class QtTable: public QTableView
{
	Q_OBJECT

public:
	QtTable(QWidget* parent = nullptr);
	~QtTable() override;

	// Keeps the filler rows that fill the view. Every row is one line high, its text elided
	// (the tooltip has it whole), so rows never need fitting to their contents: not when added,
	// nor when the columns or the view change width.
	void updateRows();
	int getFilledRowCount();

	void showFirstRow();
	void showLastRow();

	bool hasSelection() const;

protected:
	void resizeEvent(QResizeEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	float m_rowsToFill = 0;

	int m_colIndex = -1;
	int m_lastPos = -1;
};

#endif	  // QT_TABLE_H
