#include "utilityApp.h"

#include "ScopedFunctor.h"
#include "logging.h"
#include "utilityString.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>

using namespace std::chrono;

namespace utility
{
std::mutex s_runningProcessesMutex;
std::set<std::shared_ptr<QProcess>> s_runningProcesses;

std::string getDocumentationLink()
{
	return "https://github.com/petermost/Sourcetrail/blob/master/DOCUMENTATION.md";
}

std::string searchPath(const std::string& bin, bool& ok)
{
	ok = false;

	if (const char* pathEnv = std::getenv("PATH"))
	{
		for (const auto& dir : splitToVector(pathEnv, ":"))
		{
			std::filesystem::path candidate = std::filesystem::path(dir) / bin;
			std::error_code ec;
			if (std::filesystem::is_regular_file(candidate, ec))
			{
				ok = true;
				return candidate.generic_string();
			}
		}
	}
	return bin;
}

std::string searchPath(const std::string& bin)
{
	bool ok;
	return searchPath(bin, ok);
}

namespace
{
void logOutputLines(const std::string& text, std::string& logBuffer)
{
	logBuffer += text;
	if (logBuffer.empty())
		return;

	const bool isEndOfLine = (logBuffer.back() == '\n');
	const std::vector<std::string> lines = splitToVector(logBuffer, "\n");
	for (size_t i = 0; i < lines.size() - (isEndOfLine ? 0 : 1); i++)
		LOG_INFO_BARE("Process output: " + lines[i]);

	if (isEndOfLine)
		logBuffer.clear();
	else
		logBuffer = lines.back();
}
}	 // namespace

ProcessOutput executeProcess(const std::string& command, const std::vector<std::string>& arguments, const FilePath& workingDirectory,
	const bool waitUntilNoOutput, const milliseconds &timeout, bool logProcessOutput)
{
	auto process = std::make_shared<QProcess>();

	// Set up environment with extra PATH entries
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	QString path = env.value(QStringLiteral("PATH"));
	path = QStringLiteral("/opt/local/bin:/usr/local/bin:") + path;
	env.insert(QStringLiteral("PATH"), path);
	process->setProcessEnvironment(env);

	if (!workingDirectory.empty())
		process->setWorkingDirectory(QString::fromStdString(workingDirectory.str()));

	// Merge stdout and stderr into one channel
	process->setProcessChannelMode(QProcess::MergedChannels);

	// Convert arguments to QStringList
	QStringList qArgs;
	qArgs.reserve(static_cast<int>(arguments.size()));
	for (const auto& arg : arguments)
		qArgs << QString::fromStdString(arg);

	// Start process
	process->start(QString::fromStdString(searchPath(command)), qArgs);
	process->closeWriteChannel();

	if (!process->waitForStarted())
	{
		ProcessOutput ret;
		ret.error = process->errorString().toStdString();
		ret.exitCode = -1;
		LOG_ERROR_BARE("Process error: " + ret.error);
		return ret;
	}

	// Track running process
	{
		std::lock_guard<std::mutex> lock(s_runningProcessesMutex);
		s_runningProcesses.insert(process);
	}

	[[maybe_unused]]
	ScopedFunctor remover([process]()
	{
		std::lock_guard<std::mutex> lock(s_runningProcessesMutex);
		s_runningProcesses.erase(process);
	});

	// Read output
	std::string output;
	std::string logBuffer;

	if (timeout == INFINITE_TIMEOUT)
	{
		process->waitForFinished(-1);
		output = process->readAllStandardOutput().toStdString();
		if (logProcessOutput)
			logOutputLines(output, logBuffer);
	}
	else
	{
		auto deadline = steady_clock::now() + timeout;
		bool outputReceived = false;

		while (process->state() != QProcess::NotRunning)
		{
			auto remaining = duration_cast<milliseconds>(deadline - steady_clock::now());
			if (remaining <= milliseconds::zero())
			{
				if (waitUntilNoOutput && outputReceived)
				{
					outputReceived = false;
					deadline = steady_clock::now() + timeout;
					continue;
				}
				LOG_WARNING("Canceling process because it " +
					std::string(waitUntilNoOutput
						? "did not generate any output during the last "
						: "timed out after ") +
					std::to_string(duration_cast<seconds>(timeout).count()) + " seconds.");
				process->kill();
				process->waitForFinished(1000);
				break;
			}

			int waitMs = static_cast<int>(std::min(remaining, milliseconds(100)).count());
			if (process->waitForReadyRead(waitMs))
			{
				std::string chunk = process->readAllStandardOutput().toStdString();
				if (!chunk.empty())
				{
					outputReceived = true;
					output += chunk;
					if (logProcessOutput)
						logOutputLines(chunk, logBuffer);
				}
			}
		}

		// Read any remaining buffered output
		std::string remaining = process->readAllStandardOutput().toStdString();
		if (!remaining.empty())
		{
			output += remaining;
			if (logProcessOutput)
				logOutputLines(remaining, logBuffer);
		}
	}

	if (logProcessOutput && !logBuffer.empty())
	{
		for (const std::string& line : splitToVector(logBuffer, "\n"))
			LOG_INFO_BARE("Process output: " + line);
	}

	ProcessOutput ret;
	ret.output = trim(output);
	ret.exitCode = process->exitCode();
	return ret;
}

void killRunningProcesses()
{
	std::lock_guard<std::mutex> lock(s_runningProcessesMutex);
	for (const auto& process : s_runningProcesses)
		process->kill();
}

int getIdealThreadCount()
{
	int threadCount = QThread::idealThreadCount();
	if constexpr (Platform::isWindows())
	{
		threadCount -= 1;
	}
	return std::max(1, threadCount);
}

}	 // namespace utility

/* Not referenced anywhere!
enum class OsType
{
	UNKNOWN,
	LINUX,
	MAC,
	WINDOWS
};

std::string getOsTypeString()
{
	// WARNING: Don't change these string. The server API relies on them.
	if constexpr (Platform::isWindows())
		return "windows";
	else if constexpr (Platform::isMac())
		return "macOS";
	else if constexpr (Platform::isLinux())
		return "linux";
	else
		return "unknown";
}
*/
