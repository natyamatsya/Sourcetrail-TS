// Inline implementations for FileLogger.h. Included at the end of that header (classic) or via the
// srctrl.logging wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>
#endif

inline std::string FileLogger::generateDatedFileName(
	const std::string& prefix, const std::string& suffix, int offsetDays)
{
	std::time_t time;
	std::time(&time);

	std::tm t = *std::localtime(&time);

	if (offsetDays != 0)
	{
		time = std::mktime(&t) + offsetDays * 24 * 60 * 60;
		t = *std::localtime(&time);
	}

	std::stringstream filename;
	if (!prefix.empty())
	{
		filename << prefix << "_";
	}

	filename << t.tm_year + 1900 << "-";
	filename << (t.tm_mon < 9 ? "0" : "") << t.tm_mon + 1 << "-";
	filename << (t.tm_mday < 10 ? "0" : "") << t.tm_mday << "_";
	filename << (t.tm_hour < 10 ? "0" : "") << t.tm_hour << "-";
	filename << (t.tm_min < 10 ? "0" : "") << t.tm_min << "-";
	filename << (t.tm_sec < 10 ? "0" : "") << t.tm_sec;

	if (!suffix.empty())
	{
		filename << "_" << suffix;
	}

	return filename.str();
}

inline FileLogger::FileLogger()
	: Logger("FileLogger")
	, m_logFileName("log")
	, m_logDirectory("user/log/")
{
	updateLogFileName();
}

inline std::filesystem::path FileLogger::getLogFilePath() const
{
	return m_currentLogFilePath;
}

inline void FileLogger::setLogFilePath(const std::filesystem::path& filePath)
{
	m_currentLogFilePath = filePath;
	m_logFileName = "";
}

inline void FileLogger::setLogDirectory(const std::filesystem::path& directory)
{
	m_logDirectory = directory;
	std::error_code ec;
	std::filesystem::create_directories(m_logDirectory, ec);
}

inline void FileLogger::setFileName(const std::string& fileName)
{
	if (fileName != m_logFileName)
	{
		m_logFileName = fileName;
		m_currentLogLineCount = 0;
		m_currentLogFileCount = 0;
		updateLogFileName();
	}
}

inline void FileLogger::logInfo(const LogMessage& message)
{
	logMessage("INFO", message);
}

inline void FileLogger::logWarning(const LogMessage& message)
{
	logMessage("WARNING", message);
}

inline void FileLogger::logError(const LogMessage& message)
{
	logMessage("ERROR", message);
}

inline void FileLogger::setMaxLogLineCount(unsigned int lineCount)
{
	m_maxLogLineCount = lineCount;
}

inline void FileLogger::setMaxLogFileCount(unsigned int fileCount)
{
	m_maxLogFileCount = fileCount;
}

inline void FileLogger::deleteLogFiles(const std::string& cutoffDate)
{
	std::error_code ec;
	std::filesystem::directory_iterator it(m_logDirectory, ec);
	if (ec)
	{
		return;
	}

	for (const std::filesystem::directory_entry& entry: it)
	{
		if (entry.path().extension() == ".txt" && entry.path().filename().string() < cutoffDate)
		{
			std::filesystem::remove(entry.path(), ec);
		}
	}
}

inline void FileLogger::updateLogFileName()
{
	if (m_logFileName.empty())
	{
		return;
	}

	bool fileChanged = false;

	std::string currentLogFilePath = m_logDirectory.string() + m_logFileName;
	if (m_maxLogFileCount > 0)
	{
		currentLogFilePath += "_";
		if (m_currentLogLineCount >= m_maxLogLineCount)
		{
			m_currentLogLineCount = 0;

			m_currentLogFileCount++;
			if (m_currentLogFileCount >= m_maxLogFileCount)
			{
				m_currentLogFileCount = 0;
			}
			fileChanged = true;
		}
		currentLogFilePath += std::to_string(m_currentLogFileCount);
	}
	currentLogFilePath += ".txt";

	m_currentLogFilePath = std::filesystem::path(currentLogFilePath);

	if (fileChanged)
	{
		std::error_code ec;
		std::filesystem::remove(m_currentLogFilePath, ec);
	}
}

inline void FileLogger::logMessage(const std::string& type, const LogMessage& message)
{
	std::ofstream fileStream;
	fileStream.open(m_currentLogFilePath.string(), std::ios::app);
	fileStream << message.getTimeString("%H:%M:%S") << " | ";
	fileStream << message.threadId << " | ";

	if (message.filePath.size())
	{
		fileStream << message.getFileName() << ':' << message.line << ' ' << message.functionName
				   << "() | ";
	}

	fileStream << type << ": " << message.message << std::endl;
	fileStream.close();

	m_currentLogLineCount++;
	if (m_maxLogFileCount > 0)
	{
		updateLogFileName();
	}
}
