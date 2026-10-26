#ifndef PROCESS_MONITOR_H
#define PROCESS_MONITOR_H

#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#include <QThread>

#include "utilityApp.h"

class QObject;

namespace execution
{
class SerialQueue;
}

// Runs processes and tells when each ended, all from one thread of its own with an
// event loop: no thread waits for a process. What a process writes (stdout and
// stderr, merged) is read as it comes, so its pipe never fills.
//
// What `ended` and post() run, runs on that thread: keep it short, and post longer
// work elsewhere.
//
// Without a QCoreApplication (unit tests) no event loop can run: a thread then waits
// for each process, as utility::executeProcess does, and what is posted runs one at a
// time on a serial queue.
class ProcessMonitor
{
public:
	ProcessMonitor();
	//! Kills the processes still running and ends the thread; their `ended` calls do
	//! not come.
	~ProcessMonitor();

	ProcessMonitor(const ProcessMonitor&) = delete;
	ProcessMonitor& operator=(const ProcessMonitor&) = delete;

	//! Starts `command` with `arguments`, as utility::executeProcess does with no
	//! timeout; `ended` gets what it wrote, its exit code and how it ran (exit code
	//! -1: it did not start).
	void start(
		const std::string& command,
		const std::vector<std::string>& arguments,
		std::function<void(utility::ProcessOutput)> ended);

	//! Runs `work` on the monitor's thread.
	void post(std::function<void()> work);

	//! Kills the processes running now; their `ended` calls follow.
	void killAll();

	//! How many processes all monitors run now (started, not yet ended): a sign of
	//! life for a watchdog.
	static std::size_t running();

private:
	QThread m_thread;
	QObject* m_context = nullptr;	// lives on m_thread: the processes' parent

	// Without a QCoreApplication.
	std::shared_ptr<execution::SerialQueue> m_queue;
	std::vector<std::jthread> m_waiters;	// one per process started
};

#endif	  // PROCESS_MONITOR_H
