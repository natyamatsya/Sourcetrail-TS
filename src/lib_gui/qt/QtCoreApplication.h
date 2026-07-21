#ifndef QT_COREAPPLICATION_H
#define QT_COREAPPLICATION_H

#include <QCoreApplication>

#include <atomic>
#include <cstdint>

#include "MessageIndexingStatus.h"
#include "MessageListener.h"
#include "MessageQuitApplication.h"
#include "MessageStatus.h"

class QTimer;

class QtCoreApplication
	: public QCoreApplication
	, public MessageListener<MessageQuitApplication>
	, public MessageListener<MessageIndexingStatus>
	, public MessageListener<MessageStatus>
{
	Q_OBJECT

public:
	QtCoreApplication(int argc, char** argv);
	~QtCoreApplication() override = default;

private:
	void handleMessage(MessageQuitApplication* message) override;
	void handleMessage(MessageIndexingStatus* message) override;
	void handleMessage(MessageStatus* message) override;

	// No-progress watchdog: the fundamental backstop for the hang classes the terminal-event
	// plumbing cannot see (deadlocks, a parent waiting on a silently dead worker pool, IPC
	// stalls). Message handlers stamp m_lastActivityMs from their (non-main) thread; a main-
	// thread timer compares against it and aborts the process with a distinct exit code when
	// nothing has moved for the configured span. SOURCETRAIL_HEADLESS_WATCHDOG_MINUTES
	// overrides the default (30); 0 disables.
	void setupWatchdog();
	void touchActivity();

	std::atomic<std::int64_t> m_lastActivityMs{0};
	std::int64_t m_watchdogLimitMs{0};
	QTimer* m_watchdogTimer{nullptr};
};

#endif	  // QT_COREAPPLICATION
