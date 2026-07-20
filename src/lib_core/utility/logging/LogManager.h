#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include "SrctrlModule.h"

// The messaging seam (see LogManagerNotifier.h): include-free, so it is safe in every context and the
// srctrl.logging wrapper puts it in its global module fragment — this include is then a no-op in the
// purview (include guard), keeping the declaration a global-module entity its classic definition links
// against.
#include "LogManagerNotifier.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>

#include "LogManagerImplementation.h"
#include "Logger.h"
#endif

SRCTRL_EXPORT class LogManager
{
public:
	static std::shared_ptr<LogManager> getInstance();
	static void destroyInstance();

	~LogManager();

	void setLoggingEnabled(bool enabled);
	bool getLoggingEnabled() const;

	void addLogger(std::shared_ptr<Logger> logger);
	void removeLogger(std::shared_ptr<Logger> logger);
	void removeLoggersByType(const std::string& type);
	void clearLoggers();
	int getLoggerCount() const;
	Logger* getLoggerByType(const std::string& type);
	Logger* getLogger(std::shared_ptr<Logger> logger);

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
	static std::shared_ptr<LogManager> s_instance;

	LogManager();
	LogManager(const LogManager&);
	void operator=(const LogManager&);

	LogManagerImplementation m_logManagerImplementation;
	bool m_loggingEnabled = false;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "LogManager.inl"
#endif

#endif	  // LOG_MANAGER_H
