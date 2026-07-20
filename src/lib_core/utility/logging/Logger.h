#ifndef LOGGER_H
#define LOGGER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <vector>

#include "LogMessage.h"
#endif

SRCTRL_EXPORT class Logger
{
public:
	typedef int LogLevelMask;
	enum LogLevel : int
	{
		LOG_INFOS = 0x1,
		LOG_WARNINGS = 0x2,
		LOG_ERRORS = 0x4,
		LOG_ALL = 0x7
	};

	Logger(const std::string& type);
	virtual ~Logger() = default;

	std::string getType() const;

	LogLevelMask getLogLevel() const;
	void setLogLevel(LogLevelMask mask);
	bool isLogLevel(LogLevelMask mask);

	void onInfo(const LogMessage& message);
	void onWarning(const LogMessage& message);
	void onError(const LogMessage& message);

private:
	virtual void logInfo(const LogMessage& message) = 0;
	virtual void logWarning(const LogMessage& message) = 0;
	virtual void logError(const LogMessage& message) = 0;

	const std::string m_type;
	LogLevelMask m_levelMask = LOG_ALL;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "Logger.inl"
#endif

#endif	  // LOGGER_H
