// Inline implementations for LogManager.h. Included at the end of that header (classic) or via the
// srctrl.logging wrapper (purview); not a standalone TU.

#pragma once

inline std::shared_ptr<LogManager> LogManager::s_instance;

inline std::shared_ptr<LogManager> LogManager::getInstance()
{
	if (s_instance.use_count() == 0)
	{
		s_instance = std::shared_ptr<LogManager>(new LogManager());
	}
	return s_instance;
}

inline void LogManager::destroyInstance()
{
	s_instance.reset();
}

inline LogManager::~LogManager() = default;

inline void LogManager::setLoggingEnabled(bool enabled)
{
	if (m_loggingEnabled != enabled)
	{
		m_loggingEnabled = enabled;

		// Out-of-line seam: the MessageStatus/Version announcement lives in LogManagerNotifier.cpp so
		// the (deliberately classic) messaging core never rides into logging consumers or the module.
		log_manager_detail::notifyLoggingToggled(enabled);
	}
}

inline bool LogManager::getLoggingEnabled() const
{
	return m_loggingEnabled;
}

inline void LogManager::addLogger(std::shared_ptr<Logger> logger)
{
	m_logManagerImplementation.addLogger(logger);
}

inline void LogManager::removeLogger(std::shared_ptr<Logger> logger)
{
	m_logManagerImplementation.removeLogger(logger);
}

inline void LogManager::removeLoggersByType(const std::string& type)
{
	m_logManagerImplementation.removeLoggersByType(type);
}

inline Logger* LogManager::getLogger(std::shared_ptr<Logger> logger)
{
	return m_logManagerImplementation.getLogger(logger);
}

inline Logger* LogManager::getLoggerByType(const std::string& type)
{
	return m_logManagerImplementation.getLoggerByType(type);
}

inline void LogManager::clearLoggers()
{
	m_logManagerImplementation.clearLoggers();
}

inline int LogManager::getLoggerCount() const
{
	return m_logManagerImplementation.getLoggerCount();
}

inline void LogManager::logInfo(
	const std::string& message,
	const std::string& file,
	const std::string& function,
	const unsigned int line)
{
	if (m_loggingEnabled)
	{
		m_logManagerImplementation.logInfo(message, file, function, line);
	}
}

inline void LogManager::logWarning(
	const std::string& message,
	const std::string& file,
	const std::string& function,
	const unsigned int line)
{
	if (m_loggingEnabled)
	{
		m_logManagerImplementation.logWarning(message, file, function, line);
	}
}

inline void LogManager::logError(
	const std::string& message,
	const std::string& file,
	const std::string& function,
	const unsigned int line)
{
	if (m_loggingEnabled)
	{
		m_logManagerImplementation.logError(message, file, function, line);
	}
}

inline LogManager::LogManager() = default;
