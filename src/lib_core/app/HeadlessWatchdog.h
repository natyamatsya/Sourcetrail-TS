#ifndef HEADLESS_WATCHDOG_H
#define HEADLESS_WATCHDOG_H

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

#include "AppStateMachine.h"
#include "IndexingStateMachine.h"
#include "ProjectStateMachine.h"

// Tells when a headless run will not end by itself, from what its state machines say
// it is doing (ADR-0008's watchdog, made to know what progress is expected where):
//
// - Nothing left to do: no project loading, no indexing, and no quit coming. Nothing
//   would ever end the run (a load that loaded nothing, a refresh that started nothing).
// - Waiting for a person (the mode choice, the report): a headless run has none.
// - Indexing, but no indexer process runs and no file finished for a while.
// - Otherwise: nothing moved (no state, no file, no message) for the long limit, which
//   leaves a slow file its time.
//
// The app looks at the run every few seconds and passes each look to check().
class HeadlessWatchdog
{
public:
	using Clock = std::chrono::steady_clock;

	struct Limits
	{
		std::chrono::milliseconds resting {std::chrono::seconds(30)};
		std::chrono::milliseconds waitingForPerson {std::chrono::seconds(30)};
		std::chrono::milliseconds indexersGone {std::chrono::minutes(2)};
		std::chrono::milliseconds noProgress {std::chrono::minutes(30)};
	};

	//! What the run is at, as one look sees it.
	struct Look
	{
		Clock::time_point at;
		AppStateMachine::State app = AppStateMachine::State::Starting;
		ProjectStateMachine::State project = ProjectStateMachine::State::NoProject;
		IndexingStateMachine::State indexing = IndexingStateMachine::State::Idle;
		std::size_t started = 0;	// the run's files
		std::size_t finished = 0;
		std::size_t total = 0;
		std::size_t liveProcesses = 0;	// child processes running now (indexers, a PCH build, custom commands)
		Clock::time_point lastActivity;	// the last status or progress message
	};

	explicit HeadlessWatchdog(Limits limits);

	//! Why the run should end now, if it should.
	std::optional<std::string> check(const Look& look);

private:
	static bool moved(const Look& before, const Look& now);

	const Limits m_limits;
	std::optional<Look> m_last;
	Clock::time_point m_movedAt;	// the last look that differed from the one before
	std::optional<Clock::time_point> m_restingSince;
	std::optional<Clock::time_point> m_waitingSince;
	std::optional<Clock::time_point> m_noIndexerSince;
};

#endif	  // HEADLESS_WATCHDOG_H
