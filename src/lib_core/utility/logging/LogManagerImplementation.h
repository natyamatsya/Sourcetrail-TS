#ifndef LOG_MANAGER_IMPLEMENTATION_H
#define LOG_MANAGER_IMPLEMENTATION_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

#include "Logger.h"
#endif

SRCTRL_EXPORT class LogManagerImplementation
{
public:
	LogManagerImplementation();

	// Must be implemented because std::mutex is non-copyable, see
	// http://stackoverflow.com/questions/14263836/why-does-stdmutex-create-a-c2248-when-used-in-a-struct-with-windows-socket
	// for more details
	LogManagerImplementation(const LogManagerImplementation& other);
	void operator=(const LogManagerImplementation& other);

	~LogManagerImplementation();

	void addLogger(std::shared_ptr<Logger> logger);
	void removeLogger(std::shared_ptr<Logger> logger);
	void removeLoggersByType(const std::string& type);
	void clearLoggers();
	int getLoggerCount() const;
	Logger* getLogger(std::shared_ptr<Logger> logger);
	Logger* getLoggerByType(const std::string& type);

	void logInfo(
		const std::string& message,
		const std::string& file,
		const std::string& function,
		const unsigned int line);
	void logWarning(
		const std::string& message,
		const std::string& file,
		const std::string& function,
		const unsigned int line);
	void logError(
		const std::string& message,
		const std::string& file,
		const std::string& function,
		const unsigned int line);

private:
	static std::tm getTime();

	std::vector<std::shared_ptr<Logger>> m_loggers;

	mutable std::mutex m_loggerMutex;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "LogManagerImplementation.inl"
#endif

#endif	  // LOG_MANAGER_IMPLEMENTATION_H
