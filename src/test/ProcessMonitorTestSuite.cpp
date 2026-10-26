#include "Catch2.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <filesystem>

#include <QCoreApplication>

#include "ProcessMonitor.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace
{
template <typename Predicate>
bool eventually(Predicate predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	while (!predicate())
	{
		if (std::chrono::steady_clock::now() > deadline)
		{
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return true;
}

// What the processes a monitor ran ended with.
struct Ends
{
	std::mutex mutex;
	std::vector<utility::ProcessOutput> outputs;
	std::set<std::thread::id> threads;
	std::size_t leastRunning = 1000;	// ProcessMonitor::running() as each end is told

	std::function<void(utility::ProcessOutput)> recorder()
	{
		return [this](utility::ProcessOutput output) {
			const std::lock_guard<std::mutex> lock(mutex);
			outputs.push_back(std::move(output));
			threads.insert(std::this_thread::get_id());
			leastRunning = (std::min)(leastRunning, ProcessMonitor::running());	// (): windows.h
		};
	}
	std::size_t count()
	{
		const std::lock_guard<std::mutex> lock(mutex);
		return outputs.size();
	}
};

// This test binary: run with --help it prints its usage and exits with 0. Asked of
// the system: there may be no QCoreApplication to ask.
std::string self()
{
#if defined(_WIN32)
	std::wstring path(32768, L'\0');
	path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
	return std::filesystem::path(path).string();
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string path(size, '\0');
	_NSGetExecutablePath(path.data(), &size);
	return path.c_str();
#else
	return std::filesystem::read_symlink("/proc/self/exe").string();
#endif
}

void runThree(ProcessMonitor& monitor, Ends& ends, const std::string& program)
{
	monitor.start(program, {"--help"}, ends.recorder());
	monitor.start(program, {"--help"}, ends.recorder());
	monitor.start("__no_such_program__", {}, ends.recorder());
	REQUIRE(eventually([&]() { return ends.count() == 3; }));

	int succeeded = 0;
	int failedToStart = 0;
	for (const utility::ProcessOutput& output: ends.outputs)
	{
		if (output.exitCode == 0)
		{
			succeeded++;
			REQUIRE(output.output.find("Catch2") != std::string::npos);	// read as it came
		}
		else if (output.exitCode == -1)
		{
			failedToStart++;
			REQUIRE_FALSE(output.processInfo.empty());
		}
	}
	REQUIRE(succeeded == 2);
	REQUIRE(failedToStart == 1);

	// A process counts as running until its end was told, then no more.
	REQUIRE(ends.leastRunning >= 1);
	REQUIRE(eventually([]() { return ProcessMonitor::running() == 0; }));
}
}	 // namespace

TEST_CASE("process monitor tells when each process ended, from its one thread")
{
	int argc = 1;
	char name[] = "Sourcetrail_test";
	char* argv[] = {name, nullptr};
	QCoreApplication app(argc, argv);

	Ends ends;
	{
		ProcessMonitor monitor;
		runThree(monitor, ends, self());

		// What is posted runs there too.
		std::atomic<bool> posted = false;
		std::thread::id postedOn;
		monitor.post([&]() {
			postedOn = std::this_thread::get_id();
			posted = true;
		});
		REQUIRE(eventually([&]() { return posted.load(); }));
		REQUIRE(ends.threads.size() == 1);
		REQUIRE(*ends.threads.begin() == postedOn);
	}
}

TEST_CASE("process monitor without an application waits for each process on a thread")
{
	REQUIRE(QCoreApplication::instance() == nullptr);
	Ends ends;
	ProcessMonitor monitor;
	runThree(monitor, ends, self());
}
