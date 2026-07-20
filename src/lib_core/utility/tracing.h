#ifndef TRACING_H
#define TRACING_H

// #define TRACING_ENABLED
// #define USE_ACCUMULATED_TRACING

// The tracer machinery (and its FilePath/TimeStamp dependencies) exists only when tracing is
// enabled; with tracing off this header reduces to the two empty macros below. That keeps it
// safe to #include in a module unit's global module fragment: it must not textually drag in
// headers whose types are attached to a named module (FilePath -> srctrl.file, TimeStamp ->
// srctrl.utility:time).
#ifdef TRACING_ENABLED

#include "FilePath.h"
#include "Id.h"
#include "TimeStamp.h"

#include <map>
#include <memory>
#include <mutex>
#include <stack>
#include <string>
#include <thread>
#include <vector>

struct TraceEvent
{
public:
	TraceEvent():  id(0), depth(0), time(0.0) {}

	TraceEvent(const std::string& eventName, Id id, size_t depth)
		: eventName(eventName), id(id), depth(depth), time(0.0)
	{
	}

	std::string eventName;
	Id id;
	size_t depth;

	std::string functionName;
	std::string locationName;

	double time;
};


class Tracer
{
public:
	static Tracer* getInstance();

	std::shared_ptr<TraceEvent> startEvent(const std::string& eventName);
	void finishEvent(std::shared_ptr<TraceEvent> event);

	void printTraces();

private:
	static std::shared_ptr<Tracer> s_instance;
	static Id s_nextTraceId;

	Tracer();
	Tracer(const Tracer&) = delete;
	void operator=(const Tracer&) = delete;

	std::map<std::thread::id, std::vector<std::shared_ptr<TraceEvent>>> m_events;
	std::map<std::thread::id, std::stack<TraceEvent*>> m_startedEvents;

	std::mutex m_mutex;
};


class AccumulatingTracer
{
public:
	static AccumulatingTracer* getInstance();

	std::shared_ptr<TraceEvent> startEvent(const std::string& eventName);
	void finishEvent(std::shared_ptr<TraceEvent> event);

	void printTraces();

private:
	struct AccumulatedTraceEvent
	{
		TraceEvent event;
		size_t count;
		double time;
	};

	static std::shared_ptr<AccumulatingTracer> s_instance;
	static Id s_nextTraceId;

	AccumulatingTracer();
	AccumulatingTracer(const AccumulatingTracer&) = delete;
	void operator=(const AccumulatingTracer&) = delete;

	std::map<std::string, AccumulatedTraceEvent> m_accumulatedEvents;
	std::map<std::thread::id, std::stack<TraceEvent*>> m_startedEvents;

	std::mutex m_mutex;
};


template <typename TracerType>
class ScopedTrace
{
public:
	ScopedTrace(
		const std::string& eventName,
		const std::string& fileName,
		int lineNumber,
		const std::string& functionName);
	~ScopedTrace();

private:
	std::shared_ptr<TraceEvent> m_event;
	TimeStamp m_timeStamp;
};

template <typename TracerType>
ScopedTrace<TracerType>::ScopedTrace(
	const std::string& eventName,
	const std::string& fileName,
	int lineNumber,
	const std::string& functionName)
{
	m_event = TracerType::getInstance()->startEvent(eventName);
	m_event->functionName = functionName;
	m_event->locationName = FilePath(fileName).fileName() + ":" +
		std::to_string(lineNumber);

	m_timeStamp = TimeStamp::now();
}

template <typename TracerType>
ScopedTrace<TracerType>::~ScopedTrace()
{
	m_event->time = TimeStamp::durationSeconds(m_timeStamp);
	TracerType::getInstance()->finishEvent(m_event);
}

#endif	  // TRACING_ENABLED


#ifdef TRACING_ENABLED
#	ifdef USE_ACCUMULATED_TRACING
#		define TRACE(...)                                                                    \
			ScopedTrace<AccumulatingTracer> __trace__(                                             \
				std::string(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)

#		define PRINT_TRACES() AccumulatingTracer::getInstance()->printTraces()
#	else
#		define TRACE(...)                                                                    \
			ScopedTrace<Tracer> __trace__(std::string(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)

#		define PRINT_TRACES() Tracer::getInstance()->printTraces()
#	endif


#else
#	define TRACE(...)
#	define PRINT_TRACES()
#endif

#endif	  // TRACING_H
