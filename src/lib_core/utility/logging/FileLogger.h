#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <filesystem>
#include <string>

#include "LogMessage.h"
#include "Logger.h"
#endif

// Deliberately FilePath-free (std::filesystem::path API): the logging backend sits at the bottom of
// the module stack — srctrl.file already imports srctrl.logging, so a FilePath here would be a module
// cycle. FilePath callers pass .getPath().
SRCTRL_EXPORT class FileLogger: public Logger
{
public:
	static std::string generateDatedFileName(
		const std::string& prefix = "", const std::string& suffix = "", int offsetDays = 0);

	FileLogger();

	std::filesystem::path getLogFilePath() const;
	void setLogFilePath(const std::filesystem::path& filePath);

	void setLogDirectory(const std::filesystem::path& directory);
	void setFileName(const std::string& fileName);
	void setMaxLogLineCount(unsigned int logCount);

	// setting the max log file count to 0 will disable ringlogging
	void setMaxLogFileCount(unsigned int amount);

	void deleteLogFiles(const std::string& cutoffDate);

private:
	void logInfo(const LogMessage& message) override;
	void logWarning(const LogMessage& message) override;
	void logError(const LogMessage& message) override;

	void logMessage(const std::string& type, const LogMessage& message);
	void updateLogFileName();

	std::string m_logFileName;
	std::filesystem::path m_logDirectory;
	std::filesystem::path m_currentLogFilePath;

	unsigned int m_maxLogLineCount = 0;
	unsigned int m_maxLogFileCount = 0;
	unsigned int m_currentLogLineCount = 0;
	unsigned int m_currentLogFileCount = 0;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "FileLogger.inl"
#endif

#endif	  // FILE_LOGGER_H
