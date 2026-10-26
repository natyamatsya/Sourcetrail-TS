// Module build: LOG_* macros stay textual; backend via `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "QtCoreApplication.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <QTimer>

#include "AppStateMachine.h"
#include "HeadlessWatchdog.h"
#include "IndexingStateMachine.h"
#include "ProcessMonitor.h"
#include "ProjectStateMachine.h"
#include "logging.h"
#include "utilityApp.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

namespace
{
std::int64_t nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
}	 // namespace

QtCoreApplication::QtCoreApplication(int argc, char** argv): QCoreApplication(argc, argv)
{
	setupWatchdog();
}

QtCoreApplication::~QtCoreApplication() = default;

void QtCoreApplication::handleMessage(MessageQuitApplication* message)
{
	std::cout << "Quitting" << std::endl;
	// Message handlers run off the main thread; hop onto it for exit(). The exit code carries
	// the indexing outcome (0 = success), so headless callers can trust the process result.
	const int exitCode = message->exitCode;
	QMetaObject::invokeMethod(
		this, [exitCode]() { QCoreApplication::exit(exitCode); }, Qt::QueuedConnection);
}

void QtCoreApplication::handleMessage(MessageIndexingStatus* message)
{
	touchActivity();

	if (message->showProgress)
	{
		std::cout << message->progressPercent << "% " << '\r' << std::flush;
	}
}

void QtCoreApplication::handleMessage(MessageStatus* message)
{
	touchActivity();

	if (message->isError)
	{
		std::cout << "ERROR: ";
	}

	for (const std::string& status: message->stati())
	{
		std::cout << status << std::endl;
	}
}

void QtCoreApplication::setupWatchdog()
{
	std::int64_t minutes = 30;
	if (const char* configured = std::getenv("SOURCETRAIL_HEADLESS_WATCHDOG_MINUTES"))
	{
		char* end = nullptr;
		const long long parsed = std::strtoll(configured, &end, 10);
		if (end != configured && parsed >= 0)
		{
			minutes = parsed;
		}
	}
	if (minutes == 0)
	{
		return;
	}

	HeadlessWatchdog::Limits limits;
	limits.noProgress = std::chrono::minutes(minutes);
	m_watchdog = std::make_unique<HeadlessWatchdog>(limits);
	touchActivity();

	// Created in the constructor, so the timer lives on the main thread with the event loop.
	m_watchdogTimer = new QTimer(this);
	m_watchdogTimer->setInterval(5 * 1000);
	connect(m_watchdogTimer, &QTimer::timeout, this, [this]() { lookAtTheRun(); });
	m_watchdogTimer->start();
}

void QtCoreApplication::lookAtTheRun()
{
	using Clock = HeadlessWatchdog::Clock;
	const IndexingMachine::Progress progress = IndexingStateMachine::instance()->progress();

	HeadlessWatchdog::Look look;
	look.at = Clock::now();
	look.app = AppStateMachine::instance()->state();
	look.project = ProjectStateMachine::instance()->state();
	look.indexing = IndexingStateMachine::instance()->state();
	look.started = progress.started;
	look.finished = progress.finished;
	look.total = progress.total;
	look.liveProcesses = ProcessMonitor::running() + utility::runningProcessCount();
	look.lastActivity = Clock::time_point(
		std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(m_lastActivityMs.load())));

	const std::optional<std::string> reason = m_watchdog->check(look);
	if (!reason)
	{
		return;
	}
	const std::string message = "Headless watchdog: " + *reason +
		" -- aborting the run (exit code 3). Tune or disable via "
		"SOURCETRAIL_HEADLESS_WATCHDOG_MINUTES (0 = off).";
	LOG_ERROR(message);
	std::cerr << message << std::endl;
	m_watchdogTimer->stop();
	QCoreApplication::exit(3);
}

void QtCoreApplication::touchActivity()
{
	m_lastActivityMs.store(nowMs());
}
