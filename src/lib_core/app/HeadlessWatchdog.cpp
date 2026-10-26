#include "HeadlessWatchdog.h"

#include <algorithm>

namespace
{
using Clock = HeadlessWatchdog::Clock;

// `since` is when the condition began: kept while it holds, set when it begins.
bool holdsFor(std::optional<Clock::time_point>& since, bool holds, Clock::time_point now, std::chrono::milliseconds limit)
{
	if (!holds)
	{
		since.reset();
		return false;
	}
	if (!since)
	{
		since = now;
	}
	return now - *since >= limit;
}

std::string seconds(std::chrono::milliseconds span)
{
	return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(span).count()) + " s";
}
}	 // namespace

HeadlessWatchdog::HeadlessWatchdog(Limits limits): m_limits(limits) {}

std::optional<std::string> HeadlessWatchdog::check(const Look& look)
{
	using IndexingState = IndexingStateMachine::State;
	using ProjectState = ProjectStateMachine::State;

	if (!m_last || moved(*m_last, look))
	{
		m_movedAt = look.at;
	}
	m_last = look;

	if (look.app == AppStateMachine::State::Closing)
	{
		return std::nullopt;	// it ends
	}

	const bool resting = (look.project == ProjectState::NoProject || look.project == ProjectState::Ready) &&
		look.indexing == IndexingState::Idle;
	if (holdsFor(m_restingSince, resting, look.at, m_limits.resting))
	{
		return "nothing is left to do and nothing ends the run: the project is " +
			std::string(look.project == ProjectState::NoProject ? "not loaded" : "loaded") +
			" and no indexing runs, for " + seconds(m_limits.resting);
	}

	const bool waiting = look.indexing == IndexingState::ChoosingMode || look.indexing == IndexingState::Reporting;
	if (holdsFor(m_waitingSince, waiting, look.at, m_limits.waitingForPerson))
	{
		return std::string("indexing waits for a person's answer (") +
			(look.indexing == IndexingState::ChoosingMode ? "the mode choice" : "the report") +
			"), and a headless run has none";
	}

	const bool noIndexer = look.indexing == IndexingState::Indexing && look.liveProcesses == 0;
	if (holdsFor(m_noIndexerSince, noIndexer, look.at, m_limits.indexersGone) &&
		look.at - m_movedAt >= m_limits.indexersGone)
	{
		return "no indexer process runs and no file finished for " + seconds(m_limits.indexersGone) + " (" +
			std::to_string(look.finished) + " of " + std::to_string(look.total) + " files done)";
	}

	const Clock::time_point lastSign = std::max(m_movedAt, look.lastActivity);
	if (look.at - lastSign >= m_limits.noProgress)
	{
		return "no progress for " +
			std::to_string(std::chrono::duration_cast<std::chrono::minutes>(look.at - lastSign).count()) + " minutes";
	}
	return std::nullopt;
}

bool HeadlessWatchdog::moved(const Look& before, const Look& now)
{
	return before.app != now.app || before.project != now.project || before.indexing != now.indexing ||
		before.started != now.started || before.finished != now.finished || before.total != now.total;
}
