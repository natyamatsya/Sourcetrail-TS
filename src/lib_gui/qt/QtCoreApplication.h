#ifndef QT_COREAPPLICATION_H
#define QT_COREAPPLICATION_H

#include <QCoreApplication>

#include <atomic>
#include <cstdint>
#include <memory>

#include "MessageIndexingStatus.h"
#include "MessageListener.h"
#include "MessageQuitApplication.h"
#include "MessageStatus.h"

class HeadlessWatchdog;
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
	~QtCoreApplication() override;

private:
	void handleMessage(MessageQuitApplication* message) override;
	void handleMessage(MessageIndexingStatus* message) override;
	void handleMessage(MessageStatus* message) override;

	// The watchdog: the backstop for the hang classes the terminal-event plumbing cannot
	// see. A main-thread timer looks at the run every few seconds (its state machines, its
	// files, its indexer processes, and when a status or progress message last came, which
	// the handlers stamp from their thread) and aborts it with a distinct exit code once
	// HeadlessWatchdog says it will not end by itself. SOURCETRAIL_HEADLESS_WATCHDOG_MINUTES
	// sets the long no-progress limit (default 30); 0 disables the watchdog.
	void setupWatchdog();
	void touchActivity();
	void lookAtTheRun();

	std::atomic<std::int64_t> m_lastActivityMs{0};
	std::unique_ptr<HeadlessWatchdog> m_watchdog;
	QTimer* m_watchdogTimer{nullptr};
};

#endif	  // QT_COREAPPLICATION
