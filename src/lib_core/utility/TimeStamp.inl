// Inline implementations for TimeStamp.h. Included at the end of that header; not a standalone TU.

#pragma once

// Guarded like the header's std includes: in a module build the wrapper's GMF (or `import std`) already
// provides these, and re-including them in the purview would clash with `import std`.
#ifndef SRCTRL_MODULE_PURVIEW
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#endif

namespace
{
inline std::tm toLocalTm(std::chrono::system_clock::time_point tp)
{
	std::time_t t = std::chrono::system_clock::to_time_t(tp);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	return tm;
}

inline std::string formatLocal(std::chrono::system_clock::time_point tp, const char* fmt)
{
	std::tm tm = toLocalTm(tp);
	char buf[64];
	std::strftime(buf, sizeof(buf), fmt, &tm);
	return buf;
}
}	 // namespace

inline TimeStamp TimeStamp::now()
{
	return TimeStamp(std::chrono::system_clock::now());
}

inline double TimeStamp::durationSeconds(const TimeStamp& start)
{
	return double(TimeStamp::now().deltaMS(start)) / 1000.0;
}

inline std::string TimeStamp::secondsToString(double secs)
{
	std::stringstream ss;

	int hours = int(secs / 3600);
	secs -= hours * 3600;

	int minutes = int(secs / 60);
	secs -= minutes * 60;

	int seconds = int(secs);
	secs -= seconds;

	const int milliSeconds = static_cast<int>(secs * 1000);

	if (hours > 9)
	{
		ss << hours;
	}
	else
	{
		ss << std::setw(2) << std::setfill('0') << hours;
	}
	ss << ":" << std::setw(2) << std::setfill('0') << minutes;
	ss << ":" << std::setw(2) << std::setfill('0') << seconds;

	if (!hours && !minutes)
	{
		ss << ":" << std::setw(3) << std::setfill('0') << milliSeconds;
	}

	return ss.str();
}

inline TimeStamp::TimeStamp() {}

inline TimeStamp::TimeStamp(time_point t): m_time(t), m_valid(true) {}

inline TimeStamp::TimeStamp(std::string s)
{
	if (s.empty())
		return;

	std::tm tm{};
	tm.tm_isdst = -1;
	std::istringstream ss(s);
	ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
	if (!ss.fail())
	{
		std::time_t t = std::mktime(&tm);
		if (t != -1)
		{
			m_time = std::chrono::system_clock::from_time_t(t);
			m_valid = true;
		}
	}
}

inline bool TimeStamp::isValid() const
{
	return m_valid;
}

inline std::string TimeStamp::toString() const
{
	if (!m_valid)
		return "not-a-date-time";
	return formatLocal(m_time, "%Y-%m-%d %H:%M:%S");
}

inline std::string TimeStamp::getDDMMYYYYString() const
{
	if (!m_valid)
		return "not-a-date-time";
	return formatLocal(m_time, "%d-%m-%Y");
}

inline std::string TimeStamp::dayOfWeek() const
{
	if (!m_valid)
		return "none";

	std::tm tm = toLocalTm(m_time);
	switch (tm.tm_wday)
	{
	case 0:
		return "Sunday";
	case 1:
		return "Monday";
	case 2:
		return "Tuesday";
	case 3:
		return "Wednesday";
	case 4:
		return "Thursday";
	case 5:
		return "Friday";
	case 6:
		return "Saturday";
	}
	return "none";
}

inline std::string TimeStamp::dayOfWeekShort() const
{
	return dayOfWeek().substr(0, 3);
}

inline std::size_t TimeStamp::deltaMS(const TimeStamp& other) const
{
	auto d = std::chrono::duration_cast<std::chrono::milliseconds>(m_time - other.m_time);
	return static_cast<std::size_t>(std::abs(d.count()));
}

inline std::size_t TimeStamp::deltaS(const TimeStamp& other) const
{
	auto d = std::chrono::duration_cast<std::chrono::seconds>(m_time - other.m_time);
	return static_cast<std::size_t>(std::abs(d.count()));
}

inline bool TimeStamp::isSameDay(const TimeStamp& other) const
{
	std::tm a = toLocalTm(m_time);
	std::tm b = toLocalTm(other.m_time);
	return a.tm_mday == b.tm_mday && a.tm_mon == b.tm_mon && a.tm_year == b.tm_year;
}

inline std::size_t TimeStamp::deltaDays(const TimeStamp& other) const
{
	std::tm a = toLocalTm(m_time);
	std::tm b = toLocalTm(other.m_time);
	a.tm_hour = a.tm_min = a.tm_sec = 0;
	b.tm_hour = b.tm_min = b.tm_sec = 0;
	double diff = std::difftime(std::mktime(&a), std::mktime(&b));
	return static_cast<std::size_t>(std::abs(std::lround(diff / 86400.0)));
}

inline std::size_t TimeStamp::deltaHours(const TimeStamp& other) const
{
	auto d = std::chrono::duration_cast<std::chrono::hours>(m_time - other.m_time);
	return static_cast<std::size_t>(std::abs(d.count()));
}
