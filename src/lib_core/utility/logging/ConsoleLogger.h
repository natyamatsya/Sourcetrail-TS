#ifndef CONSOLE_LOGGER_H
#define CONSOLE_LOGGER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <mutex>

#include "LogMessage.h"
#include "Logger.h"
#endif

SRCTRL_EXPORT class ConsoleLogger: public Logger
{
public:
	ConsoleLogger();

private:
	void logInfo(const LogMessage& message) override;
	void logWarning(const LogMessage& message) override;
	void logError(const LogMessage& message) override;

	static void logMessage(const std::string& type, const LogMessage& message);
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "ConsoleLogger.inl"
#endif

#endif	  // CONSOLE_LOGGER_H
