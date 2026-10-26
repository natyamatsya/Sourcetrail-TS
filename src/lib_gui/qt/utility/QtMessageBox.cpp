#include "QtMessageBox.h"

#include <QPointer>

QtMessageBox::QtMessageBox(QWidget *parent)
	: QMessageBox(parent)
{
}

QAbstractButton *QtMessageBox::execModal()
{
	// Closing the parent while the box is open deletes it: then nothing was clicked.
	const QPointer<QtMessageBox> self(this);
	(void)exec();

	return self ? clickedButton() : nullptr;
}

int QtMessageBox::exec()
{
	return QMessageBox::exec();
}
