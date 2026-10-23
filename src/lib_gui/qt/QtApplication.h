#ifndef QT_APPLICATION_H
#define QT_APPLICATION_H

#include <QApplication>

class QtApplication: public QApplication
{
	Q_OBJECT
public:
	QtApplication(int& argc, char** argv);

	bool event(QEvent* event) override;
	bool notify(QObject* receiver, QEvent* event) override;
	static int exec();

private slots:
	static void onApplicationStateChanged(Qt::ApplicationState state);
};

#endif	  // QT_APPLICATION_H
