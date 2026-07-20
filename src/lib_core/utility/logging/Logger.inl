// Inline implementations for Logger.h. Included at the end of that header (classic) or via the
// srctrl.logging wrapper (purview); not a standalone TU.

#pragma once

inline Logger::Logger(const std::string& type): m_type(type) {}

inline std::string Logger::getType() const
{
	return m_type;
}

inline Logger::LogLevelMask Logger::getLogLevel() const
{
	return m_levelMask;
}

inline void Logger::setLogLevel(LogLevelMask mask)
{
	m_levelMask = mask;
}

inline bool Logger::isLogLevel(LogLevelMask mask)
{
	return (m_levelMask & mask) > 0;
}

inline void Logger::onInfo(const LogMessage& message)
{
	if (isLogLevel(LOG_INFOS))
	{
		logInfo(message);
	}
}

inline void Logger::onWarning(const LogMessage& message)
{
	if (isLogLevel(LOG_WARNINGS))
	{
		logWarning(message);
	}
}

inline void Logger::onError(const LogMessage& message)
{
	if (isLogLevel(LOG_ERRORS))
	{
		logError(message);
	}
}
