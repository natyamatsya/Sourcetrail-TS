#include "tracing.h"

// The tracer classes exist only when tracing is enabled (see tracing.h); with tracing off
// this TU is intentionally empty.
#ifdef TRACING_ENABLED

#include <format>
#include <print>
#include <set>

std::shared_ptr<Tracer> Tracer::s_instance;
Id Tracer::s_nextTraceId = 0;

Tracer* Tracer::getInstance()
{
	if (!s_instance)
	{
		s_instance = std::shared_ptr<Tracer>(new Tracer());
	}

	return s_instance.get();
}

std::shared_ptr<TraceEvent> Tracer::startEvent(const std::string& eventName)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	const std::thread::id id = std::this_thread::get_id();

	std::shared_ptr<TraceEvent> event = std::make_shared<TraceEvent>(
		eventName, s_nextTraceId++, m_startedEvents[id].size());

	m_events[id].push_back(event);
	m_startedEvents[id].push(event.get());

	return event;
}

void Tracer::finishEvent(std::shared_ptr<TraceEvent>  /*event*/)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	const std::thread::id id = std::this_thread::get_id();

	m_startedEvents[id].pop();
}

void Tracer::printTraces()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	size_t unfinishEvents = 0;
	for (auto& p: m_startedEvents)
	{
		unfinishEvents += p.second.size();
	}

	if (unfinishEvents > 0)
	{
		std::println("TRACING: Trace events are still running.");
		return;
	}
	else if (m_events.empty())
	{
		std::println("TRACING: No trace events collected.");
		return;
	}


	std::println("TRACING\n--------------------------\n");

	std::println("HISTORY:\n");
	std::println(
		"    time                 name                     function"
		"                                          location");
	std::println(
		"-----------------------------------------------------------------"
		"------------------------------------------------------------");

	for (auto& p: m_events)
	{
		std::println("thread: {}", p.first);

		for (const std::shared_ptr<TraceEvent>& event: p.second)
		{
			// Nested-scope indentation: the time column grows by 2 per depth level and the
			// gap before the name shrinks to match, keeping the name/function columns aligned.
			std::println(
				"{:>{}.3f}{:>{}}{:<25}{:<50}{}",
				event->time,
				8 + 2 * event->depth,
				"",
				17 - 2 * event->depth,
				event->eventName,
				event->functionName + "()",
				event->locationName);
		}

		std::println("");
	}

	std::println("\nREPORT:\n");
	std::println(
		"    time      count      name                     function"
		"                                          location");
	std::println(
		"-----------------------------------------------------------------"
		"------------------------------------------------------------");

	struct AccumulatedTraceEvent
	{
		TraceEvent* event;
		size_t count;
		float time;
	};

	std::map<std::string, AccumulatedTraceEvent> accumulatedEvents;

	for (auto& p: m_events)
	{
		for (const std::shared_ptr<TraceEvent>& event: p.second)
		{
			const std::string name = event->eventName + event->functionName + event->locationName;

			std::pair<std::map<std::string, AccumulatedTraceEvent>::iterator, bool> p =
				accumulatedEvents.emplace(name, AccumulatedTraceEvent());

			AccumulatedTraceEvent* acc = &p.first->second;
			if (p.second)
			{
				acc->event = event.get();
				acc->time = static_cast<float>(event->time);
				acc->count = 1;
			}
			else
			{
				acc->time += static_cast<float>(event->time);
				acc->count++;
			}
		}
	}

	std::multiset<
		AccumulatedTraceEvent,
		std::function<bool(const AccumulatedTraceEvent&, const AccumulatedTraceEvent&)>>
		sortedEvents([](const AccumulatedTraceEvent& a, const AccumulatedTraceEvent& b) {
			return a.time > b.time;
		});

	for (const auto &p: accumulatedEvents)
	{
		sortedEvents.insert(p.second);
	}

	for (const AccumulatedTraceEvent& acc: sortedEvents)
	{
		std::println(
			"{:>8.3f}{:>10}       {:<25}{:<50}{}",
			acc.time,
			acc.count,
			acc.event->eventName,
			acc.event->functionName + "()",
			acc.event->locationName);
	}

	std::println("");

	m_events.clear();
}

Tracer::Tracer() = default;


std::shared_ptr<AccumulatingTracer> AccumulatingTracer::s_instance;
Id AccumulatingTracer::s_nextTraceId = 0;

AccumulatingTracer* AccumulatingTracer::getInstance()
{
	if (!s_instance)
	{
		s_instance = std::shared_ptr<AccumulatingTracer>(new AccumulatingTracer());
	}

	return s_instance.get();
}

std::shared_ptr<TraceEvent> AccumulatingTracer::startEvent(const std::string& eventName)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	const std::thread::id id = std::this_thread::get_id();

	std::shared_ptr<TraceEvent> event = std::make_shared<TraceEvent>(
		eventName, s_nextTraceId++, m_startedEvents[id].size());

	m_startedEvents[id].push(event.get());

	return event;
}

void AccumulatingTracer::finishEvent(std::shared_ptr<TraceEvent> event)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	const std::thread::id id = std::this_thread::get_id();

	m_startedEvents[id].pop();

	const std::string name = event->eventName + event->functionName + event->locationName;

	std::pair<std::map<std::string, AccumulatedTraceEvent>::iterator, bool> p =
		m_accumulatedEvents.emplace(name, AccumulatedTraceEvent());

	AccumulatedTraceEvent* acc = &p.first->second;
	if (p.second)
	{
		acc->event = TraceEvent(*event);
		acc->time = event->time;
		acc->count = 1;
	}
	else
	{
		acc->time += event->time;
		acc->count++;
	}
}

void AccumulatingTracer::printTraces()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto& p: m_startedEvents)
	{
		if (!p.second.empty())
		{
			std::println("TRACING: Trace events are still running.");
		}
	}

	std::println("\nREPORT:\n");
	std::println(
		"    time      count      name                     function"
		"                                          location");
	std::println(
		"-----------------------------------------------------------------"
		"------------------------------------------------------------");

	std::multiset<
		AccumulatedTraceEvent,
		std::function<bool(const AccumulatedTraceEvent&, const AccumulatedTraceEvent&)>>
		sortedEvents([](const AccumulatedTraceEvent& a, const AccumulatedTraceEvent& b) {
			return a.time > b.time;
		});

	for (const auto &p: m_accumulatedEvents)
	{
		sortedEvents.insert(p.second);
	}

	for (const AccumulatedTraceEvent& acc: sortedEvents)
	{
		std::println(
			"{:>8.3f}{:>10}       {:<25}{:<50}{}",
			acc.time,
			acc.count,
			acc.event.eventName,
			acc.event.functionName + "()",
			acc.event.locationName);
	}

	std::println("");

	m_accumulatedEvents.clear();
}

AccumulatingTracer::AccumulatingTracer() = default;

#endif	  // TRACING_ENABLED
