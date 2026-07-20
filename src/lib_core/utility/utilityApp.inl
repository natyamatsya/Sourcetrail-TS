// Inline implementations for utilityApp.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
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
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#endif

namespace utility
{
// inline: one instance of the running-process registry across all TUs that include this .inl.
inline std::mutex s_runningProcessesMutex;
inline std::set<std::shared_ptr<QProcess>> s_runningProcesses;

inline std::string getDocumentationLink()
{
	return "https://github.com/petermost/Sourcetrail/blob/master/DOCUMENTATION.md";
}

inline std::string searchPath(const std::string& bin, bool& ok)
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

inline std::string searchPath(const std::string& bin)
{
	bool ok;
	return searchPath(bin, ok);
}

// Named (not anonymous) detail namespace: these helpers are referenced from the inline
// executeProcess below; internal linkage would make each including TU reference a different
// entity from the same inline function -- an ODR violation.
namespace utility_app_detail
{
inline void logOutputLines(const std::string& text, std::string& logBuffer)
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

inline std::string quoteArgument(const std::string& argument)
{
	return "\"" + argument + "\"";
}

inline std::string buildCommandLineForInfo(
	const std::string& executable,
	const std::vector<std::string>& arguments)
{
	std::string commandLine = quoteArgument(executable);
	for (const std::string& argument: arguments)
		commandLine += " " + quoteArgument(argument);
	return commandLine;
}

inline std::string processStateToString(const QProcess::ProcessState state)
{
	switch (state)
	{
	case QProcess::NotRunning:
		return "NotRunning";
	case QProcess::Starting:
		return "Starting";
	case QProcess::Running:
		return "Running";
	}
	return "Unknown";
}

inline std::string processExitStatusToString(const QProcess::ExitStatus status)
{
	switch (status)
	{
	case QProcess::NormalExit:
		return "NormalExit";
	case QProcess::CrashExit:
		return "CrashExit";
	}
	return "Unknown";
}

inline std::string envValueForLog(const QProcessEnvironment& env, const QString& variable)
{
	if (!env.contains(variable))
		return "<unset>";
	return env.value(variable).toStdString();
}

inline std::string buildProcessInfo(
	const std::string& command,
	const std::string& resolvedCommand,
	const std::vector<std::string>& arguments,
	const FilePath& workingDirectory,
	const QProcessEnvironment& env,
	const QProcess& process,
	const bool started)
{
	std::string info;
	info += "requested_command: " + command;
	info += "\nresolved_command: " + resolvedCommand;
	info += "\ncommand_line: " + buildCommandLineForInfo(resolvedCommand, arguments);
	info += "\nworking_directory: " +
		(workingDirectory.empty() ? std::string("<default>") : workingDirectory.str());
	info += "\nstarted: " + std::string(started ? "true" : "false");
	info += "\nprocess_id: " + std::to_string(static_cast<long long>(process.processId()));
	info += "\nprocess_state: " + processStateToString(process.state());
	info += "\nexit_code: " + std::to_string(process.exitCode());
	info += "\nexit_status: " + processExitStatusToString(process.exitStatus());
	info += "\nqt_error_code: " + std::to_string(static_cast<int>(process.error()));
	info += "\nqt_error_string: " + process.errorString().toStdString();
	info += "\nPATH: " + envValueForLog(env, QStringLiteral("PATH"));
	info += "\nSDKROOT: " + envValueForLog(env, QStringLiteral("SDKROOT"));
	info += "\nCPATH: " + envValueForLog(env, QStringLiteral("CPATH"));
	info += "\nC_INCLUDE_PATH: " + envValueForLog(env, QStringLiteral("C_INCLUDE_PATH"));
	info += "\nCPLUS_INCLUDE_PATH: " + envValueForLog(env, QStringLiteral("CPLUS_INCLUDE_PATH"));
	info += "\nLIBRARY_PATH: " + envValueForLog(env, QStringLiteral("LIBRARY_PATH"));
	info += "\nDYLD_LIBRARY_PATH: " + envValueForLog(env, QStringLiteral("DYLD_LIBRARY_PATH"));
	return info;
}
}	 // namespace utility_app_detail

inline ProcessOutput executeProcess(const std::string& command, const std::vector<std::string>& arguments, const FilePath& workingDirectory,
	const bool waitUntilNoOutput, const std::chrono::milliseconds &timeout, bool logProcessOutput)
{
	auto process = std::make_shared<QProcess>();
	const std::string resolvedCommand = searchPath(command);

	// Set up environment with extra PATH entries
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifndef Q_OS_WIN
	// Unix-only: the colon-joined prefix would corrupt the semicolon-separated
	// PATH on Windows (it splices into the first existing entry).
	QString path = env.value(QStringLiteral("PATH"));
	path = QStringLiteral("/opt/local/bin:/usr/local/bin:") + path;
	env.insert(QStringLiteral("PATH"), path);
#endif

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
	process->start(QString::fromStdString(resolvedCommand), qArgs);
	process->closeWriteChannel();

	if (!process->waitForStarted())
	{
		ProcessOutput ret;
		ret.error = process->errorString().toStdString();
		ret.exitCode = -1;
		ret.processInfo = utility_app_detail::buildProcessInfo(
			command,
			resolvedCommand,
			arguments,
			workingDirectory,
			env,
			*process,
			false);
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
			utility_app_detail::logOutputLines(output, logBuffer);
	}
	else
	{
		auto deadline = std::chrono::steady_clock::now() + timeout;
		bool outputReceived = false;

		while (process->state() != QProcess::NotRunning)
		{
			auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
				deadline - std::chrono::steady_clock::now());
			if (remaining <= std::chrono::milliseconds::zero())
			{
				if (waitUntilNoOutput && outputReceived)
				{
					outputReceived = false;
					deadline = std::chrono::steady_clock::now() + timeout;
					continue;
				}
				LOG_WARNING("Canceling process because it " +
					std::string(waitUntilNoOutput
						? "did not generate any output during the last "
						: "timed out after ") +
					std::to_string(std::chrono::duration_cast<std::chrono::seconds>(timeout).count()) +
					" seconds.");
				process->kill();
				process->waitForFinished(1000);
				break;
			}

			int waitMs = static_cast<int>(std::min(remaining, std::chrono::milliseconds(100)).count());
			if (process->waitForReadyRead(waitMs))
			{
				std::string chunk = process->readAllStandardOutput().toStdString();
				if (!chunk.empty())
				{
					outputReceived = true;
					output += chunk;
					if (logProcessOutput)
						utility_app_detail::logOutputLines(chunk, logBuffer);
				}
			}
		}

		// Read any remaining buffered output
		std::string remaining = process->readAllStandardOutput().toStdString();
		if (!remaining.empty())
		{
			output += remaining;
			if (logProcessOutput)
				utility_app_detail::logOutputLines(remaining, logBuffer);
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
	ret.processInfo = utility_app_detail::buildProcessInfo(
		command,
		resolvedCommand,
		arguments,
		workingDirectory,
		env,
		*process,
		true);
	return ret;
}

inline void killRunningProcesses()
{
	std::lock_guard<std::mutex> lock(s_runningProcessesMutex);
	for (const auto& process : s_runningProcesses)
		process->kill();
}

inline int getIdealThreadCount()
{
	int threadCount = QThread::idealThreadCount();
	if constexpr (Platform::isWindows())
	{
		threadCount -= 1;
	}
	return std::max(1, threadCount);
}

}	 // namespace utility
