#include "QtCoreApplication.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <QTimer>

#include "logging.h"

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

	m_watchdogLimitMs = minutes * 60 * 1000;
	touchActivity();

	// Created in the constructor, so the timer lives on the main thread with the event loop.
	m_watchdogTimer = new QTimer(this);
	m_watchdogTimer->setInterval(60 * 1000);
	connect(m_watchdogTimer, &QTimer::timeout, this, [this]() {
		const std::int64_t idleMs = nowMs() - m_lastActivityMs.load();
		if (idleMs < m_watchdogLimitMs)
		{
			return;
		}
		const std::string message =
			"Headless watchdog: no progress for " + std::to_string(idleMs / 60000) +
			" minutes -- aborting the run (exit code 3). Tune or disable via "
			"SOURCETRAIL_HEADLESS_WATCHDOG_MINUTES (0 = off).";
		LOG_ERROR(message);
		std::cerr << message << std::endl;
		QCoreApplication::exit(3);
	});
	m_watchdogTimer->start();
}

void QtCoreApplication::touchActivity()
{
	m_lastActivityMs.store(nowMs());
}
