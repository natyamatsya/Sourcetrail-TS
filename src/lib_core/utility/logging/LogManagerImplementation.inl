// Inline implementations for LogManagerImplementation.h. Included at the end of that header
// (classic) or via the srctrl.logging wrapper (purview); not a standalone TU.

#pragma once

inline LogManagerImplementation::LogManagerImplementation() = default;

inline LogManagerImplementation::LogManagerImplementation(const LogManagerImplementation& other)
{
	m_loggers = other.m_loggers;
}

inline void LogManagerImplementation::operator=(const LogManagerImplementation& other)
{
	m_loggers = other.m_loggers;
}

inline LogManagerImplementation::~LogManagerImplementation() = default;

inline void LogManagerImplementation::addLogger(std::shared_ptr<Logger> logger)
{
	std::lock_guard<std::mutex> lockGuard(m_loggerMutex);
	m_loggers.push_back(logger);
}

inline void LogManagerImplementation::removeLogger(std::shared_ptr<Logger> logger)
{
	std::lock_guard<std::mutex> lockGuard(m_loggerMutex);
	std::vector<std::shared_ptr<Logger>>::iterator it = std::find(
		m_loggers.begin(), m_loggers.end(), logger);
	if (it != m_loggers.end())
	{
		m_loggers.erase(it);
	}
}

inline void LogManagerImplementation::removeLoggersByType(const std::string& type)
{
	std::lock_guard<std::mutex> lockGuard(m_loggerMutex);
	for (unsigned int i = 0; i < m_loggers.size(); i++)
	{
		if (m_loggers[i]->getType() == type)
		{
			m_loggers.erase(m_loggers.begin() + i);
			i--;
		}
	}
}

inline Logger* LogManagerImplementation::getLogger(std::shared_ptr<Logger> logger)
{
	std::lock_guard<std::mutex> lockGuard(m_loggerMutex);
	std::vector<std::shared_ptr<Logger>>::iterator it = std::find(
		m_loggers.begin(), m_loggers.end(), logger);
	if (it != m_loggers.end())
	{
		return (*it).get();
	}
	return nullptr;
}

inline Logger* LogManagerImplementation::getLoggerByType(const std::string& type)
{
	std::lock_guard<std::mutex> lockGuard(m_loggerMutex);
	for (unsigned int i = 0; i < m_loggers.size(); i++)
	{
		if (m_loggers[i]->getType() == type)
		{
			return m_loggers[i].get();
		}
	}
	return nullptr;
}

inline void LogManagerImplementation::clearLoggers()
{
	m_loggers.clear();
}

inline int LogManagerImplementation::getLoggerCount() const
{
	std::lock_guard<std::mutex> lockGuard(m_loggerMutex);
	return static_cast<int>(m_loggers.size());
}

inline void LogManagerImplementation::logInfo(
	const std::string& message,
	const std::string& file,
	const std::string& function,
	const unsigned int line)
{
	std::lock_guard<std::mutex> lockGuardLogger(m_loggerMutex);
	for (unsigned int i = 0; i < m_loggers.size(); i++)
	{
		m_loggers[i]->onInfo(
			LogMessage(message, file, function, line, getTime(), std::this_thread::get_id()));
	}
}

inline void LogManagerImplementation::logWarning(
	const std::string& message,
	const std::string& file,
	const std::string& function,
	const unsigned int line)
{
	std::lock_guard<std::mutex> lockGuardLogger(m_loggerMutex);
	for (unsigned int i = 0; i < m_loggers.size(); i++)
	{
		m_loggers[i]->onWarning(
			LogMessage(message, file, function, line, getTime(), std::this_thread::get_id()));
	}
}

inline void LogManagerImplementation::logError(
	const std::string& message,
	const std::string& file,
	const std::string& function,
	const unsigned int line)
{
	std::lock_guard<std::mutex> lockGuardLogger(m_loggerMutex);
	for (unsigned int i = 0; i < m_loggers.size(); i++)
	{
		m_loggers[i]->onError(
			LogMessage(message, file, function, line, getTime(), std::this_thread::get_id()));
	}
}

inline std::tm LogManagerImplementation::getTime()
{
	std::time_t time;
	std::time(&time);

	std::tm result = *std::localtime(&time);	// this is done because localtime returns a pointer to a
												// statically allocated object
	return result;
}
