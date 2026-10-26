#ifndef QT_TABLE_H
#define QT_TABLE_H

#include <QTableView>

class QtTable: public QTableView
{
	Q_OBJECT

public:
	QtTable(QWidget* parent = nullptr);
	~QtTable() override;

	// Keeps the filler rows that fill the view and fits the rows' heights to their contents,
	// those from `firstChangedRow` on: rows added at the end need not refit all the others.
	void updateRows(int firstChangedRow = 0);
	int getFilledRowCount();

	void showFirstRow();
	void showLastRow();

	bool hasSelection() const;

protected slots:
	void columnResized(int column, int oldWidth, int newWidth);

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
